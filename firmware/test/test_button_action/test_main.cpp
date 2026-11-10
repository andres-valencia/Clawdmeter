// button_action: the codes the app sends and the HID keys they type.
//   pio test -d firmware -e native -f test_button_action
#include <unity.h>
#include "button_action.h"

void setUp(void) {}
void tearDown(void) {}

static void test_keys(void) {
    uint8_t k = 0, m = 0;
    TEST_ASSERT_TRUE(button_action_key(BTN_ACT_SPACE, &k, &m));
    TEST_ASSERT_EQUAL_HEX8(0x2C, k);
    TEST_ASSERT_EQUAL_HEX8(0x00, m);
    TEST_ASSERT_TRUE(button_action_key(BTN_ACT_SHIFT_TAB, &k, &m));
    TEST_ASSERT_EQUAL_HEX8(0x2B, k);
    TEST_ASSERT_EQUAL_HEX8(0x02, m);
    TEST_ASSERT_TRUE(button_action_key(BTN_ACT_ENTER, &k, &m));
    TEST_ASSERT_EQUAL_HEX8(0x28, k);
    TEST_ASSERT_TRUE(button_action_key(BTN_ACT_ESCAPE, &k, &m));
    TEST_ASSERT_EQUAL_HEX8(0x29, k);
}

static void test_no_key(void) {
    uint8_t k = 0, m = 0;
    TEST_ASSERT_FALSE(button_action_key(BTN_ACT_NONE, &k, &m));
    TEST_ASSERT_FALSE(button_action_key(BTN_ACT_BRIGHTNESS, &k, &m));
    TEST_ASSERT_FALSE(button_action_key(99, &k, &m));
}

static void test_clamp(void) {
    TEST_ASSERT_EQUAL(BTN_ACT_TAB, button_action_clamp(3, BTN_ACT_SPACE));
    TEST_ASSERT_EQUAL(BTN_ACT_SPACE, button_action_clamp(-1, BTN_ACT_SPACE));
    TEST_ASSERT_EQUAL(BTN_ACT_SPACE, button_action_clamp(BTN_ACT_COUNT, BTN_ACT_SPACE));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_keys);
    RUN_TEST(test_no_key);
    RUN_TEST(test_clamp);
    return UNITY_END();
}
