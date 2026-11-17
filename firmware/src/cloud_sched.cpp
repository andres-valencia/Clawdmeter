#include "cloud_sched.h"
#include <string.h>

// Pure: no Arduino. See cloud_sched.h.

static bool valid(int idx) { return idx >= 0 && idx < CLOUD_ACCTS; }

static bool reached(uint32_t now_ms, uint32_t at_ms) { return (int32_t)(now_ms - at_ms) >= 0; }

void cloud_sched_clear(CloudSched* s) { memset(s, 0, sizeof(*s)); }

void cloud_sched_token(CloudSched* s, int idx, long expires) {
    if (!valid(idx)) return;
    CloudAcct& a = s->a[idx];
    a.has_token = true;
    a.expires = expires;
    a.due = true;
    a.fails = 0;
}

void cloud_sched_drop(CloudSched* s, int idx) {
    if (!valid(idx)) return;
    s->a[idx] = CloudAcct{};
}

bool cloud_sched_usable(const CloudSched* s, int idx, long now) {
    if (!valid(idx) || !s->a[idx].has_token) return false;
    const long x = s->a[idx].expires;
    return now == 0 || x == 0 || x > now + CLOUD_EXPIRY_SLACK_S;
}

bool cloud_sched_want_wifi(const CloudSched* s, bool ble_connected, uint32_t ble_down_ms, uint32_t now_ms,
                           long now, bool wifi_configured) {
    if (ble_connected || !wifi_configured) return false;
    if (!reached(now_ms, ble_down_ms + CLOUD_GRACE_MS)) return false;
    for (int i = 0; i < CLOUD_ACCTS; i++)
        if (cloud_sched_usable(s, i, now)) return true;
    return false;
}

void cloud_sched_away(CloudSched* s) {
    for (int i = 0; i < CLOUD_ACCTS; i++)
        if (s->a[i].has_token) s->a[i].due = true;
}

static bool is_due(const CloudSched* s, int i, uint32_t now_ms, long now) {
    const CloudAcct& a = s->a[i];
    return cloud_sched_usable(s, i, now) && (a.due || reached(now_ms, a.next_ms));
}

int cloud_sched_next(const CloudSched* s, uint32_t now_ms, long now, int prefer) {
    if (s->called && !reached(now_ms, s->last_call_ms + CLOUD_GAP_MS)) return -1;
    if (valid(prefer) && is_due(s, prefer, now_ms, now)) return prefer;
    for (int i = 0; i < CLOUD_ACCTS; i++)
        if (is_due(s, i, now_ms, now)) return i;
    return -1;
}

void cloud_sched_started(CloudSched* s, int idx, uint32_t now_ms) {
    s->called = true;
    s->last_call_ms = now_ms;
    if (!valid(idx)) return;
    s->a[idx].due = false;
    // Until the answer: not again for a poll interval.
    s->a[idx].next_ms = now_ms + CLOUD_POLL_MS;
}

void cloud_sched_result(CloudSched* s, int idx, CloudOutcome how, long retry_after_s, uint32_t now_ms) {
    if (!valid(idx)) return;
    CloudAcct& a = s->a[idx];
    a.due = false;
    switch (how) {
    case CLOUD_OK:
        a.fails = 0;
        a.next_ms = now_ms + CLOUD_POLL_MS;
        break;
    case CLOUD_THROTTLED: {
        const uint32_t wait = retry_after_s >= 0 ? (uint32_t)(retry_after_s > 86400 ? 86400 : retry_after_s) * 1000UL
                                                 : CLOUD_POLL_MS;
        a.next_ms = now_ms + wait;
        break;
    }
    case CLOUD_REFUSED:
        cloud_sched_drop(s, idx);
        break;
    case CLOUD_FAILED: {
        uint32_t wait = CLOUD_RETRY_MS;
        for (int i = 0; i < a.fails && wait < CLOUD_RETRY_MAX_MS; i++) wait *= 2;
        if (wait > CLOUD_RETRY_MAX_MS) wait = CLOUD_RETRY_MAX_MS;
        if (a.fails < 255) a.fails++;
        a.next_ms = now_ms + wait;
        break;
    }
    }
}
