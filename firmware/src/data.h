#pragma once
#include <stdint.h>
#include "theme_palette.h"

// Plain data, no Arduino types, so payload parsing and the signal logic can be
// unit-tested on the host (test/test_signals).

// Several accounts (`ai`, `an`, `al`, `o`): the app names the account its
// numbers belong to and sends the others' latest numbers along, so the
// device can show any of them on its own (accounts.h).
#define ACCT_LABEL_LEN  11   // 10 characters + NUL (the app's ACCOUNT_LABEL_MAX)
#define ACCT_OTHERS_MAX 3    // `o` entries kept (the app's OTHER_ACCOUNTS_MAX)

// One account's meters; the fields mean what UsageData's of the same name do.
struct AcctNumbers {
    int8_t idx;              // the account's position in the app's list
    char   label[ACCT_LABEL_LEN];
    float  session_pct;
    int    session_reset_mins;
    float  weekly_pct;
    int    weekly_reset_mins;
    bool   enterprise;
    bool   weekly_only;
    int    time_pct;
    int    period_days;
    char   reset_date[12];
};

struct UsageData {
    float session_pct;       // utilization 0-100 (5h window Pro/Max; spending % Enterprise)
    int session_reset_mins;  // minutes until reset
    float weekly_pct;        // 7-day utilization (Pro/Max only; 0 for Enterprise)
    int weekly_reset_mins;   // minutes until weekly reset (Pro/Max only)
    char status[16];         // "allowed", "limited", etc.
    bool chime;              // play the session-reset chime; false unless the app opts in
    bool beep;               // Claude needs attention: play the chime once for this packet
    char alert_msg[128];     // attention message for the status line; empty = no alert
    bool alert_clear;        // the session moved on: end the alert
    bool enterprise;         // true = Enterprise spending-limit account
    bool weekly_only;        // true = no 5-hour limit (acct "week", verified Ultra):
                             //   session_pct / session_reset_mins carry the week,
                             //   copied from w / wr by payload_parse
    int time_pct;            // 0-100: fraction of billing period elapsed (Enterprise)
    int period_days;         // total billing period length in days (Enterprise)
    char reset_date[12];     // formatted reset date e.g. "Jul 1" (Enterprise)
    long clock_epoch;        // local wall-clock epoch (s) from the app; 0 = not provided
    int  clock_fmt;          // 12 or 24 (hour format from the app); defaults to 24
    // ---- Fast clock (Claude Code hooks via the app). All optional: an older
    // app sends none of them and the fields read as "absent" below, which
    // the signal logic treats exactly like "no hooks installed".
    int8_t   activity;       // `a`  0-3 tool-call activity level; -1 = absent
    char     event;          // `e`  latest hook event: 'p' prompt, 's' stop,
                             //      'b' back after a gap; '\0' = absent. Other
                             //      chars pass through for forward compat.
    bool     has_event_seq;  // `es` present
    uint32_t event_seq;      // `es` sequence number; react once per change
    int16_t  sessions;       // `n`  sessions with a hook event in the last 5 min; -1 = absent
    bool     replay;         // `rp` 1 = the usage numbers repeat the last poll;
                             //      the rate math and reset detection skip it
    int8_t   liveliness;     // `lv` 1-5 (clamped); -1 = absent (keep the saved level)
    int16_t  attn_anim_s;    // `aa` attention animation, seconds 0-120 (0 = off); -1 = absent
    int8_t   pace_marker;    // `pm` 1 = pace marker on the usage bars, 0 = off; -1 = absent
    bool     has_theme;      // `th` present and well-formed
    uint32_t theme[TH_COUNT];// `th` the palette, 0xRRGGBB per token (theme_palette.h)
    // ---- Several accounts (optional; see usage_account() below).
    int8_t   acct_idx;       // `ai` which account these numbers are; -1 = absent
    int8_t   acct_count;     // `an` accounts the app has; 0 = absent
    char     acct_label[ACCT_LABEL_LEN];   // `al` this account's name; "" = absent
    uint8_t  other_count;    // entries read from `o`
    AcctNumbers others[ACCT_OTHERS_MAX];   // `o` the other accounts' latest numbers
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};

// Which of several accounts the numbers are, or -1 when the app has only one
// (or doesn't say). A zeroed UsageData reads as -1.
static inline int usage_account(const UsageData* d) {
    return d->acct_count >= 2 ? d->acct_idx : -1;
}

// What session_pct measures: 0 the 5-hour session, 1 the week (weekly_only),
// 2 the spend (enterprise). A change of kind is not a reset or a rise.
static inline int usage_meter_kind(const UsageData* d) {
    return d->enterprise ? 2 : d->weekly_only ? 1 : 0;
}
