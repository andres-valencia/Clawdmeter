#pragma once
#include <stdint.h>

// When the device reads usage on its own (this fork, cloud.h): WiFi only
// while the computer is away, the account on screen at most every
// CLOUD_POLL_SHOWN_MS and the others every CLOUD_POLL_MS, the server's pauses
// respected, a refused token dropped. Pure: times are
// millis()-style uint32 (wrap-safe) and unix seconds (0 = the clock isn't set
// yet), so test_cloud drives it on the host.

#define CLOUD_ACCTS          6                       // accounts kept
#define CLOUD_GRACE_MS       (60UL * 1000UL)         // BLE down this long before WiFi comes on
#define CLOUD_POLL_SHOWN_MS  (5UL * 60UL * 1000UL)   // the account on screen, at most
#define CLOUD_POLL_MS        (10UL * 60UL * 1000UL)  // every other account, at most
#define CLOUD_GAP_MS         (15UL * 1000UL)         // between two calls
#define CLOUD_RETRY_MS       (2UL * 60UL * 1000UL)   // after a failed call; doubles
#define CLOUD_RETRY_MAX_MS   (10UL * 60UL * 1000UL)
#define CLOUD_EXPIRY_SLACK_S 60                      // a token this close to expiry is spent

enum CloudOutcome {
    CLOUD_OK,          // 200, read
    CLOUD_THROTTLED,   // 429: wait its retry-after (else CLOUD_POLL_MS)
    CLOUD_REFUSED,     // 401 / 403: the token is dead
    CLOUD_FAILED,      // no network, timeout, 5xx, unreadable body
};

struct CloudAcct {
    bool     has_token;
    long     expires;    // unix seconds; 0 = unknown (used until refused)
    bool     due;        // call at the first chance
    uint32_t next_ms;    // else not before this (on screen)...
    uint32_t slow_ms;    // ... nor before this while another account is
    uint8_t  fails;      // failed calls in a row
};

struct CloudSched {
    CloudAcct a[CLOUD_ACCTS];
    bool      called;    // any call yet (for the gap)
    uint32_t  last_call_ms;
};

void cloud_sched_clear(CloudSched* s);

// Account `idx` has a (new) token: call it at the first chance.
void cloud_sched_token(CloudSched* s, int idx, long expires);
// Account `idx` has no usable token any more.
void cloud_sched_drop(CloudSched* s, int idx);

// Its token can still be used at `now` (unix seconds; 0 = unknown time).
bool cloud_sched_usable(const CloudSched* s, int idx, long now);

// WiFi should be on: the computer has been away CLOUD_GRACE_MS, there is a
// network to join and at least one usable token.
bool cloud_sched_want_wifi(const CloudSched* s, bool ble_connected, uint32_t ble_down_ms, uint32_t now_ms,
                           long now, bool wifi_configured);

// The computer went away: every account is due once the grace has passed.
void cloud_sched_away(CloudSched* s);

// The account to call now, or -1. `prefer` (the one on screen) goes first
// when it's due too.
int cloud_sched_next(const CloudSched* s, uint32_t now_ms, long now, int prefer);

// A call to `idx` started at `now_ms`.
void cloud_sched_started(CloudSched* s, int idx, uint32_t now_ms);

// How the call to `idx` ended. `retry_after_s` < 0 = the server gave none.
void cloud_sched_result(CloudSched* s, int idx, CloudOutcome how, long retry_after_s, uint32_t now_ms);
