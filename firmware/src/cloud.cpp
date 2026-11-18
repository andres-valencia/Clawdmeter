#include "cloud.h"

#ifndef CLAWD_CLOUD_POLL

// Not on this board: the device only shows what the app sends.
void cloud_init(void) {}
void cloud_on_auth(const char* json) { (void)json; }
void cloud_tick(bool ble_connected, int prefer) { (void)ble_connected; (void)prefer; }
bool cloud_take_update(UsageData* out, int prefer) { (void)out; (void)prefer; return false; }
bool cloud_live(void) { return false; }
bool cloud_has_usable_token(void) { return false; }
long cloud_last_read_local(void) { return 0; }
void cloud_print_status(void) {}
void cloud_poll_now(void) {}

#else

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "cloud_json.h"
#include "cloud_sched.h"
#include "build_info.h"

#define USAGE_URL       "https://api.anthropic.com/api/oauth/usage"
#define OAUTH_BETA      "oauth-2025-04-20"
#define TOKEN_MAX       400            // bigger than any access token seen (108)
#define BODY_MAX        8192
#define WIFI_JOIN_MS    (20UL * 1000UL)
#define WIFI_RETRY_MS   (20UL * 1000UL)
#define CLOCK_WAIT_MS   (30UL * 1000UL)   // WiFi up this long without the time: start over
#define READ_FRESH_S    (25L * 60L)    // a read older than this isn't live
#define REPLAY_MS       (60UL * 1000UL)
#define NVS_NS          "cloud"

struct Acct {
    char        token[TOKEN_MAX + 1];  // "" = none
    long        expires;               // unix seconds, 0 = unknown
    char        label[ACCT_LABEL_LEN];
    bool        has_reading;
    CloudReading reading;
    long        read_at;               // unix seconds of the read
    int         last_status;           // last HTTP status (-1 = no answer), for `cloud`
};

static Acct      accts[CLOUD_ACCTS];
static int       count = 0;           // accounts the app has (`n`)
static long      tz_offset = 0;       // the computer's UTC offset (s)
static int       hour_fmt = 24;
static char      ssid[33] = "";
static char      password[65] = "";
static CloudSched sched;

static bool      last_ble = true;
static uint32_t  ble_down_ms = 0;
static bool      away = false;        // the computer has been away the grace period

enum WifiState { WIFI_IDLE, WIFI_JOINING, WIFI_UP };
static WifiState wifi_state = WIFI_IDLE;
static uint32_t  wifi_since_ms = 0;
static uint32_t  up_since_ms = 0;
static uint32_t  wifi_begin_ms = 0;   // the last WiFi.begin
static int       wifi_rejoins = 0;    // quick retries in this join
static uint32_t  wifi_retry_ms = 0;

static bool      update_ready = false;
static int       fresh_idx = -1;
static uint32_t  last_synth_ms = 0;
static long      last_read_utc = 0;
static uint32_t  force_until_ms = 0;  // serial `cloud poll`: read now, even with the app connected

// ---- the HTTP call runs on its own task, so a TLS handshake never stalls
// the UI loop. One request at a time.
struct Req { int idx; char auth[TOKEN_MAX + 16]; };
struct Res { int idx; int status; long retry_after; char* body; };
static QueueHandle_t req_q = nullptr;
static QueueHandle_t res_q = nullptr;
static volatile bool in_flight = false;

struct HttpCtx { char* body; size_t len; long retry_after; };

static esp_err_t http_event(esp_http_client_event_t* e) {
    HttpCtx* c = (HttpCtx*)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && strcasecmp(e->header_key, "retry-after") == 0) {
        char* end = nullptr;
        const long v = strtol(e->header_value, &end, 10);
        if (end && end != e->header_value) c->retry_after = v;
    } else if (e->event_id == HTTP_EVENT_ON_DATA && c->body) {
        const size_t room = BODY_MAX - 1 - c->len;
        const size_t n = (size_t)e->data_len < room ? (size_t)e->data_len : room;
        memcpy(c->body + c->len, e->data, n);
        c->len += n;
        c->body[c->len] = '\0';
    }
    return ESP_OK;
}

