#pragma once
#include "data.h"

// The device reading usage on its own over WiFi while the computer is away
// (this fork). The app sends each account's OAuth access token (never the
// refresh token; the computer stays the one that renews them) and the WiFi
// network on the AUTH characteristic; both are kept in NVS until the token
// expires. Once the BLE link has been down for CLOUD_GRACE_MS, WiFi comes on
// and Claude's usage endpoint is read for every account with a usable token
// (cloud_sched.h), and the readings reach the screen as payloads built here,
// through the same path as the app's (accounts.h, signals.h, ui.h). When
// the link is back, WiFi goes off.
//
// Compiled in only on boards built with -DCLAWD_CLOUD_POLL; elsewhere these
// are no-ops and the device behaves as without this feature.

void cloud_init(void);                    // load the saved network and tokens
void cloud_on_auth(const char* json);     // an AUTH write from the app

// Once per loop. `prefer` = the account on screen (polled first).
void cloud_tick(bool ble_connected, int prefer);

// A payload to show from the device's own readings: true after a fresh read,
// and once a minute while live (the countdowns move). `prefer` = the account
// to put first; it falls back to any account with a reading.
bool cloud_take_update(UsageData* out, int prefer);

// The numbers on screen come from the device's own reads right now.
bool cloud_live(void);
// Local wall-clock seconds of the last good read (0 = none).
long cloud_last_read_local(void);

void cloud_print_status(void);            // serial `cloud`: never prints a token
// Serial `cloud poll`: every account due now, and reads for 5 minutes even
// with the app connected (the screen keeps the app's numbers), to check the
// network, the clock and the endpoint from the serial log.
void cloud_poll_now(void);
