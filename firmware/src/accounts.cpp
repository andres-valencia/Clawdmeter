#include "accounts.h"
#include <string.h>

// Pure: no Arduino. See accounts.h.

#define ACCT_SLOTS (ACCT_OTHERS_MAX + 1)

static AcctNumbers slots[ACCT_SLOTS];
static int n_slots = 0;
static int count   = 0;    // accounts the app has (`an`); < 2 = switching off
static int app_idx = -1;   // the account the app's main numbers are (`ai`)
static int view    = -1;   // the account chosen here; -1 = the app's

static AcctNumbers* find(int idx) {
    for (int i = 0; i < n_slots; i++)
        if (slots[i].idx == idx) return &slots[i];
    return nullptr;
}

static void keep(const AcctNumbers* a) {
    AcctNumbers* s = find(a->idx);
    if (!s) {
        if (n_slots >= ACCT_SLOTS) return;
        s = &slots[n_slots++];
    }
    *s = *a;
}

void accounts_clear(void) {
    n_slots = 0;
    count = 0;
    app_idx = -1;
    view = -1;
}

void accounts_ingest(const UsageData* d) {
    // A "no data" beat says nothing about the accounts: keep what we have.
    if (!d->valid || !d->ok) return;
    const int now_count = d->acct_idx >= 0 ? d->acct_count : 0;
    if (now_count < 2) {
        n_slots = 0;
        count = 0;
        app_idx = -1;
        return;
    }
    // The list changed: an index may name another account now.
    if (now_count != count) n_slots = 0;
    count = now_count;
    app_idx = d->acct_idx;

    AcctNumbers m = {};
    m.idx = d->acct_idx;
    memcpy(m.label, d->acct_label, sizeof(m.label));
    m.session_pct = d->session_pct;
    m.session_reset_mins = d->session_reset_mins;
    m.weekly_pct = d->weekly_pct;
    m.weekly_reset_mins = d->weekly_reset_mins;
    m.enterprise = d->enterprise;
    m.weekly_only = d->weekly_only;
    m.time_pct = d->time_pct;
    m.period_days = d->period_days;
    memcpy(m.reset_date, d->reset_date, sizeof(m.reset_date));
    keep(&m);
    for (int i = 0; i < d->other_count && i < ACCT_OTHERS_MAX; i++) keep(&d->others[i]);
}

int accounts_count(void) { return count; }
int accounts_view(void) { return view; }
void accounts_set_view(int idx) { view = idx < 0 ? -1 : idx; }

int accounts_next(void) {
    if (n_slots < 2) return -1;
    const int cur = find(view) ? view : app_idx;
    int next = -1, lowest = -1;
    for (int i = 0; i < n_slots; i++) {
        const int idx = slots[i].idx;
        if (lowest < 0 || idx < lowest) lowest = idx;
        if (idx > cur && (next < 0 || idx < next)) next = idx;
    }
    view = next >= 0 ? next : lowest;
    return view;
}

bool accounts_apply_view(UsageData* d) {
    if (!d->valid || !d->ok || count < 2 || view < 0 || view == d->acct_idx) return false;
    const AcctNumbers* a = find(view);
    if (!a) return false;
    d->acct_idx = a->idx;
    memcpy(d->acct_label, a->label, sizeof(d->acct_label));
    d->session_pct = a->session_pct;
    d->session_reset_mins = a->session_reset_mins;
    d->weekly_pct = a->weekly_pct;
    d->weekly_reset_mins = a->weekly_reset_mins;
    d->enterprise = a->enterprise;
    d->weekly_only = a->weekly_only;
    d->time_pct = a->time_pct;
    d->period_days = a->period_days;
    memcpy(d->reset_date, a->reset_date, sizeof(d->reset_date));
    // Kept numbers, not a poll of this account: out of the rate math and the
    // reset detector (signals.cpp), like any replay.
    d->replay = true;
    return true;
}

bool accounts_app_behind(void) {
    return count >= 2 && view >= 0 && app_idx >= 0 && view != app_idx && find(view) != nullptr;
}
