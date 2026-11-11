// payload_parse: long-standing fields keep their defaults, fast-clock fields
// are optional and clamped.
//   pio test -d firmware -e native -f test_payload
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "payload.h"

void setUp(void) {}
void tearDown(void) {}

static void test_rejects_malformed_json(void) {
    UsageData d = {};
    d.session_pct = 42;
    const char* err = nullptr;
    TEST_ASSERT_FALSE(payload_parse("{\"s\":", &d, &err));
    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_FLOAT(42, d.session_pct);   // untouched on failure
}

static void test_empty_object_gives_legacy_defaults(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{}", &d, nullptr));
    TEST_ASSERT_EQUAL_FLOAT(0, d.session_pct);
    TEST_ASSERT_EQUAL_INT(-1, d.session_reset_mins);
    TEST_ASSERT_EQUAL_INT(-1, d.weekly_reset_mins);
    TEST_ASSERT_EQUAL_STRING("unknown", d.status);
    TEST_ASSERT_FALSE(d.chime);
    TEST_ASSERT_FALSE(d.beep);
    TEST_ASSERT_EQUAL_STRING("", d.alert_msg);
    TEST_ASSERT_FALSE(d.enterprise);
    TEST_ASSERT_EQUAL_INT(30, d.period_days);
    TEST_ASSERT_EQUAL_INT(24, d.clock_fmt);
    TEST_ASSERT_FALSE(d.ok);
    TEST_ASSERT_TRUE(d.valid);
    // fast clock absent
    TEST_ASSERT_EQUAL_INT(-1, d.activity);
    TEST_ASSERT_EQUAL_CHAR('\0', d.event);
    TEST_ASSERT_FALSE(d.has_event_seq);
    TEST_ASSERT_EQUAL_INT(-1, d.sessions);
    TEST_ASSERT_FALSE(d.replay);
}

static void test_legacy_fields(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse(
        "{\"s\":12.5,\"sr\":240,\"w\":38,\"wr\":1500,\"st\":\"allowed\",\"c\":1,\"b\":1,"
        "\"m\":\"Claude needs your permission\",\"x\":1,\"acct\":\"ent\",\"tp\":40,"
        "\"pd\":31,\"rd\":\"Jul 1\",\"t\":1760000000,\"tf\":12,\"ok\":true}", &d, nullptr));
    TEST_ASSERT_EQUAL_FLOAT(12.5f, d.session_pct);
    TEST_ASSERT_EQUAL_INT(240, d.session_reset_mins);
    TEST_ASSERT_EQUAL_FLOAT(38, d.weekly_pct);
    TEST_ASSERT_EQUAL_INT(1500, d.weekly_reset_mins);
    TEST_ASSERT_EQUAL_STRING("allowed", d.status);
    TEST_ASSERT_TRUE(d.chime);
    TEST_ASSERT_TRUE(d.beep);
    TEST_ASSERT_EQUAL_STRING("Claude needs your permission", d.alert_msg);
    TEST_ASSERT_TRUE(d.alert_clear);
    TEST_ASSERT_TRUE(d.enterprise);
    TEST_ASSERT_EQUAL_INT(40, d.time_pct);
    TEST_ASSERT_EQUAL_INT(31, d.period_days);
    TEST_ASSERT_EQUAL_STRING("Jul 1", d.reset_date);
    TEST_ASSERT_EQUAL_INT64(1760000000L, (int64_t)d.clock_epoch);
    TEST_ASSERT_EQUAL_INT(12, d.clock_fmt);
    TEST_ASSERT_TRUE(d.ok);
}

static void test_account_kinds(void) {
    UsageData d;
    // No 5-hour limit: s/sr come as "not started"; the week becomes the main meter.
    TEST_ASSERT_TRUE(payload_parse(
        "{\"s\":0,\"sr\":-1,\"w\":48,\"wr\":7900,\"acct\":\"week\",\"ok\":true}", &d, nullptr));
    TEST_ASSERT_TRUE(d.weekly_only);
    TEST_ASSERT_FALSE(d.enterprise);
    TEST_ASSERT_EQUAL_FLOAT(48, d.session_pct);
    TEST_ASSERT_EQUAL_INT(7900, d.session_reset_mins);
    TEST_ASSERT_EQUAL_FLOAT(48, d.weekly_pct);
    TEST_ASSERT_TRUE(payload_parse("{\"acct\":\"pro\"}", &d, nullptr));
    TEST_ASSERT_FALSE(d.weekly_only);
    TEST_ASSERT_TRUE(payload_parse("{\"acct\":\"ent\"}", &d, nullptr));
    TEST_ASSERT_FALSE(d.weekly_only);
    TEST_ASSERT_TRUE(d.enterprise);
    TEST_ASSERT_TRUE(payload_parse("{}", &d, nullptr));
    TEST_ASSERT_FALSE(d.weekly_only);
}