static void http_task(void*) {
    Req req;
    for (;;) {
        if (xQueueReceive(req_q, &req, portMAX_DELAY) != pdTRUE) continue;
        HttpCtx ctx = { (char*)malloc(BODY_MAX), 0, -1 };
        if (ctx.body) ctx.body[0] = '\0';
        esp_http_client_config_t cfg = {};
        cfg.url = USAGE_URL;
        cfg.method = HTTP_METHOD_GET;
        cfg.timeout_ms = 20000;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.event_handler = http_event;
        cfg.user_data = &ctx;
        cfg.buffer_size = 2048;
        cfg.buffer_size_tx = 1024;
        int status = -1;
        esp_http_client_handle_t h = esp_http_client_init(&cfg);
        if (h) {
            esp_http_client_set_header(h, "Authorization", req.auth);
            esp_http_client_set_header(h, "anthropic-beta", OAUTH_BETA);
            esp_http_client_set_header(h, "User-Agent", "clawdmeter-device/" CLAWD_VERSION);
            esp_http_client_set_header(h, "Accept", "application/json");
            if (esp_http_client_perform(h) == ESP_OK) status = esp_http_client_get_status_code(h);
            esp_http_client_cleanup(h);
        }
        memset(req.auth, 0, sizeof(req.auth));
        Res res = { req.idx, status, ctx.retry_after, ctx.body };
        xQueueSend(res_q, &res, portMAX_DELAY);
    }
}

// ---- NVS: the network, each account's token, expiry and label, the clock.

static void key(char* out, size_t n, const char* base, int idx) { snprintf(out, n, "%s%d", base, idx); }

static void save_account(int i) {
    Preferences p;
    p.begin(NVS_NS, false);
    char k[8];
    key(k, sizeof(k), "tk", i);
    if (accts[i].token[0]) p.putString(k, accts[i].token);
    else p.remove(k);
    key(k, sizeof(k), "x", i);
    p.putLong64(k, accts[i].expires);
    key(k, sizeof(k), "l", i);
    p.putString(k, accts[i].label);
    p.end();
}

static void save_meta(void) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putUChar("n", (uint8_t)count);
    p.putLong("tz", tz_offset);
    p.putUChar("tf", (uint8_t)hour_fmt);
    p.end();
}

static void save_wifi(void) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString("ssid", ssid);
    p.putString("pw", password);
    p.end();
}

static void forget_token(int i, const char* why) {
    if (!accts[i].token[0]) return;
    memset(accts[i].token, 0, sizeof(accts[i].token));
    accts[i].expires = 0;
    save_account(i);
    cloud_sched_drop(&sched, i);
    Serial.printf("cloud: account %d token dropped (%s)\n", i, why);
}

static long utc_now(void) {
    const time_t t = time(nullptr);
    return t > 1700000000 ? (long)t : 0;   // 0 until NTP has set the clock
}

void cloud_init(void) {
    cloud_sched_clear(&sched);
    memset(accts, 0, sizeof(accts));
    // At boot the computer hasn't connected yet: read right away rather than
    // after the grace period, so the screen has numbers in seconds. If the
    // app connects first, WiFi stays off.
    last_ble = false;
    ble_down_ms = millis() - CLOUD_GRACE_MS;
    Preferences p;
    p.begin(NVS_NS, true);
    count = p.getUChar("n", 0);
    tz_offset = p.getLong("tz", 0);
    hour_fmt = p.getUChar("tf", 24) == 12 ? 12 : 24;
    p.getString("ssid", ssid, sizeof(ssid));
    p.getString("pw", password, sizeof(password));
    int tokens = 0;
    for (int i = 0; i < CLOUD_ACCTS; i++) {
        char k[8];
        key(k, sizeof(k), "tk", i);
        if (p.isKey(k)) p.getString(k, accts[i].token, sizeof(accts[i].token));
        key(k, sizeof(k), "x", i);
        accts[i].expires = (long)p.getLong64(k, 0);
        key(k, sizeof(k), "l", i);
        if (p.isKey(k)) p.getString(k, accts[i].label, sizeof(accts[i].label));
        accts[i].last_status = 0;
        if (accts[i].token[0]) {
            cloud_sched_token(&sched, i, accts[i].expires);
            tokens++;
        }
    }
    p.end();
    req_q = xQueueCreate(1, sizeof(Req));
    res_q = xQueueCreate(1, sizeof(Res));
    xTaskCreatePinnedToCore(http_task, "cloud_http", 12288, nullptr, 1, nullptr, 0);
    Serial.printf("cloud: %s, %d saved token(s)\n", ssid[0] ? "WiFi network saved" : "no WiFi network", tokens);
}

