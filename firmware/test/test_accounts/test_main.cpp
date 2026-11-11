// accounts: the numbers kept per account, the account chosen on the device
// (the view), and the rewrite of a payload to that account.
//   pio test -d firmware -e native -f test_accounts
#include <unity.h>
#include <string.h>
#include "payload.h"
#include "accounts.h"

void setUp(void) { accounts_clear(); }
void tearDown(void) {}

static UsageData parse(const char* json) {
    UsageData d;
    TEST_ASSERT_TRUE(payload_parse(json, &d, nullptr));
    return d;
}

// The app shows account 0 ("Home") and sends account 1 ("Work") along.
static const char* const HOME_SHOWN =
    "{\"s\":10,\"sr\":100,\"w\":20,\"wr\":5000,\"acct\":\"pro\",\"ok\":true,\"ai\":0,\"an\":2,\"al\":\"Home\","
    "\"o\":[{\"i\":1,\"l\":\"Work\",\"s\":55,\"sr\":40,\"w\":70,\"wr\":900,\"acct\":\"pro\"}]}";
// The app shows account 1 and sends account 0 along.
static const char* const WORK_SHOWN =
    "{\"s\":56,\"sr\":39,\"w\":70,\"wr\":899,\"acct\":\"pro\",\"ok\":true,\"ai\":1,\"an\":2,\"al\":\"Work\","
    "\"o\":[{\"i\":0,\"l\":\"Home\",\"s\":10,\"sr\":99,\"w\":20,\"wr\":4999,\"acct\":\"pro\"}]}";

static void test_one_account_has_nothing_to_switch(void) {
    UsageData d = parse("{\"s\":12,\"sr\":200,\"w\":30,\"wr\":5000,\"ok\":true}");
    accounts_ingest(&d);
    TEST_ASSERT_EQUAL_INT(0, accounts_count());
    TEST_ASSERT_EQUAL_INT(-1, accounts_next());
    TEST_ASSERT_EQUAL_INT(-1, accounts_view());
    TEST_ASSERT_FALSE(accounts_apply_view(&d));
    TEST_ASSERT_FALSE(accounts_app_behind());
    TEST_ASSERT_EQUAL_FLOAT(12, d.session_pct);
    TEST_ASSERT_FALSE(d.replay);
}

static void test_follows_the_app_until_a_choice(void) {
    UsageData d = parse(HOME_SHOWN);
    accounts_ingest(&d);
    TEST_ASSERT_EQUAL_INT(2, accounts_count());
    TEST_ASSERT_EQUAL_INT(-1, accounts_view());
    TEST_ASSERT_FALSE(accounts_apply_view(&d));
    TEST_ASSERT_FALSE(accounts_app_behind());
    // The app moves to the other account by itself: still followed.
    d = parse(WORK_SHOWN);
    accounts_ingest(&d);
    TEST_ASSERT_FALSE(accounts_apply_view(&d));
    TEST_ASSERT_EQUAL_FLOAT(56, d.session_pct);
}

static void test_button_cycles_and_rewrites(void) {
    UsageData raw = parse(HOME_SHOWN);
    accounts_ingest(&raw);

    TEST_ASSERT_EQUAL_INT(1, accounts_next());          // from the app's (0) to 1
    TEST_ASSERT_EQUAL_INT(1, accounts_view());
    TEST_ASSERT_TRUE(accounts_app_behind());
    UsageData d = raw;
    TEST_ASSERT_TRUE(accounts_apply_view(&d));
    TEST_ASSERT_EQUAL_INT(1, d.acct_idx);
    TEST_ASSERT_EQUAL_STRING("Work", d.acct_label);
    TEST_ASSERT_EQUAL_FLOAT(55, d.session_pct);
    TEST_ASSERT_EQUAL_INT(40, d.session_reset_mins);
    TEST_ASSERT_EQUAL_FLOAT(70, d.weekly_pct);
    TEST_ASSERT_EQUAL_INT(900, d.weekly_reset_mins);
    TEST_ASSERT_TRUE(d.replay);                         // kept numbers, not a poll
    TEST_ASSERT_TRUE(d.ok);

    TEST_ASSERT_EQUAL_INT(0, accounts_next());          // wraps to the lowest
    TEST_ASSERT_FALSE(accounts_app_behind());
    d = raw;
    TEST_ASSERT_FALSE(accounts_apply_view(&d));         // the app's own account: untouched
    TEST_ASSERT_EQUAL_FLOAT(10, d.session_pct);
    TEST_ASSERT_FALSE(d.replay);
}

// Once the app has caught up (it sends the chosen account as its main one),
// the payload is a real sample again and nothing is rewritten.
static void test_app_catches_up(void) {
    UsageData d = parse(HOME_SHOWN);
    accounts_ingest(&d);
    accounts_next();                                    // -> 1
    d = parse(WORK_SHOWN);
    accounts_ingest(&d);
    TEST_ASSERT_FALSE(accounts_app_behind());
    TEST_ASSERT_FALSE(accounts_apply_view(&d));
    TEST_ASSERT_FALSE(d.replay);
    TEST_ASSERT_EQUAL_FLOAT(56, d.session_pct);
    // The app's other account is refreshed from `o` meanwhile.
    accounts_next();                                    // -> 0
    d = parse(WORK_SHOWN);
    TEST_ASSERT_TRUE(accounts_apply_view(&d));
    TEST_ASSERT_EQUAL_INT(99, d.session_reset_mins);
    TEST_ASSERT_EQUAL_STRING("Home", d.acct_label);
}

