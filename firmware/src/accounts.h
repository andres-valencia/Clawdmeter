#pragma once
#include "data.h"

// Several accounts on one device. The app names the account its numbers
// belong to (`ai` of `an`, labelled `al`) and sends the others' latest
// numbers along (`o`). This module keeps them all, so the account button can
// show any of them at once, with or without the app, and remembers which one
// the user chose (the view).
//
// Call order per payload: payload_parse() -> accounts_ingest(raw) ->
// accounts_apply_view(copy) -> signals_ingest(copy) -> draw the copy.
// While the view is another account than the app's, the copy carries that
// account's kept numbers marked as a replay (`rp`), so they never reach the
// rate math or the reset detector; the device tells the app its view
// (TX {"view":N}) and the app then sends that account as the main one.
//
// Pure: no Arduino. Unit-tested in test/test_accounts.

void accounts_clear(void);                 // boot state; for tests

// Keep every account in an ok payload. A payload without account keys (one
// account, or an app that doesn't send them) turns switching off.
void accounts_ingest(const UsageData* d);

int  accounts_count(void);                 // accounts the app has; < 2 = switching is off
int  accounts_view(void);                  // the account chosen here; -1 = whichever the app shows
void accounts_set_view(int idx);           // restore the saved choice at boot (-1 = none)

// Choose the next account with kept numbers, in index order, wrapping.
// Returns it, or -1 when fewer than two are known (nothing to switch to).
int  accounts_next(void);

// Rewrite `d`'s meters with the chosen account's kept numbers when that is
// another account than `d`'s own. Returns true when rewritten; `d` is then a
// replay. False (and `d` untouched) when the view is `d`'s account, nothing
// was chosen, or nothing is kept for the choice.
bool accounts_apply_view(UsageData* d);

// The app's numbers are for another account than the one chosen here, and it
// could show the chosen one: tell it (ble_send_view).
bool accounts_app_behind(void);
