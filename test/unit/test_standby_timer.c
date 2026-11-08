/**
 * @file test_standby_timer.c
 * @brief Unit tests for the LEOP STANDBY → SAFE autonomy timer.
 */

#include "obsw/fdir/standby_timer.h"
#include "unity.h"

#include <stddef.h>

static obsw_standby_timer_t t;

void setUp(void)
{
    obsw_standby_timer_reset(&t);
}
void tearDown(void)
{
}

/* Tick @p n times in STANDBY without contact; return the tick that expired (1-based) or 0. */
static uint32_t run_standby(uint32_t n, uint32_t timeout_s)
{
    for (uint32_t i = 1U; i <= n; i++) {
        if (obsw_standby_timer_tick(&t, OBSW_FSM_STANDBY, timeout_s, false)) {
            return i;
        }
    }
    return 0U;
}

/* @verifies OBSW-SRS-187 */
void test_expires_after_timeout_without_contact(void)
{
    TEST_ASSERT_EQUAL_UINT32(300U, run_standby(1000U, 300U));
}

/* @verifies OBSW-SRS-187 */
void test_default_timeout_is_300_s(void)
{
    TEST_ASSERT_EQUAL_UINT32(300U, OBSW_STANDBY_SAFE_TIMEOUT_S);
}

/* @verifies OBSW-SRS-187 */
void test_zero_timeout_expires_on_first_tick(void)
{
    TEST_ASSERT_TRUE(obsw_standby_timer_tick(&t, OBSW_FSM_STANDBY, 0U, false));
}

/* @verifies OBSW-SRS-187 */
void test_keeps_reporting_expiry_until_mode_leaves_standby(void)
{
    TEST_ASSERT_EQUAL_UINT32(3U, run_standby(10U, 3U));
    /* A caller whose SAFE command did not take effect is told again */
    TEST_ASSERT_TRUE(obsw_standby_timer_tick(&t, OBSW_FSM_STANDBY, 3U, false));
}

/* @verifies OBSW-SRS-188 */
void test_ground_contact_restarts_countdown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, run_standby(250U, 300U));
    TEST_ASSERT_FALSE(obsw_standby_timer_tick(&t, OBSW_FSM_STANDBY, 300U, true));
    TEST_ASSERT_EQUAL_UINT32(0U, t.elapsed_s);
    /* A full timeout is needed again after contact */
    TEST_ASSERT_EQUAL_UINT32(300U, run_standby(1000U, 300U));
}

/* @verifies OBSW-SRS-188 */
void test_ground_contact_on_expiry_tick_prevents_safe(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, run_standby(9U, 10U));
    TEST_ASSERT_FALSE(obsw_standby_timer_tick(&t, OBSW_FSM_STANDBY, 10U, true));
}

/* @verifies OBSW-SRS-188 */
void test_leaving_standby_resets_countdown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, run_standby(200U, 300U));
    TEST_ASSERT_FALSE(obsw_standby_timer_tick(&t, OBSW_FSM_SAFE, 300U, false));
    TEST_ASSERT_FALSE(obsw_standby_timer_tick(&t, OBSW_FSM_NOMINAL, 300U, false));
    TEST_ASSERT_EQUAL_UINT32(0U, t.elapsed_s);
    /* Re-entering STANDBY starts a full countdown */
    TEST_ASSERT_EQUAL_UINT32(300U, run_standby(1000U, 300U));
}

/* @verifies OBSW-SRS-187 */
void test_timeout_change_applies_immediately(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, run_standby(50U, 300U));
    /* Shortened below the elapsed time: expires on the next tick */
    TEST_ASSERT_TRUE(obsw_standby_timer_tick(&t, OBSW_FSM_STANDBY, 30U, false));
}

/* @verifies OBSW-SRS-187 */
void test_elapsed_saturates(void)
{
    t.elapsed_s = UINT32_MAX;
    TEST_ASSERT_TRUE(obsw_standby_timer_tick(&t, OBSW_FSM_STANDBY, UINT32_MAX, false));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, t.elapsed_s);
}

/* @verifies OBSW-SRS-187 */
void test_null_timer_is_ignored(void)
{
    obsw_standby_timer_reset(NULL);
    TEST_ASSERT_FALSE(obsw_standby_timer_tick(NULL, OBSW_FSM_STANDBY, 0U, false));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_expires_after_timeout_without_contact);
    RUN_TEST(test_default_timeout_is_300_s);
    RUN_TEST(test_zero_timeout_expires_on_first_tick);
    RUN_TEST(test_keeps_reporting_expiry_until_mode_leaves_standby);
    RUN_TEST(test_ground_contact_restarts_countdown);
    RUN_TEST(test_ground_contact_on_expiry_tick_prevents_safe);
    RUN_TEST(test_leaving_standby_resets_countdown);
    RUN_TEST(test_timeout_change_applies_immediately);
    RUN_TEST(test_elapsed_saturates);
    RUN_TEST(test_null_timer_is_ignored);
    return UNITY_END();
}