static void test_fast_clock_fields(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{\"a\":2,\"e\":\"b\",\"es\":4000000000,\"n\":3,\"rp\":1}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(2, d.activity);
    TEST_ASSERT_EQUAL_CHAR('b', d.event);
    TEST_ASSERT_TRUE(d.has_event_seq);
    TEST_ASSERT_EQUAL_UINT32(4000000000UL, d.event_seq);
    TEST_ASSERT_EQUAL_INT(3, d.sessions);
    TEST_ASSERT_TRUE(d.replay);
}

static void test_fast_clock_clamps(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{\"a\":9,\"n\":-4,\"es\":0}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(3, d.activity);
    TEST_ASSERT_EQUAL_INT(0, d.sessions);
    TEST_ASSERT_TRUE(d.has_event_seq);             // 0 is a real seq, not absent
    TEST_ASSERT_TRUE(payload_parse("{\"a\":-1}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(0, d.activity);
}

static void test_attention_animation(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{\"s\":1}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(-1, d.attn_anim_s);      // absent: keep the saved setting
    TEST_ASSERT_TRUE(payload_parse("{\"aa\":0}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(0, d.attn_anim_s);       // off
    TEST_ASSERT_TRUE(payload_parse("{\"aa\":30}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(30, d.attn_anim_s);
    TEST_ASSERT_TRUE(payload_parse("{\"aa\":999}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(120, d.attn_anim_s);
}

static void test_liveliness(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{\"ok\":true}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(-1, d.liveliness);       // absent: keep the saved level
    TEST_ASSERT_TRUE(payload_parse("{\"lv\":1}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(1, d.liveliness);
    TEST_ASSERT_TRUE(payload_parse("{\"lv\":9}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(5, d.liveliness);
    TEST_ASSERT_TRUE(payload_parse("{\"lv\":0}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(1, d.liveliness);
}

static void test_pace_marker(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{\"ok\":true}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(-1, d.pace_marker);      // absent: keep the saved setting
    TEST_ASSERT_TRUE(payload_parse("{\"pm\":1}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(1, d.pace_marker);
    TEST_ASSERT_TRUE(payload_parse("{\"pm\":0}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(0, d.pace_marker);
    TEST_ASSERT_TRUE(payload_parse("{\"pm\":7}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(1, d.pace_marker);
}

static void test_theme(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{\"ok\":true}", &d, nullptr));
    TEST_ASSERT_FALSE(d.has_theme);                // absent: keep the saved palette
    char hex[THEME_HEX_LEN + 1];
    uint32_t want[TH_COUNT];
    memcpy(want, THEME_DEFAULTS, sizeof(want));
    want[TH_ACCENT] = 0x647d99;
    theme_format_hex(want, hex);
    char json[128];
    snprintf(json, sizeof(json), "{\"th\":\"%s\"}", hex);
    TEST_ASSERT_TRUE(payload_parse(json, &d, nullptr));
    TEST_ASSERT_TRUE(d.has_theme);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(want, d.theme, TH_COUNT);
    // Upper case parses too; the round trip formats lower case.
    for (char* c = hex; *c; c++) if (*c >= 'a' && *c <= 'f') *c -= 32;
    uint32_t got[TH_COUNT];
    TEST_ASSERT_TRUE(theme_parse_hex(hex, got));
    TEST_ASSERT_EQUAL_HEX32_ARRAY(want, got, TH_COUNT);
    // Too short, too long or not hex: ignored, never a half-applied palette.
    TEST_ASSERT_TRUE(payload_parse("{\"th\":\"647d99\"}", &d, nullptr));
    TEST_ASSERT_FALSE(d.has_theme);
    hex[5] = 'g';
    TEST_ASSERT_FALSE(theme_parse_hex(hex, got));
    TEST_ASSERT_EQUAL_HEX32_ARRAY(want, got, TH_COUNT);   // untouched
    TEST_ASSERT_TRUE(payload_parse("{\"th\":5}", &d, nullptr));
    TEST_ASSERT_FALSE(d.has_theme);
}

static void test_long_strings_truncate(void) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse("{\"st\":\"abcdefghijklmnopqrstuvwxyz\"}", &d, nullptr));
    TEST_ASSERT_EQUAL_size_t(sizeof(d.status) - 1, strlen(d.status));
}

// Several accounts: which one the numbers are, and the others' in `o`.
static void test_accounts(void) {
    UsageData d;
    // Absent: one account.
    TEST_ASSERT_TRUE(payload_parse("{\"s\":1,\"ok\":true}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(-1, d.acct_idx);
    TEST_ASSERT_EQUAL_INT(0, d.acct_count);
    TEST_ASSERT_EQUAL_STRING("", d.acct_label);
    TEST_ASSERT_EQUAL_INT(0, d.other_count);
    TEST_ASSERT_EQUAL_INT(-1, usage_account(&d));

    TEST_ASSERT_TRUE(payload_parse(
        "{\"s\":10,\"ok\":true,\"ai\":1,\"an\":2,\"al\":\"A name far too long\","
        "\"o\":[{\"i\":0,\"l\":\"Home\",\"s\":55,\"sr\":40,\"w\":70,\"wr\":900,\"acct\":\"pro\"}]}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(1, d.acct_idx);
    TEST_ASSERT_EQUAL_INT(2, d.acct_count);
    TEST_ASSERT_EQUAL_INT(1, usage_account(&d));
    TEST_ASSERT_EQUAL_STRING("A name far", d.acct_label);   // cut to ACCT_LABEL_LEN - 1
    TEST_ASSERT_EQUAL_INT(1, d.other_count);
    TEST_ASSERT_EQUAL_INT(0, d.others[0].idx);
    TEST_ASSERT_EQUAL_STRING("Home", d.others[0].label);
    TEST_ASSERT_EQUAL_FLOAT(55, d.others[0].session_pct);
    TEST_ASSERT_EQUAL_INT(40, d.others[0].session_reset_mins);
    TEST_ASSERT_EQUAL_FLOAT(70, d.others[0].weekly_pct);
    TEST_ASSERT_EQUAL_INT(900, d.others[0].weekly_reset_mins);
    TEST_ASSERT_FALSE(d.others[0].enterprise);
    TEST_ASSERT_FALSE(d.others[0].weekly_only);
    TEST_ASSERT_EQUAL_INT(30, d.others[0].period_days);

    // Entries without an index are skipped; at most ACCT_OTHERS_MAX are kept,
    // with the usual defaults for what an entry leaves out.
    TEST_ASSERT_TRUE(payload_parse(
        "{\"ok\":true,\"ai\":0,\"an\":6,\"o\":[{\"l\":\"x\"},{\"i\":1},{\"i\":2},{\"i\":3},{\"i\":4}]}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(ACCT_OTHERS_MAX, d.other_count);
    TEST_ASSERT_EQUAL_INT(1, d.others[0].idx);
    TEST_ASSERT_EQUAL_INT(3, d.others[ACCT_OTHERS_MAX - 1].idx);
    TEST_ASSERT_EQUAL_INT(-1, d.others[0].session_reset_mins);
    TEST_ASSERT_EQUAL_STRING("", d.others[0].label);

    // `o` of another type is ignored.
    TEST_ASSERT_TRUE(payload_parse("{\"ok\":true,\"ai\":0,\"an\":2,\"o\":5}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(0, d.other_count);
    // An index with a single account configured isn't "several accounts".
    TEST_ASSERT_TRUE(payload_parse("{\"ok\":true,\"ai\":0,\"an\":1}", &d, nullptr));
    TEST_ASSERT_EQUAL_INT(-1, usage_account(&d));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_malformed_json);
    RUN_TEST(test_empty_object_gives_legacy_defaults);
    RUN_TEST(test_legacy_fields);
    RUN_TEST(test_account_kinds);
    RUN_TEST(test_fast_clock_fields);
    RUN_TEST(test_fast_clock_clamps);
    RUN_TEST(test_liveliness);
    RUN_TEST(test_attention_animation);
    RUN_TEST(test_pace_marker);
    RUN_TEST(test_theme);
    RUN_TEST(test_long_strings_truncate);
    RUN_TEST(test_accounts);
    return UNITY_END();
}
