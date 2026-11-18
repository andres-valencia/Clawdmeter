#pragma once
#include <stdint.h>

// PWR double press = power off (this fork). A single PWR press waits
// PWR_DOUBLE_MS for a second one before it runs its usual action
// (brightness, or what the app set as `k3`); a second press inside the
// window is a double press instead. Pure: millis()-style times, wrap-safe,
// so test_pwr_taps drives it on the host.

#define PWR_DOUBLE_MS 500

enum PwrTap {
    PWR_TAP_NONE,     // nothing to do yet
    PWR_TAP_SINGLE,   // one press, and the window closed: its usual action
    PWR_TAP_DOUBLE,   // two presses inside the window: power off
};

struct PwrTaps {
    bool     pending;    // one press seen, waiting for a second
    uint32_t first_ms;
};

// A PWR press at `now_ms`.
PwrTap pwr_taps_press(PwrTaps* t, uint32_t now_ms);

// Once per loop: PWR_TAP_SINGLE when a lone press's window has closed.
PwrTap pwr_taps_tick(PwrTaps* t, uint32_t now_ms);
