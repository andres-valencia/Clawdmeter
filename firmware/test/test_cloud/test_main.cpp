// cloud: the device reading usage itself over WiFi (this fork). The usage
// endpoint's answer read like the app reads it (cloud_json), and when to
// call it (cloud_sched).
//   pio test -d firmware -e native -f test_cloud
#include <unity.h>
#include <string.h>
#include <initializer_list>
#include "cloud_json.h"
#include "cloud_sched.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t MIN = 60UL * 1000UL;
// 2026-10-14T00:00:00Z
static const long NOW = 1791936000L;

static void test_iso8601(void) {
    long t = 0;
    TEST_ASSERT_TRUE(cloud_iso8601("1970-01-01T00:00:00Z", &t));
    TEST_ASSERT_EQUAL_INT64(0, t);
    TEST_ASSERT_TRUE(cloud_iso8601("2026-10-14T00:00:00Z", &t));
    TEST_ASSERT_EQUAL_INT64(NOW, t);
    TEST_ASSERT_TRUE(cloud_iso8601("2026-10-14T02:40:00.123456+00:00", &t));
    TEST_ASSERT_EQUAL_INT64(NOW + 2 * 3600 + 40 * 60, t);
    TEST_ASSERT_TRUE(cloud_iso8601("2026-10-13T21:00:00-05:00", &t));   // Colombia: 02:00Z
    TEST_ASSERT_EQUAL_INT64(NOW + 2 * 3600, t);
    TEST_ASSERT_TRUE(cloud_iso8601("2028-02-29T12:00:00Z", &t));          // leap day
    TEST_ASSERT_TRUE(cloud_iso8601("2026-10-14T00:00:00", &t));           // no zone: UTC
    for (const char* bad : {"", "2026-10-14", "2026-13-01T00:00:00Z", "2026-10-14T25:00:00Z",
                            "2026-10-14T00:00:00Zx", "2026-10-14T00:00:00+0500", "soon"}) {
        TEST_ASSERT_FALSE_MESSAGE(cloud_iso8601(bad, &t), bad);
    }
}

static void test_pro_with_a_running_window_and_fable(void) {
    CloudReading r;
    TEST_ASSERT_TRUE(cloud_parse_usage(
        "{\"five_hour\":{\"utilization\":35.4,\"resets_at\":\"2026-10-14T02:40:00Z\"},"
        "\"seven_day\":{\"utilization\":20.0,\"resets_at\":\"2026-10-18T22:00:00+00:00\"},"
        "\"extra_usage\":{\"is_enabled\":false},"
        "\"limits\":[{\"kind\":\"spend\",\"percent\":5},"
        "{\"kind\":\"weekly_scoped\",\"percent\":3,\"resets_at\":\"2026-10-18T22:00:00Z\","
        "\"scope\":{\"model\":{\"display_name\":\"Fable\"}}}]}",
        NOW, &r));
    TEST_ASSERT_TRUE(r.session_running);
    TEST_ASSERT_EQUAL_FLOAT(35, r.session_pct);           // rounded, like the app
    TEST_ASSERT_TRUE(r.has_week);
    TEST_ASSERT_EQUAL_FLOAT(20, r.weekly_pct);
    TEST_ASSERT_FALSE(r.weekly_only);
    TEST_ASSERT_EQUAL_INT(3, r.fable_pct);
    AcctNumbers n = {};
    n.idx = 1;
    strcpy(n.label, "Trabajo");
    cloud_fill_numbers(&r, NOW, &n);
    TEST_ASSERT_EQUAL_INT(1, n.idx);                       // kept
    TEST_ASSERT_EQUAL_STRING("Trabajo", n.label);
    TEST_ASSERT_EQUAL_FLOAT(35, n.session_pct);
    TEST_ASSERT_EQUAL_INT(160, n.session_reset_mins);
    TEST_ASSERT_EQUAL_FLOAT(20, n.weekly_pct);
    TEST_ASSERT_EQUAL_INT(4 * 1440 + 22 * 60, n.weekly_reset_mins);
    TEST_ASSERT_FALSE(n.weekly_only);
    TEST_ASSERT_FALSE(n.enterprise);
}