// The saved choice is restored at boot, before any payload.
static void test_saved_view_applies_to_the_first_payload(void) {
    accounts_set_view(1);
    UsageData d = parse(HOME_SHOWN);
    accounts_ingest(&d);
    TEST_ASSERT_TRUE(accounts_app_behind());
    TEST_ASSERT_TRUE(accounts_apply_view(&d));
    TEST_ASSERT_EQUAL_FLOAT(55, d.session_pct);
    // A choice nothing is kept for shows the app's account and stays quiet.
    accounts_set_view(3);
    d = parse(HOME_SHOWN);
    TEST_ASSERT_FALSE(accounts_apply_view(&d));
    TEST_ASSERT_FALSE(accounts_app_behind());
    TEST_ASSERT_EQUAL_INT(1, accounts_next());          // the button starts from the app's (0)
}

static void test_no_data_keeps_the_accounts(void) {
    UsageData d = parse(HOME_SHOWN);
    accounts_ingest(&d);
    accounts_next();                                    // -> 1
    UsageData none = parse("{\"ok\":false}");
    accounts_ingest(&none);
    TEST_ASSERT_EQUAL_INT(2, accounts_count());
    TEST_ASSERT_FALSE(accounts_apply_view(&none));      // nothing to rewrite in "no data"
    TEST_ASSERT_FALSE(none.ok);
    d = parse(HOME_SHOWN);
    TEST_ASSERT_TRUE(accounts_apply_view(&d));
}

// A payload without the account keys (the user went back to one account, or
// another app version): switching is off and nothing is rewritten.
static void test_back_to_one_account(void) {
    UsageData d = parse(HOME_SHOWN);
    accounts_ingest(&d);
    accounts_next();
    d = parse("{\"s\":12,\"sr\":200,\"w\":30,\"wr\":5000,\"ok\":true}");
    accounts_ingest(&d);
    TEST_ASSERT_EQUAL_INT(0, accounts_count());
    TEST_ASSERT_FALSE(accounts_apply_view(&d));
    TEST_ASSERT_FALSE(accounts_app_behind());
    TEST_ASSERT_EQUAL_INT(-1, accounts_next());
}

static void test_other_account_kinds(void) {
    UsageData d = parse(
        "{\"s\":10,\"sr\":100,\"w\":20,\"wr\":5000,\"acct\":\"pro\",\"ok\":true,\"ai\":0,\"an\":3,\"al\":\"Home\","
        "\"o\":[{\"i\":1,\"l\":\"Ultra\",\"s\":0,\"sr\":-1,\"w\":48,\"wr\":7900,\"acct\":\"week\"},"
        "{\"i\":2,\"l\":\"Corp\",\"s\":41,\"sr\":23040,\"w\":0,\"wr\":0,\"acct\":\"ent\",\"tp\":48,\"pd\":31,\"rd\":\"Nov 1\"}]}");
    accounts_ingest(&d);
    TEST_ASSERT_EQUAL_INT(3, accounts_count());

    TEST_ASSERT_EQUAL_INT(1, accounts_next());
    UsageData v = d;
    TEST_ASSERT_TRUE(accounts_apply_view(&v));
    TEST_ASSERT_TRUE(v.weekly_only);                    // the week is its main meter
    TEST_ASSERT_FALSE(v.enterprise);
    TEST_ASSERT_EQUAL_FLOAT(48, v.session_pct);
    TEST_ASSERT_EQUAL_INT(7900, v.session_reset_mins);

    TEST_ASSERT_EQUAL_INT(2, accounts_next());
    v = d;
    TEST_ASSERT_TRUE(accounts_apply_view(&v));
    TEST_ASSERT_TRUE(v.enterprise);
    TEST_ASSERT_FALSE(v.weekly_only);
    TEST_ASSERT_EQUAL_FLOAT(41, v.session_pct);
    TEST_ASSERT_EQUAL_INT(48, v.time_pct);
    TEST_ASSERT_EQUAL_INT(31, v.period_days);
    TEST_ASSERT_EQUAL_STRING("Nov 1", v.reset_date);

    TEST_ASSERT_EQUAL_INT(0, accounts_next());          // 2 -> wraps to 0
}

// A different number of accounts means the list changed: the kept numbers
// are dropped, since an index may name another account now.
static void test_account_list_change_drops_kept_numbers(void) {
    UsageData d = parse(HOME_SHOWN);
    accounts_ingest(&d);
    accounts_next();                                    // -> 1
    d = parse("{\"s\":10,\"sr\":100,\"w\":20,\"wr\":5000,\"ok\":true,\"ai\":0,\"an\":3,\"al\":\"Home\"}");
    accounts_ingest(&d);
    TEST_ASSERT_EQUAL_INT(3, accounts_count());
    TEST_ASSERT_FALSE(accounts_apply_view(&d));         // nothing kept for 1 any more
    TEST_ASSERT_EQUAL_INT(-1, accounts_next());         // only one account known
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_one_account_has_nothing_to_switch);
    RUN_TEST(test_follows_the_app_until_a_choice);
    RUN_TEST(test_button_cycles_and_rewrites);
    RUN_TEST(test_app_catches_up);
    RUN_TEST(test_saved_view_applies_to_the_first_payload);
    RUN_TEST(test_no_data_keeps_the_accounts);
    RUN_TEST(test_back_to_one_account);
    RUN_TEST(test_other_account_kinds);
    RUN_TEST(test_account_list_change_drops_kept_numbers);
    return UNITY_END();
}
