#include "payload.h"
#include <ArduinoJson.h>
#include <string.h>

// strlcpy isn't in every host libc; this is all we need of it.
static void copy_str(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

bool payload_parse(const char* json, UsageData* out, const char** err) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json);
    if (e) {
        if (err) *err = e.c_str();
        return false;
    }

    UsageData d = {};
    d.session_pct = doc["s"] | 0.0f;
    d.session_reset_mins = doc["sr"] | -1;
    d.weekly_pct = doc["w"] | 0.0f;
    d.weekly_reset_mins = doc["wr"] | -1;
    copy_str(d.status, sizeof(d.status), doc["st"] | "unknown");
    // as<bool>, not `| false`: daemons send 1, and `|` only accepts a JSON bool.
    d.chime = doc["c"].as<bool>();   // absent (old daemon / chime off) → stay silent
    d.beep = doc["b"].as<bool>();
    copy_str(d.alert_msg, sizeof(d.alert_msg), doc["m"] | "");
    d.alert_clear = doc["x"].as<bool>();
    const char* acct = doc["acct"] | "pro";
    d.enterprise = (strcmp(acct, "ent") == 0);
    d.weekly_only = (strcmp(acct, "week") == 0);
    if (d.weekly_only) {
        // No 5-hour limit: the app leaves s/sr "not started" for firmware
        // that predates "week"; here the week is the main meter.
        d.session_pct = d.weekly_pct;
        d.session_reset_mins = d.weekly_reset_mins;
    }
    d.time_pct = doc["tp"] | 0;
    d.period_days = doc["pd"] | 30;
    copy_str(d.reset_date, sizeof(d.reset_date), doc["rd"] | "");
    d.clock_epoch = doc["t"] | 0L;
    d.clock_fmt = doc["tf"] | 24;

    // ---- fast clock (optional) ----
    JsonVariant a = doc["a"];
    if (a.isNull()) {
        d.activity = -1;
    } else {
        int lvl = a | 0;
        d.activity = (int8_t)(lvl < 0 ? 0 : lvl > 3 ? 3 : lvl);
    }
    const char* ev = doc["e"] | "";
    d.event = ev[0];
    JsonVariant es = doc["es"];
    d.has_event_seq = !es.isNull();
    d.event_seq = d.has_event_seq ? es.as<uint32_t>() : 0;
    JsonVariant n = doc["n"];
    if (n.isNull()) {
        d.sessions = -1;
    } else {
        int v = n | 0;
        d.sessions = (int16_t)(v < 0 ? 0 : v > 999 ? 999 : v);
    }
    d.replay = doc["rp"].as<bool>();
    JsonVariant lv = doc["lv"];
    if (lv.isNull()) {
        d.liveliness = -1;
    } else {
        int v = lv | 3;
        d.liveliness = (int8_t)(v < 1 ? 1 : v > 5 ? 5 : v);
    }
    JsonVariant aa = doc["aa"];
    if (aa.isNull()) {
        d.attn_anim_s = -1;
    } else {
        int v = aa | 120;
        d.attn_anim_s = (int16_t)(v < 0 ? 0 : v > 120 ? 120 : v);
    }

    JsonVariant pm = doc["pm"];
    d.pace_marker = pm.isNull() ? -1 : (int8_t)((pm | 0) ? 1 : 0);
    d.has_theme = theme_parse_hex(doc["th"] | "", d.theme);

    // ---- several accounts (optional) ----
    JsonVariant ai = doc["ai"];
    if (ai.isNull()) {
        d.acct_idx = -1;
    } else {
        int v = ai | 0;
        d.acct_idx = (int8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    }
    {
        int v = doc["an"] | 0;
        d.acct_count = (int8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    }
    copy_str(d.acct_label, sizeof(d.acct_label), doc["al"] | "");
    for (JsonVariant entry : doc["o"].as<JsonArray>()) {
        if (d.other_count >= ACCT_OTHERS_MAX) break;
        JsonVariant i = entry["i"];
        if (i.isNull()) continue;
        const int idx = i | -1;
        if (idx < 0 || idx > 127) continue;
        AcctNumbers& o = d.others[d.other_count++];
        o.idx = (int8_t)idx;
        copy_str(o.label, sizeof(o.label), entry["l"] | "");
        o.session_pct = entry["s"] | 0.0f;
        o.session_reset_mins = entry["sr"] | -1;
        o.weekly_pct = entry["w"] | 0.0f;
        o.weekly_reset_mins = entry["wr"] | -1;
        const char* kind = entry["acct"] | "pro";
        o.enterprise = (strcmp(kind, "ent") == 0);
        o.weekly_only = (strcmp(kind, "week") == 0);
        if (o.weekly_only) {   // as the main numbers above
            o.session_pct = o.weekly_pct;
            o.session_reset_mins = o.weekly_reset_mins;
        }
        o.time_pct = entry["tp"] | 0;
        o.period_days = entry["pd"] | 30;
        copy_str(o.reset_date, sizeof(o.reset_date), entry["rd"] | "");
    }

    d.ok = doc["ok"] | false;
    d.valid = true;
    *out = d;
    return true;
}