// A five-hour window that already reset, or with no reset, isn't running:
// the session reads "not started" (0 %, -1).
static void test_window_not_running(void) {
    CloudReading r;
    TEST_ASSERT_TRUE(cloud_parse_usage(
        "{\"five_hour\":{\"utilization\":80,\"resets_at\":\"2026-10-13T23:00:00Z\"},"
        "\"seven_day\":{\"utilization\":41,\"resets_at\":null}}", NOW, &r));
    TEST_ASSERT_FALSE(r.session_running);
    AcctNumbers n = {};
    cloud_fill_numbers(&r, NOW, &n);
    TEST_ASSERT_EQUAL_FLOAT(0, n.session_pct);
    TEST_ASSERT_EQUAL_INT(-1, n.session_reset_mins);
    TEST_ASSERT_EQUAL_FLOAT(41, n.weekly_pct);
    TEST_ASSERT_EQUAL_INT(-1, n.weekly_reset_mins);       // no reset given
    TEST_ASSERT_EQUAL_INT(-1, r.fable_pct);
    TEST_ASSERT_TRUE(cloud_parse_usage("{\"five_hour\":{\"utilization\":0,\"resets_at\":null}}", NOW, &r));
    TEST_ASSERT_FALSE(r.session_running);
    TEST_ASSERT_FALSE(r.has_week);
}

// "five_hour": null beside a week is an account with no 5-hour limit: the
// week is its main meter. A missing key says nothing.
static void test_weekly_only(void) {
    CloudReading r;
    TEST_ASSERT_TRUE(cloud_parse_usage(
        "{\"five_hour\":null,\"seven_day\":{\"utilization\":48,\"resets_at\":\"2026-10-19T00:00:00Z\"}}", NOW, &r));
    TEST_ASSERT_TRUE(r.weekly_only);
    AcctNumbers n = {};
    cloud_fill_numbers(&r, NOW, &n);
    TEST_ASSERT_TRUE(n.weekly_only);
    TEST_ASSERT_EQUAL_FLOAT(48, n.session_pct);
    TEST_ASSERT_EQUAL_INT(5 * 1440, n.session_reset_mins);
    TEST_ASSERT_TRUE(cloud_parse_usage("{\"seven_day\":{\"utilization\":48,\"resets_at\":null}}", NOW, &r));
    TEST_ASSERT_FALSE(r.weekly_only);
}

static void test_unreadable_answers(void) {
    CloudReading r;
    TEST_ASSERT_FALSE(cloud_parse_usage("", NOW, &r));
    TEST_ASSERT_FALSE(cloud_parse_usage("<html>", NOW, &r));
    TEST_ASSERT_FALSE(cloud_parse_usage("[1,2]", NOW, &r));
    TEST_ASSERT_FALSE(cloud_parse_usage("{\"error\":{\"type\":\"rate_limit_error\"}}", NOW, &r));
    // A spend-capped account: the app draws it; the device on its own doesn't.
    TEST_ASSERT_FALSE(cloud_parse_usage("{\"five_hour\":null,\"seven_day\":null,\"limits\":[{\"kind\":\"spend\","
                                        "\"percent\":54}]}", NOW, &r));
    TEST_ASSERT_FALSE(cloud_parse_usage("{\"five_hour\":{\"utilization\":\"x\"}}", NOW, &r));
}

static void test_minutes_until(void) {
    TEST_ASSERT_EQUAL_INT(-1, cloud_minutes_until(0, NOW));
    TEST_ASSERT_EQUAL_INT(0, cloud_minutes_until(NOW - 5, NOW));
    TEST_ASSERT_EQUAL_INT(1, cloud_minutes_until(NOW + 31, NOW));
    TEST_ASSERT_EQUAL_INT(0, cloud_minutes_until(NOW + 29, NOW));
    TEST_ASSERT_EQUAL_INT(80, cloud_minutes_until(NOW + 80 * 60, NOW));
}

// ---- cloud_sched ----

