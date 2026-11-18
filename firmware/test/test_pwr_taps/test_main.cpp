// pwr_taps: a PWR double press within PWR_DOUBLE_MS powers off (this fork);
// a single press runs its usual action once that window has closed.
//   pio test -d firmware -e native -f test_pwr_taps
#include <unity.h>
#include "pwr_taps.h"

void setUp(void) {}
void tearDown(void) {}

static void test_single_waits_out_the_window(void) {
    PwrTaps t = {};
    TEST_ASSERT_EQUAL_INT(PWR_TAP_NONE, pwr_taps_press(&t, 1000));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_NONE, pwr_taps_tick(&t, 1000 + PWR_DOUBLE_MS - 1));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_SINGLE, pwr_taps_tick(&t, 1000 + PWR_DOUBLE_MS));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_NONE, pwr_taps_tick(&t, 1000 + 5 * PWR_DOUBLE_MS));   // once
}

static void test_double_inside_the_window(void) {
    PwrTaps t = {};
    TEST_ASSERT_EQUAL_INT(PWR_TAP_NONE, pwr_taps_press(&t, 1000));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_DOUBLE, pwr_taps_press(&t, 1000 + PWR_DOUBLE_MS - 1));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_NONE, pwr_taps_tick(&t, 1000 + 5 * PWR_DOUBLE_MS));   // no single after it
}

static void test_two_slow_presses_are_two_singles(void) {
    PwrTaps t = {};
    pwr_taps_press(&t, 1000);
    TEST_ASSERT_EQUAL_INT(PWR_TAP_SINGLE, pwr_taps_tick(&t, 1000 + PWR_DOUBLE_MS));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_NONE, pwr_taps_press(&t, 1000 + PWR_DOUBLE_MS + 10));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_SINGLE, pwr_taps_tick(&t, 1000 + 2 * PWR_DOUBLE_MS + 10));
    // A second press after the window but before any tick saw it: the first
    // was a single, the second starts a new window.
    pwr_taps_press(&t, 5000);
    TEST_ASSERT_EQUAL_INT(PWR_TAP_SINGLE, pwr_taps_press(&t, 5000 + PWR_DOUBLE_MS));
    TEST_ASSERT_EQUAL_INT(PWR_TAP_DOUBLE, pwr_taps_press(&t, 5000 + PWR_DOUBLE_MS + 100));
}

static void test_millis_wrap(void) {
    PwrTaps t = {};
    const uint32_t near = 0xFFFFFFFFUL - 100;
    pwr_taps_press(&t, near);
    TEST_ASSERT_EQUAL_INT(PWR_TAP_DOUBLE, pwr_taps_press(&t, near + 300));   // wraps past 0
    pwr_taps_press(&t, near);
    TEST_ASSERT_EQUAL_INT(PWR_TAP_SINGLE, pwr_taps_tick(&t, near + PWR_DOUBLE_MS));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_single_waits_out_the_window);
    RUN_TEST(test_double_inside_the_window);
    RUN_TEST(test_two_slow_presses_are_two_singles);
    RUN_TEST(test_millis_wrap);
    return UNITY_END();
}
