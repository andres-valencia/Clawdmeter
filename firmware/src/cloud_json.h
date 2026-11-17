#pragma once
#include <stdint.h>
#include "data.h"

// The device reading usage on its own over WiFi while the computer is away
// (this fork, cloud.h): what Claude's usage endpoint answers, read the way
// the app reads it (desktop/crates/clawdmeter-core/src/oauth.rs
// parse_usage_json), for subscription accounts:
//
//   {"five_hour": {"utilization": 35.0, "resets_at": "2026-10-14T02:40:00Z"},
//    "seven_day": {"utilization": 20.0, "resets_at": "..."},
//    "limits": [{"kind": "weekly_scoped", "percent": 3, "resets_at": "...",
//                "scope": {"model": {"display_name": "Fable"}}}]}
//
// Pure: no Arduino, so it runs in the native unit tests (test_cloud).

struct CloudReading {
    bool    session_running;   // a five-hour window whose reset is still ahead
    float   session_pct;       // its % (0 when none runs)
    long    session_reset;     // its reset, unix seconds (0 when none runs)
    bool    weekly_only;       // "five_hour": null beside a weekly window
    bool    has_week;
    float   weekly_pct;
    long    weekly_reset;      // unix seconds, 0 = unknown
    int16_t fable_pct;         // Fable's own weekly % (0-999), -1 = none
};

// An ISO 8601 / RFC 3339 time ("2026-10-14T02:40:00Z", fractional seconds and
// a +hh:mm / -hh:mm offset allowed) as unix seconds. False when it isn't one.
bool cloud_iso8601(const char* s, long* epoch);

// Read a 200 body at `now` (unix seconds). False when it isn't JSON or has
// no window this firmware can draw (a spend-capped account: the app shows it,
// the device on its own doesn't).
bool cloud_parse_usage(const char* body, long now, CloudReading* out);

// Minutes until `at`, rounded and clamped at 0, like the app's payload;
// -1 when `at` is 0 (unknown).
int cloud_minutes_until(long at, long now);

// A reading as one account's meters at `now` (idx and label untouched), the
// shape payload_parse gives an `o` entry: a weekly-only account carries the
// week in the session fields.
void cloud_fill_numbers(const CloudReading* r, long now, AcctNumbers* out);
