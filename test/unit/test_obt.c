/**
 * @file test_obt.c
 * @brief Unit tests for the on-board time (OBT) module.
 */

#include "obsw/time/obt.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

/* @verifies OBSW-SRS-150, OBSW-SRS-151 */
void test_advance_whole_second(void)
{
    obsw_obt_t obt = {.coarse = 10U, .fine = 0U};
    obsw_obt_advance(&obt, 1000U);
    TEST_ASSERT_EQUAL_UINT32(11U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(0U, obt.fine);
}

/* @verifies OBSW-SRS-150, OBSW-SRS-151 */
void test_advance_sub_second_accumulates_fine(void)
{
    obsw_obt_t obt = {.coarse = 0U, .fine = 0U};
    obsw_obt_advance(&obt, 100U); /* +0.1 s = 6553.6 fine units, truncated to 6553 */
    TEST_ASSERT_EQUAL_UINT32(0U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(6553U, obt.fine);
}

/* @verifies OBSW-SRS-150, OBSW-SRS-151 */
void test_advance_fine_overflow_carries_into_coarse(void)
{
    obsw_obt_t obt = {.coarse = 5U, .fine = 65000U};
    obsw_obt_advance(&obt, 100U); /* 65000 + 6553 = 71553 -> carry 1, remainder 6017 */
    TEST_ASSERT_EQUAL_UINT32(6U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(6017U, obt.fine);
}

/* @verifies OBSW-SRS-151 */
void test_advance_repeated_ticks_matches_single_advance(void)
{
    /* 250 ms is chosen because 250 * 65536 / 1000 = 16384 exactly - no
     * per-step truncation, so four 250 ms ticks must equal one 1000 ms
     * tick bit-for-bit. Uneven ticks are covered by
     * test_advance_uneven_ticks_do_not_drift. */
    obsw_obt_t stepped = {.coarse = 0U, .fine = 0U};
    for (int i = 0; i < 4; i++)
        obsw_obt_advance(&stepped, 250U);

    obsw_obt_t direct = {.coarse = 0U, .fine = 0U};
    obsw_obt_advance(&direct, 1000U);

    TEST_ASSERT_EQUAL_UINT32(direct.coarse, stepped.coarse);
    TEST_ASSERT_EQUAL_UINT16(direct.fine, stepped.fine);
}

/* @verifies OBSW-SRS-151 */
void test_advance_uneven_ticks_do_not_drift(void)
{
    /* 100 ms = 6553.6 fine units: the remainder must carry, so ten ticks
     * land on exactly one second and a day of ticks on exactly 86400 s. */
    obsw_obt_t obt = {.coarse = 0U, .fine = 0U};
    for (int i = 0; i < 10; i++)
        obsw_obt_advance(&obt, 100U);
    TEST_ASSERT_EQUAL_UINT32(1U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(0U, obt.fine);

    obsw_obt_set(&obt, 0U, 0U);
    for (long i = 0; i < 864000L; i++)
        obsw_obt_advance(&obt, 100U);
    TEST_ASSERT_EQUAL_UINT32(86400U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(0U, obt.fine);
}

/* @verifies OBSW-SRS-151, OBSW-SRS-152 */
void test_set_clears_remainder(void)
{
    obsw_obt_t obt = {.coarse = 0U, .fine = 0U};
    obsw_obt_advance(&obt, 100U); /* leaves rem = 600 */
    obsw_obt_set(&obt, 5U, 0U);
    for (int i = 0; i < 10; i++)
        obsw_obt_advance(&obt, 100U);
    TEST_ASSERT_EQUAL_UINT32(6U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(0U, obt.fine);
}

/* @verifies OBSW-SRS-150, OBSW-SRS-152 */
void test_set_overwrites_coarse_and_fine(void)
{
    obsw_obt_t obt = {.coarse = 100U, .fine = 200U};
    obsw_obt_set(&obt, 999999U, 42U);
    TEST_ASSERT_EQUAL_UINT32(999999U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(42U, obt.fine);
}

/* @verifies OBSW-SRS-150 */
void test_seconds_truncates_fine(void)
{
    obsw_obt_t obt = {.coarse = 77U, .fine = 60000U};
    TEST_ASSERT_EQUAL_UINT32(77U, obsw_obt_seconds(&obt));
}

/* @verifies OBSW-SRS-150 */
void test_null_pointers_are_safe(void)
{
    obsw_obt_advance(NULL, 100U);
    obsw_obt_set(NULL, 1U, 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, obsw_obt_seconds(NULL));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_advance_whole_second);
    RUN_TEST(test_advance_sub_second_accumulates_fine);
    RUN_TEST(test_advance_fine_overflow_carries_into_coarse);
    RUN_TEST(test_advance_repeated_ticks_matches_single_advance);
    RUN_TEST(test_advance_uneven_ticks_do_not_drift);
    RUN_TEST(test_set_clears_remainder);
    RUN_TEST(test_set_overwrites_coarse_and_fine);
    RUN_TEST(test_seconds_truncates_fine);
    RUN_TEST(test_null_pointers_are_safe);

    return UNITY_END();
}