static void test_wifi_only_while_away_with_a_usable_token(void) {
    CloudSched s;
    cloud_sched_clear(&s);
    const uint32_t down = 1000;
    TEST_ASSERT_FALSE(cloud_sched_want_wifi(&s, false, down, down + 2 * MIN, NOW, true));   // no token
    cloud_sched_token(&s, 0, NOW + 8 * 3600);
    TEST_ASSERT_FALSE(cloud_sched_want_wifi(&s, true, down, down + 2 * MIN, NOW, true));    // app connected
    TEST_ASSERT_FALSE(cloud_sched_want_wifi(&s, false, down, down + 30000, NOW, true));      // grace
    TEST_ASSERT_TRUE(cloud_sched_want_wifi(&s, false, down, down + MIN, NOW, true));
    TEST_ASSERT_FALSE(cloud_sched_want_wifi(&s, false, down, down + MIN, NOW, false));       // no network
    // Expired (or nearly): spent. Unknown time or expiry: used until refused.
    TEST_ASSERT_FALSE(cloud_sched_want_wifi(&s, false, down, down + MIN, NOW + 8 * 3600 - 30, true));
    TEST_ASSERT_TRUE(cloud_sched_usable(&s, 0, 0));
    cloud_sched_token(&s, 1, 0);
    TEST_ASSERT_TRUE(cloud_sched_usable(&s, 1, NOW + 365L * 86400L));
    cloud_sched_drop(&s, 1);
    TEST_ASSERT_FALSE(cloud_sched_usable(&s, 1, NOW));
    TEST_ASSERT_FALSE(cloud_sched_usable(&s, 9, NOW));
}

static void test_poll_order_gap_and_interval(void) {
    CloudSched s;
    cloud_sched_clear(&s);
    cloud_sched_token(&s, 0, NOW + 8 * 3600);
    cloud_sched_token(&s, 1, NOW + 8 * 3600);
    uint32_t t = 5000;
    TEST_ASSERT_EQUAL_INT(1, cloud_sched_next(&s, t, NOW, 1));     // the one on screen first
    cloud_sched_started(&s, 1, t);
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 500, NOW, 1));    // in flight / gap
    cloud_sched_result(&s, 1, CLOUD_OK, -1, t + 800);
    // An account never read yet goes right after (1 s), so the account
    // button has every account within seconds of a boot.
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 999, NOW, 1));
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 1000, NOW, 1));
    cloud_sched_started(&s, 0, t + 1000);
    cloud_sched_result(&s, 0, CLOUD_OK, -1, t + 1500);
    // The one on screen every 5 minutes, the other every 10.
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 800 + 5 * MIN - 1, NOW, 1));
    TEST_ASSERT_EQUAL_INT(1, cloud_sched_next(&s, t + 800 + 5 * MIN, NOW, 1));
    // Neither on screen (another account is): 10 minutes for both.
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 800 + 10 * MIN - 1, NOW, 5));
    TEST_ASSERT_EQUAL_INT(1, cloud_sched_next(&s, t + 800 + 10 * MIN, NOW, 5));
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 1500 + 10 * MIN, NOW, 5));
    // Switched to account 0 on the device: it is on screen now, so 5
    // minutes after its last read is enough.
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 1500 + 5 * MIN, NOW, 0));
    // Back from the computer and away again: everything due at once, and
    // with both read before, 15 s between the calls.
    cloud_sched_away(&s);
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 3 * MIN, NOW, 0));
    cloud_sched_started(&s, 0, t + 3 * MIN);
    cloud_sched_result(&s, 0, CLOUD_OK, -1, t + 3 * MIN + 500);
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 3 * MIN + 15000 - 1, NOW, 1));
    TEST_ASSERT_EQUAL_INT(1, cloud_sched_next(&s, t + 3 * MIN + 15000, NOW, 1));
}

