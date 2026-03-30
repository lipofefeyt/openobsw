#include "unity.h"
#include "obsw/pus/s5.h"
#include "obsw/fdir/fsm.h"
#include "obsw/tm/store.h"

void setUp(void) {}
void tearDown(void) {}

static obsw_tm_store_t store;
static obsw_fsm_ctx_t fsm;
static obsw_s5_ctx_t s5;

static void setup(void)
{
    obsw_tm_store_init(&store);

    obsw_fsm_config_t cfg = {0};
    obsw_fsm_init(&fsm, &cfg);

    s5.tm_store = &store;
    s5.apid = 0x010;
    s5.msg_counter = 0;
    s5.timestamp = 0;
    s5.fsm = (struct obsw_fsm_ctx *)&fsm;
    s5.safe_trigger_ids[0] = 0xDEAD;
    s5.safe_trigger_ids[1] = 0x00FF;
    s5.safe_trigger_count = 2;
}

void test_high_matching_id_triggers_safe(void)
{
    setup();
    obsw_s5_report(&s5, OBSW_S5_HIGH, 0xDEAD, NULL, 0);
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
}

void test_high_non_matching_id_no_safe(void)
{
    setup();
    obsw_s5_report(&s5, OBSW_S5_HIGH, 0x1234, NULL, 0);
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
}

void test_low_matching_id_no_safe(void)
{
    setup();
    /* Even if event ID matches, severity < HIGH should not trigger */
    obsw_s5_report(&s5, OBSW_S5_LOW, 0xDEAD, NULL, 0);
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
}

void test_no_fsm_pointer_no_crash(void)
{
    setup();
    s5.fsm = NULL;
    /* Must not crash even with matching HIGH event */
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK,
                          obsw_s5_report(&s5, OBSW_S5_HIGH, 0xDEAD, NULL, 0));
}

void test_second_safe_trigger_id_works(void)
{
    setup();
    obsw_s5_report(&s5, OBSW_S5_HIGH, 0x00FF, NULL, 0);
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_high_matching_id_triggers_safe);
    RUN_TEST(test_high_non_matching_id_no_safe);
    RUN_TEST(test_low_matching_id_no_safe);
    RUN_TEST(test_no_fsm_pointer_no_crash);
    RUN_TEST(test_second_safe_trigger_id_works);
    return UNITY_END();
}