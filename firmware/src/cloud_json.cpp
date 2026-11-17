#include "cloud_json.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Pure: no Arduino. See cloud_json.h.

static bool digits(const char* s, int n, int* out) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant).
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

bool cloud_iso8601(const char* s, long* epoch) {
    if (!s) return false;
    int y, mo, d, h, mi, se;
    if (strlen(s) < 19 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':' ||
        s[16] != ':')
        return false;
    if (!digits(s, 4, &y) || !digits(s + 5, 2, &mo) || !digits(s + 8, 2, &d) || !digits(s + 11, 2, &h) ||
        !digits(s + 14, 2, &mi) || !digits(s + 17, 2, &se))
        return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return false;
    const char* p = s + 19;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') p++;
    }
    long offset = 0;
    if (*p == 'Z' || *p == 'z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        int oh, om;
        if (!digits(p + 1, 2, &oh) || p[3] != ':' || !digits(p + 4, 2, &om)) return false;
        offset = (oh * 3600L + om * 60L) * (*p == '-' ? -1 : 1);
        p += 6;
    } else if (*p != '\0') {
        return false;
    }
    if (*p != '\0') return false;
    *epoch = days_from_civil(y, mo, d) * 86400L + h * 3600L + mi * 60L + se - offset;
    return true;
}

// A window object: its % (rounded, never below 0) and reset (0 when absent).
static bool window(JsonVariantConst v, const char* pct_key, float* pct, long* reset) {
    if (!v.is<JsonObjectConst>()) return false;
    JsonVariantConst p = v[pct_key];
    if (!p.is<float>() && !p.is<int>()) return false;
    const float x = p.as<float>();
    *pct = x < 0 ? 0.0f : roundf(x);
    *reset = 0;
    const char* r = v["resets_at"] | (const char*)nullptr;
    if (r) cloud_iso8601(r, reset);
    return true;
}

bool cloud_parse_usage(const char* body, long now, CloudReading* out) {
    JsonDocument answer;
    if (deserializeJson(answer, body)) return false;
    if (!answer.is<JsonObject>()) return false;

    CloudReading r = {};
    r.fable_pct = -1;

    JsonVariantConst five = answer["five_hour"];
    float s_pct = 0;
    long s_reset = 0;
    const bool five_obj = window(five, "utilization", &s_pct, &s_reset);
    // A window whose reset has passed (or has none) isn't running.
    if (five_obj && s_reset > now) {
        r.session_running = true;
        r.session_pct = s_pct;
        r.session_reset = s_reset;
    }

    r.has_week = window(answer["seven_day"], "utilization", &r.weekly_pct, &r.weekly_reset);
    // An explicit null next to a weekly window is no 5-hour limit at all; a
    // missing key says nothing.
    bool five_key = false;
    for (JsonPairConst kv : answer.as<JsonObjectConst>()) {
        if (strcmp(kv.key().c_str(), "five_hour") == 0) five_key = true;
    }
    r.weekly_only = five_key && five.isNull() && r.has_week;

    for (JsonVariantConst l : answer["limits"].as<JsonArrayConst>()) {
        const char* kind = l["kind"] | "";
        const char* model = l["scope"]["model"]["display_name"] | "";
        if (strcmp(kind, "weekly_scoped") != 0 || strcmp(model, "Fable") != 0) continue;
        float f = 0;
        long unused = 0;
        if (window(l, "percent", &f, &unused)) r.fable_pct = (int16_t)(f > 999 ? 999 : f);
        break;
    }

    if (!five_obj && !r.has_week) return false;
    *out = r;
    return true;
}

int cloud_minutes_until(long at, long now) {
    if (at == 0) return -1;
    const long secs = at - now;
    return secs <= 0 ? 0 : (int)((secs + 30) / 60);
}

void cloud_fill_numbers(const CloudReading* r, long now, AcctNumbers* out) {
    out->enterprise = false;
    out->time_pct = 0;
    out->period_days = 30;
    out->reset_date[0] = '\0';
    out->weekly_pct = r->has_week ? r->weekly_pct : 0.0f;
    out->weekly_reset_mins = r->has_week ? cloud_minutes_until(r->weekly_reset, now) : -1;
    out->weekly_only = r->weekly_only;
    if (r->weekly_only) {
        out->session_pct = out->weekly_pct;
        out->session_reset_mins = out->weekly_reset_mins;
    } else if (r->session_running) {
        out->session_pct = r->session_pct;
        out->session_reset_mins = cloud_minutes_until(r->session_reset, now);
    } else {
        out->session_pct = 0.0f;
        out->session_reset_mins = -1;
    }
}