static void test_throttled_refused_and_failed(void) {
    CloudSched s;
    cloud_sched_clear(&s);
    cloud_sched_token(&s, 0, NOW + 8 * 3600);
    uint32_t t = 1000;
    cloud_sched_started(&s, 0, t);
    cloud_sched_result(&s, 0, CLOUD_THROTTLED, 1353, t);          // exactly its retry-after
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 1352 * 1000UL, NOW, 0));
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 1352 * 1000UL, NOW, 5));   // not on screen either
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 1353 * 1000UL, NOW, 0));
    cloud_sched_started(&s, 0, t);
    cloud_sched_result(&s, 0, CLOUD_THROTTLED, -1, t);            // none given: a poll interval
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 9 * MIN, NOW, 0));
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 10 * MIN, NOW, 0));
    // A retry-after of 0 (seen on the device) or a few seconds: still 10 min.
    for (long ra : {0L, 5L, 120L}) {
        cloud_sched_started(&s, 0, t);
        cloud_sched_result(&s, 0, CLOUD_THROTTLED, ra, t);
        TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 10 * MIN - 1, NOW, 0));
        TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 10 * MIN, NOW, 0));
    }
    // Never read yet: the first three failures retry in 10 s.
    for (int i = 0; i < 3; i++) {
        cloud_sched_started(&s, 0, t);
        cloud_sched_result(&s, 0, CLOUD_FAILED, -1, t);
        TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 9999, NOW, 0));
        TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 10000, NOW, 0));
    }
    // Then (and for an account read before) they back off 16 min capped at
    // 10; a good read resets the count.
    const uint32_t waits[] = { 10 * MIN, 10 * MIN };
    for (uint32_t w : waits) {
        cloud_sched_started(&s, 0, t);
        cloud_sched_result(&s, 0, CLOUD_FAILED, -1, t);
        TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + w - 1, NOW, 0));
        TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + w, NOW, 0));
    }
    cloud_sched_started(&s, 0, t);
    cloud_sched_result(&s, 0, CLOUD_OK, -1, t);
    // Read once: 2, 4, 8, then 10 minutes.
    const uint32_t read_waits[] = { 2 * MIN, 4 * MIN, 8 * MIN, 10 * MIN };
    for (uint32_t w : read_waits) {
        cloud_sched_started(&s, 0, t);
        cloud_sched_result(&s, 0, CLOUD_FAILED, -1, t);
        TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + w - 1, NOW, 0));
        TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + w, NOW, 0));
    }
    // Refused: the token is gone; nothing more to call.
    cloud_sched_started(&s, 0, t);
    cloud_sched_result(&s, 0, CLOUD_REFUSED, -1, t);
    TEST_ASSERT_FALSE(cloud_sched_usable(&s, 0, NOW));
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 60 * MIN, NOW, 0));
    // A new token from the app: due again.
    cloud_sched_token(&s, 0, NOW + 3600);
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 60 * MIN, NOW, 0));
}

static void test_millis_wrap(void) {
    CloudSched s;
    cloud_sched_clear(&s);
    cloud_sched_token(&s, 0, NOW + 8 * 3600);
    const uint32_t t = 0xFFFFFFFFUL - 30000UL;
    TEST_ASSERT_TRUE(cloud_sched_want_wifi(&s, false, t - MIN, t, NOW, true));
    cloud_sched_started(&s, 0, t);
    cloud_sched_result(&s, 0, CLOUD_OK, -1, t);
    TEST_ASSERT_EQUAL_INT(-1, cloud_sched_next(&s, t + 5 * MIN - 1, NOW, 0));
    TEST_ASSERT_EQUAL_INT(0, cloud_sched_next(&s, t + 5 * MIN, NOW, 0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_iso8601);
    RUN_TEST(test_pro_with_a_running_window_and_fable);
    RUN_TEST(test_window_not_running);
    RUN_TEST(test_weekly_only);
    RUN_TEST(test_unreadable_answers);
    RUN_TEST(test_minutes_until);
    RUN_TEST(test_wifi_only_while_away_with_a_usable_token);
    RUN_TEST(test_poll_order_gap_and_interval);
    RUN_TEST(test_throttled_refused_and_failed);
    RUN_TEST(test_millis_wrap);
    return UNITY_END();
}
