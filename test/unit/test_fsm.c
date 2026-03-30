#include "unity.h"
#include "obsw/fdir/fsm.h"
#include "obsw/tm/store.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Hook tracking                                                       */
/* ------------------------------------------------------------------ */

static int enter_safe_calls;
static int exit_safe_calls;

static void on_enter_safe(void *ctx)
{
    (void)ctx;
    enter_safe_calls++;
}
static void on_exit_safe(void *ctx)
{
    (void)ctx;
    exit_safe_calls++;
}

/* ------------------------------------------------------------------ */
/* Whitelist                                                           */
/* ------------------------------------------------------------------ */

static const obsw_fsm_tc_entry_t whitelist[] = {
    {.service = 17, .subservice = 1}, /* S17 ping  */
    {.service = 1, .subservice = 1},  /* S1 accept */
};

/* ------------------------------------------------------------------ */
/* Setup helpers                                                       */
/* ------------------------------------------------------------------ */

static obsw_fsm_ctx_t make_fsm(void)
{
    enter_safe_calls = 0;
    exit_safe_calls = 0;
    obsw_fsm_ctx_t fsm;
    obsw_fsm_config_t cfg = {
        .on_enter_safe = on_enter_safe,
        .on_exit_safe = on_exit_safe,
        .hook_ctx = NULL,
        .safe_tc_whitelist = whitelist,
        .whitelist_len = 2,
    };
    obsw_fsm_init(&fsm, &cfg);
    return fsm;
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void test_init_starts_nominal(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    TEST_ASSERT_EQUAL_INT(OBSW_FSM_NOMINAL, obsw_fsm_mode(&fsm));
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
}

void test_init_null_returns_error(void)
{
    obsw_fsm_config_t cfg = {0};
    TEST_ASSERT_NOT_EQUAL(0, obsw_fsm_init(NULL, &cfg));
    obsw_fsm_ctx_t fsm;
    TEST_ASSERT_NOT_EQUAL(0, obsw_fsm_init(&fsm, NULL));
}

/* ------------------------------------------------------------------ */
/* Transitions                                                         */
/* ------------------------------------------------------------------ */

void test_to_safe_transitions_mode(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    obsw_fsm_to_safe(&fsm);
    TEST_ASSERT_EQUAL_INT(OBSW_FSM_SAFE, obsw_fsm_mode(&fsm));
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
}

void test_to_safe_fires_hook(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    obsw_fsm_to_safe(&fsm);
    TEST_ASSERT_EQUAL_INT(1, enter_safe_calls);
}

void test_to_safe_idempotent(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    obsw_fsm_to_safe(&fsm);
    obsw_fsm_to_safe(&fsm);
    TEST_ASSERT_EQUAL_INT(1, enter_safe_calls); /* hook fired only once */
    TEST_ASSERT_EQUAL_UINT32(1, fsm.safe_entry_count);
}

void test_safe_entry_count_increments(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();

    /* S1 context and store for recover TC */
    obsw_tm_store_t store;
    obsw_tm_store_init(&store);
    obsw_s1_ctx_t s1 = {.tm_store = &store, .apid = 0x010};
    obsw_fsm_recover_ctx_t r = {.fsm = &fsm, .s1 = &s1};
    obsw_tc_t tc = {.apid = 0x010, .service = 128, .subservice = 1, .seq_count = 1};

    obsw_fsm_to_safe(&fsm);
    obsw_fsm_recover(&tc, NULL, &r);
    obsw_fsm_to_safe(&fsm);
    TEST_ASSERT_EQUAL_UINT32(2, fsm.safe_entry_count);
}

/* ------------------------------------------------------------------ */
/* Recovery                                                            */
/* ------------------------------------------------------------------ */

void test_recover_returns_to_nominal(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    obsw_fsm_to_safe(&fsm);

    obsw_tm_store_t store;
    obsw_tm_store_init(&store);
    obsw_s1_ctx_t s1 = {.tm_store = &store, .apid = 0x010};
    obsw_fsm_recover_ctx_t r = {.fsm = &fsm, .s1 = &s1};
    obsw_tc_t tc = {.apid = 0x010, .service = 128, .subservice = 1, .seq_count = 1};

    TEST_ASSERT_EQUAL_INT(0, obsw_fsm_recover(&tc, NULL, &r));
    TEST_ASSERT_EQUAL_INT(OBSW_FSM_NOMINAL, obsw_fsm_mode(&fsm));
    TEST_ASSERT_EQUAL_INT(1, exit_safe_calls);
}

void test_recover_from_nominal_is_noop(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();

    obsw_tm_store_t store;
    obsw_tm_store_init(&store);
    obsw_s1_ctx_t s1 = {.tm_store = &store, .apid = 0x010};
    obsw_fsm_recover_ctx_t r = {.fsm = &fsm, .s1 = &s1};
    obsw_tc_t tc = {.apid = 0x010, .service = 128, .subservice = 1, .seq_count = 1};

    obsw_fsm_recover(&tc, NULL, &r);
    TEST_ASSERT_EQUAL_INT(OBSW_FSM_NOMINAL, obsw_fsm_mode(&fsm));
    TEST_ASSERT_EQUAL_INT(0, exit_safe_calls);
}

/* ------------------------------------------------------------------ */
/* TC whitelist                                                        */
/* ------------------------------------------------------------------ */

void test_nominal_allows_all_tc(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    TEST_ASSERT_TRUE(obsw_fsm_tc_allowed(&fsm, 3, 5));
    TEST_ASSERT_TRUE(obsw_fsm_tc_allowed(&fsm, 17, 1));
    TEST_ASSERT_TRUE(obsw_fsm_tc_allowed(&fsm, 99, 99));
}

void test_safe_allows_whitelisted_tc(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    obsw_fsm_to_safe(&fsm);
    TEST_ASSERT_TRUE(obsw_fsm_tc_allowed(&fsm, 17, 1));
    TEST_ASSERT_TRUE(obsw_fsm_tc_allowed(&fsm, 1, 1));
}

void test_safe_blocks_non_whitelisted_tc(void)
{
    obsw_fsm_ctx_t fsm = make_fsm();
    obsw_fsm_to_safe(&fsm);
    TEST_ASSERT_FALSE(obsw_fsm_tc_allowed(&fsm, 3, 5));
    TEST_ASSERT_FALSE(obsw_fsm_tc_allowed(&fsm, 3, 6));
    TEST_ASSERT_FALSE(obsw_fsm_tc_allowed(&fsm, 99, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_starts_nominal);
    RUN_TEST(test_init_null_returns_error);
    RUN_TEST(test_to_safe_transitions_mode);
    RUN_TEST(test_to_safe_fires_hook);
    RUN_TEST(test_to_safe_idempotent);
    RUN_TEST(test_safe_entry_count_increments);
    RUN_TEST(test_recover_returns_to_nominal);
    RUN_TEST(test_recover_from_nominal_is_noop);
    RUN_TEST(test_nominal_allows_all_tc);
    RUN_TEST(test_safe_allows_whitelisted_tc);
    RUN_TEST(test_safe_blocks_non_whitelisted_tc);
    return UNITY_END();
}