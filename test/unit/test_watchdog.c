#include "obsw/fdir/fsm.h"
#include "obsw/fdir/watchdog.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

static int expire_count;
static void on_expire(void *ctx)
{
    (void)ctx;
    expire_count++;
}

static obsw_wd_ctx_t make_wd(uint32_t timeout)
{
    expire_count = 0;
    obsw_wd_ctx_t wd;
    obsw_wd_init(&wd, timeout, on_expire, NULL);
    return wd;
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void test_init_ok(void)
{
    obsw_wd_ctx_t wd;
    TEST_ASSERT_EQUAL_INT(OBSW_WD_OK, obsw_wd_init(&wd, 5, on_expire, NULL));
    TEST_ASSERT_FALSE(obsw_wd_expired(&wd));
}

void test_init_zero_timeout_fails(void)
{
    obsw_wd_ctx_t wd;
    TEST_ASSERT_EQUAL_INT(OBSW_WD_ERR_PARAM, obsw_wd_init(&wd, 0, on_expire, NULL));
}

void test_init_null_cb_fails(void)
{
    obsw_wd_ctx_t wd;
    TEST_ASSERT_EQUAL_INT(OBSW_WD_ERR_NULL, obsw_wd_init(&wd, 5, NULL, NULL));
}

/* ------------------------------------------------------------------ */
/* Expiry                                                              */
/* ------------------------------------------------------------------ */

void test_expires_after_timeout_ticks(void)
{
    obsw_wd_ctx_t wd = make_wd(3);
    obsw_wd_tick(&wd);
    obsw_wd_tick(&wd);
    TEST_ASSERT_FALSE(obsw_wd_expired(&wd));
    obsw_wd_tick(&wd);
    TEST_ASSERT_TRUE(obsw_wd_expired(&wd));
    TEST_ASSERT_EQUAL_INT(1, expire_count);
}

void test_callback_fires_only_once(void)
{
    obsw_wd_ctx_t wd = make_wd(2);
    obsw_wd_tick(&wd);
    obsw_wd_tick(&wd); /* expires here */
    obsw_wd_tick(&wd); /* already expired — no second call */
    TEST_ASSERT_EQUAL_INT(1, expire_count);
}

/* ------------------------------------------------------------------ */
/* Kick                                                                */
/* ------------------------------------------------------------------ */

void test_kick_prevents_expiry(void)
{
    obsw_wd_ctx_t wd = make_wd(3);
    obsw_wd_tick(&wd);
    obsw_wd_kick(&wd);
    obsw_wd_tick(&wd); /* countdown reloaded — no expiry */
    obsw_wd_kick(&wd);
    obsw_wd_tick(&wd);
    TEST_ASSERT_FALSE(obsw_wd_expired(&wd));
    TEST_ASSERT_EQUAL_INT(0, expire_count);
}

void test_kick_reloads_countdown(void)
{
    obsw_wd_ctx_t wd = make_wd(3);
    obsw_wd_tick(&wd);
    obsw_wd_tick(&wd); /* countdown = 1, one tick from expiry */
    obsw_wd_kick(&wd);
    obsw_wd_tick(&wd); /* reloaded to 3, decrements to 2 */
    obsw_wd_tick(&wd);
    TEST_ASSERT_FALSE(obsw_wd_expired(&wd));
}

void test_late_kick_still_expires(void)
{
    obsw_wd_ctx_t wd = make_wd(2);
    obsw_wd_tick(&wd);
    obsw_wd_tick(&wd); /* expires */
    obsw_wd_kick(&wd); /* too late */
    TEST_ASSERT_TRUE(obsw_wd_expired(&wd));
}

/* ------------------------------------------------------------------ */
/* FSM integration                                                     */
/* ------------------------------------------------------------------ */

void test_watchdog_triggers_fsm_safe(void)
{
    obsw_fsm_ctx_t fsm;
    obsw_fsm_config_t cfg = {
        .on_enter_safe     = NULL,
        .on_exit_safe      = NULL,
        .hook_ctx          = NULL,
        .safe_tc_whitelist = NULL,
        .whitelist_len     = 0,
    };
    obsw_fsm_init(&fsm, &cfg);

    obsw_wd_ctx_t wd;
    obsw_wd_init(&wd, 2, (obsw_wd_expire_cb_t)obsw_fsm_to_safe, &fsm);

    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
    obsw_wd_tick(&wd);
    obsw_wd_tick(&wd);
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_ok);
    RUN_TEST(test_init_zero_timeout_fails);
    RUN_TEST(test_init_null_cb_fails);
    RUN_TEST(test_expires_after_timeout_ticks);
    RUN_TEST(test_callback_fires_only_once);
    RUN_TEST(test_kick_prevents_expiry);
    RUN_TEST(test_kick_reloads_countdown);
    RUN_TEST(test_late_kick_still_expires);
    RUN_TEST(test_watchdog_triggers_fsm_safe);
    return UNITY_END();
}