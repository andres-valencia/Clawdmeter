#include "button_action.h"

int button_action_clamp(int v, int fallback) {
    return (v >= 0 && v < BTN_ACT_COUNT) ? v : fallback;
}

bool button_action_key(int action, uint8_t* key, uint8_t* modifier) {
    switch (action) {
        case BTN_ACT_SPACE:     *key = 0x2C; *modifier = 0x00; return true;
        case BTN_ACT_SHIFT_TAB: *key = 0x2B; *modifier = 0x02; return true;  // LEFT_SHIFT
        case BTN_ACT_TAB:       *key = 0x2B; *modifier = 0x00; return true;
        case BTN_ACT_ENTER:     *key = 0x28; *modifier = 0x00; return true;
        case BTN_ACT_ESCAPE:    *key = 0x29; *modifier = 0x00; return true;
        default: return false;
    }
}
