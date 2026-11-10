#pragma once
#include <stdint.h>

// What a physical button does when pressed (device settings `k1` / `k2`,
// the PRIMARY and SECONDARY buttons). The PWR button keeps its fixed jobs
// (brightness, hold to pair). Pure: no Arduino, so it runs in the native
// unit tests. The codes are the wire values; never renumber them.
enum ButtonAction {
    BTN_ACT_NONE       = 0,   // nothing (a press still wakes the screen and dismisses an alert)
    BTN_ACT_SPACE      = 1,   // HID Space: Claude Code voice-mode push to talk (primary default)
    BTN_ACT_SHIFT_TAB  = 2,   // HID Shift+Tab: mode toggle (secondary default)
    BTN_ACT_TAB        = 3,   // HID Tab
    BTN_ACT_ENTER      = 4,   // HID Enter
    BTN_ACT_ESCAPE     = 5,   // HID Escape
    BTN_ACT_BRIGHTNESS = 6,   // cycle the brightness
    BTN_ACT_COUNT
};

#define BTN_ACT_PRIMARY_DEFAULT   BTN_ACT_SPACE
#define BTN_ACT_SECONDARY_DEFAULT BTN_ACT_SHIFT_TAB

// Out-of-range codes fall back to `fallback`.
int button_action_clamp(int v, int fallback);

// The HID key and modifier an action types. False when it sends no key
// (none, brightness).
bool button_action_key(int action, uint8_t* key, uint8_t* modifier);