void cloud_on_auth(const char* json) {
    JsonDocument msg;
    if (deserializeJson(msg, json)) {
        Serial.println("cloud: unreadable AUTH write");
        return;
    }
    if (msg["wifi"].is<JsonObject>()) {
        const char* s = msg["wifi"]["s"] | "";
        const char* pw = msg["wifi"]["p"] | "";
        if (strcmp(s, ssid) != 0 || strcmp(pw, password) != 0) {
            snprintf(ssid, sizeof(ssid), "%s", s);
            snprintf(password, sizeof(password), "%s", pw);
            save_wifi();
            Serial.printf("cloud: WiFi network set (%s)\n", ssid[0] ? ssid : "none");
        }
        return;
    }
    const int i = msg["i"] | -1;
    const char* tk = msg["tk"] | "";
    if (i < 0 || i >= CLOUD_ACCTS || !tk[0] || strlen(tk) > TOKEN_MAX) {
        Serial.println("cloud: AUTH write without a usable account token");
        return;
    }
    const int n = msg["n"] | count;
    const long tz = msg["tz"] | tz_offset;
    const int tf = (msg["tf"] | hour_fmt) == 12 ? 12 : 24;
    if (n != count || tz != tz_offset || tf != hour_fmt) {
        count = n;
        tz_offset = tz;
        hour_fmt = tf;
        save_meta();
    }
    Acct& a = accts[i];
    const long x = msg["x"] | 0L;
    const char* l = msg["l"] | "";
    if (strcmp(a.token, tk) != 0 || a.expires != x || strcmp(a.label, l) != 0) {
        snprintf(a.token, sizeof(a.token), "%s", tk);
        a.expires = x;
        snprintf(a.label, sizeof(a.label), "%s", l);
        save_account(i);
        cloud_sched_token(&sched, i, x);
        Serial.printf("cloud: account %d (%s) token saved, expires %ld\n", i, a.label, x);
    }
}

static void wifi_off(void) {
    if (wifi_state == WIFI_IDLE) return;
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    wifi_state = WIFI_IDLE;
    Serial.println("cloud: WiFi off");
}

static void handle_result(const Res& r, uint32_t now_ms) {
    in_flight = false;
    if (r.idx < 0 || r.idx >= CLOUD_ACCTS) {
        free(r.body);
        return;
    }
    Acct& a = accts[r.idx];
    a.last_status = r.status;
    const long now = utc_now();
    CloudOutcome how = CLOUD_FAILED;
    if (r.status == 200 && r.body) {
        CloudReading reading;
        if (cloud_parse_usage(r.body, now, &reading)) {
            a.reading = reading;
            a.has_reading = true;
            a.read_at = now;
            last_read_utc = now;
            update_ready = true;
            fresh_idx = r.idx;
            how = CLOUD_OK;
        }
    } else if (r.status == 429) {
        how = CLOUD_THROTTLED;
    } else if (r.status == 401 || r.status == 403) {
        how = CLOUD_REFUSED;
    }
    if (r.status == 429) Serial.printf("cloud: account %d retry-after %ld s\n", r.idx, r.retry_after);
    Serial.printf("cloud: account %d read -> HTTP %d (%s)\n", r.idx, r.status,
                  how == CLOUD_OK ? "ok" : how == CLOUD_THROTTLED ? "paused" : how == CLOUD_REFUSED ? "refused" : "failed");
    cloud_sched_result(&sched, r.idx, how, r.retry_after, now_ms);
    if (how == CLOUD_REFUSED) forget_token(r.idx, "refused");
    free(r.body);
}

