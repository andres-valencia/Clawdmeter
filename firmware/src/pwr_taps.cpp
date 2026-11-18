#include "pwr_taps.h"

// Pure: no Arduino. See pwr_taps.h.

PwrTap pwr_taps_press(PwrTaps* t, uint32_t now_ms) {
    if (t->pending && now_ms - t->first_ms < PWR_DOUBLE_MS) {
        t->pending = false;
        return PWR_TAP_DOUBLE;
    }
    // A press after the window closed (and before a tick saw it) is a new
    // first press; the old one was a single.
    const bool was_single = t->pending;
    t->pending = true;
    t->first_ms = now_ms;
    return was_single ? PWR_TAP_SINGLE : PWR_TAP_NONE;
}

PwrTap pwr_taps_tick(PwrTaps* t, uint32_t now_ms) {
    if (t->pending && now_ms - t->first_ms >= PWR_DOUBLE_MS) {
        t->pending = false;
        return PWR_TAP_SINGLE;
    }
    return PWR_TAP_NONE;
}