void cloud_tick(bool ble_connected, int prefer) {
    const uint32_t now_ms = millis();
    const long now = utc_now();

    Res r;
    if (res_q && xQueueReceive(res_q, &r, 0) == pdTRUE) handle_result(r, now_ms);

    if (ble_connected != last_ble) {
        last_ble = ble_connected;
        if (!ble_connected) ble_down_ms = now_ms;
        away = false;
    }

    // A token past its expiry is spent: drop it (the clock must be set).
    if (now) {
        for (int i = 0; i < CLOUD_ACCTS; i++)
            if (accts[i].token[0] && accts[i].expires && accts[i].expires <= now) forget_token(i, "expired");
    }

    const bool forced = force_until_ms && (int32_t)(force_until_ms - now_ms) > 0;
    const bool want = cloud_sched_want_wifi(&sched, forced ? false : ble_connected,
                                            forced ? now_ms - CLOUD_GRACE_MS : ble_down_ms, now_ms, now,
                                            ssid[0] != '\0');
    if (want && !away) {
        away = true;
        cloud_sched_away(&sched);
    }
    if (!want) {
        if (!in_flight) wifi_off();
        return;
    }

    switch (wifi_state) {
    case WIFI_IDLE:
        if ((int32_t)(now_ms - wifi_retry_ms) < 0) return;
        if (!WiFi.mode(WIFI_STA)) {
            Serial.printf("cloud: WiFi didn't start (internal heap free %u); retrying in %lu s\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                          WIFI_RETRY_MS / 1000UL);
            WiFi.mode(WIFI_OFF);
            wifi_retry_ms = now_ms + WIFI_RETRY_MS;
            return;
        }
        WiFi.begin(ssid, password);
        wifi_state = WIFI_JOINING;
        wifi_since_ms = now_ms;
        wifi_begin_ms = now_ms;
        wifi_rejoins = 0;
        Serial.printf("cloud: joining %s (internal heap free %u)\n", ssid,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        return;
    case WIFI_JOINING:
        if (WiFi.status() == WL_CONNECTED) {
            wifi_state = WIFI_UP;
            up_since_ms = now_ms;
            Serial.printf("cloud: WiFi up (%s)\n", WiFi.localIP().toString().c_str());
            // Every join restarts the time sync: a first try that failed
            // (no DNS yet, say) would otherwise back off for a long while.
            configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
        } else if (WiFi.status() == WL_CONNECT_FAILED && now_ms - wifi_since_ms < WIFI_JOIN_MS &&
                   now_ms - wifi_begin_ms >= 1000UL && wifi_rejoins < 3) {
            // The join failed outright (about 1 boot in 8 on the 2.16, right
            // after power-up): ask again now rather than wait out the timeout.
            wifi_rejoins++;
            wifi_begin_ms = now_ms;
            Serial.printf("cloud: WiFi join failed; trying again (%d)\n", wifi_rejoins);
            WiFi.disconnect(false, false);
            WiFi.begin(ssid, password);
        } else if (now_ms - wifi_since_ms >= WIFI_JOIN_MS) {
            Serial.printf("cloud: WiFi join timed out (status %d); retrying in %lu s\n", (int)WiFi.status(),
                          WIFI_RETRY_MS / 1000UL);
            wifi_off();
            wifi_retry_ms = now_ms + WIFI_RETRY_MS;
        }
        return;
    case WIFI_UP:
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("cloud: WiFi lost");
            wifi_off();
            wifi_retry_ms = now_ms + WIFI_RETRY_MS;
            return;
        }
        // No time from NTP: reads need it (TLS, the countdowns). Start over.
        if (!now && !in_flight && now_ms - up_since_ms >= CLOCK_WAIT_MS) {
            Serial.println("cloud: no time from NTP after 30 s on WiFi; starting over");
            wifi_off();
            wifi_retry_ms = now_ms + 2000UL;
            return;
        }
        break;
    }

    // TLS needs the real time to check the certificate.
    if (!now || in_flight) return;
    const int idx = cloud_sched_next(&sched, now_ms, now, prefer);
    if (idx < 0) return;
    Req req;
    req.idx = idx;
    snprintf(req.auth, sizeof(req.auth), "Bearer %s", accts[idx].token);
    cloud_sched_started(&sched, idx, now_ms);
    in_flight = true;
    if (xQueueSend(req_q, &req, 0) != pdTRUE) in_flight = false;
    memset(req.auth, 0, sizeof(req.auth));
}

static bool reading_live(int i, long now) {
    return i >= 0 && i < CLOUD_ACCTS && accts[i].has_reading && cloud_sched_usable(&sched, i, now) &&
           now - accts[i].read_at < READ_FRESH_S;
}

bool cloud_live(void) {
    if (wifi_state == WIFI_IDLE && !away) return false;
    if (last_ble) return false;
    const long now = utc_now();
    if (!now) return false;
    for (int i = 0; i < CLOUD_ACCTS; i++)
        if (reading_live(i, now)) return true;
    return false;
}

bool cloud_has_usable_token(void) {
    const long now = utc_now();
    for (int i = 0; i < CLOUD_ACCTS; i++)
        if (accts[i].token[0] && cloud_sched_usable(&sched, i, now)) return true;
    return false;
}

long cloud_last_read_local(void) { return last_read_utc ? last_read_utc + tz_offset : 0; }

bool cloud_take_update(UsageData* out, int prefer) {
    if (!cloud_live()) {
        update_ready = false;
        return false;
    }
    const uint32_t now_ms = millis();
    if (!update_ready && now_ms - last_synth_ms < REPLAY_MS) return false;
    const long now = utc_now();
    int main = reading_live(prefer, now) ? prefer : -1;
    for (int i = 0; i < CLOUD_ACCTS && main < 0; i++)
        if (reading_live(i, now)) main = i;
    if (main < 0) return false;

    UsageData d = {};
    AcctNumbers m = {};
    cloud_fill_numbers(&accts[main].reading, now, &m);
    d.session_pct = m.session_pct;
    d.session_reset_mins = m.session_reset_mins;
    d.weekly_pct = m.weekly_pct;
    d.weekly_reset_mins = m.weekly_reset_mins;
    d.weekly_only = m.weekly_only;
    d.period_days = 30;
    d.fable_pct = accts[main].reading.fable_pct;
    snprintf(d.status, sizeof(d.status), "allowed");
    d.resets_held = -1;
    d.resets_expiry_n = -1;
    d.clock_epoch = now + tz_offset;
    d.clock_fmt = hour_fmt;
    d.activity = -1;
    d.sessions = -1;
    d.liveliness = -1;
    d.attn_anim_s = -1;
    d.pace_marker = -1;
    d.btn_primary = d.btn_secondary = d.btn_pwr = -1;
    const int n = count > 0 ? count : 1;
    d.acct_count = (int8_t)(n > 127 ? 127 : n);
    d.acct_idx = (int8_t)main;
    snprintf(d.acct_label, sizeof(d.acct_label), "%s", accts[main].label);
    for (int i = 0; i < CLOUD_ACCTS && d.other_count < ACCT_OTHERS_MAX; i++) {
        if (i == main || !reading_live(i, now)) continue;
        AcctNumbers& o = d.others[d.other_count++];
        o.idx = (int8_t)i;
        snprintf(o.label, sizeof(o.label), "%s", accts[i].label);
        cloud_fill_numbers(&accts[i].reading, now, &o);
    }
    // Only a fresh read of the account on screen is a real sample (rate
    // math, reset chime); the rest repeat numbers already shown.
    d.replay = !(update_ready && fresh_idx == main);
    d.ok = true;
    d.valid = true;
    update_ready = false;
    last_synth_ms = now_ms;
    *out = d;
    return true;
}

void cloud_print_status(void) {
    const long now = utc_now();
    Serial.printf("{\"cloud\":{\"wifi\":\"%s\",\"network\":%s,\"state\":\"%s\",\"clock\":%s,\"live\":%s,\"accounts\":[",
                  ssid, ssid[0] ? "true" : "false",
                  wifi_state == WIFI_UP ? "up" : wifi_state == WIFI_JOINING ? "joining" : "off",
                  now ? "true" : "false", cloud_live() ? "true" : "false");
    bool first = true;
    for (int i = 0; i < CLOUD_ACCTS; i++) {
        if (!accts[i].token[0] && !accts[i].has_reading) continue;
        Serial.printf("%s{\"i\":%d,\"l\":\"%s\",\"token\":%s,\"expires_in\":%ld,\"read_age\":%ld,\"http\":%d}",
                      first ? "" : ",", i, accts[i].label, accts[i].token[0] ? "true" : "false",
                      now && accts[i].expires ? accts[i].expires - now : -1L,
                      now && accts[i].has_reading ? now - accts[i].read_at : -1L, accts[i].last_status);
        first = false;
    }
    Serial.printf("],\"heap\":{\"internal_free\":%u,\"internal_largest\":%u,\"psram_free\":%u}}}\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void cloud_poll_now(void) {
    cloud_sched_away(&sched);
    force_until_ms = millis() + 5UL * 60UL * 1000UL;
    if (!force_until_ms) force_until_ms = 1;
    Serial.println("cloud: every account due; reading for 5 minutes even with the app connected");
}

#endif
