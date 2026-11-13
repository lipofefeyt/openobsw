#include "unity.h"
#include "obsw/pus/s5.h"
#include "obsw/fdir/fsm.h"
#include "obsw/srdb_generated.h"
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

/* @verifies OBSW-SRS-111 */
void test_high_matching_id_triggers_safe(void)
{
    setup();
    obsw_s5_report(&s5, OBSW_S5_HIGH, 0xDEAD, NULL, 0);
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
}

/* @verifies OBSW-SRS-111 */
void test_high_non_matching_id_no_safe(void)
{
    setup();
    obsw_s5_report(&s5, OBSW_S5_HIGH, 0x1234, NULL, 0);
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
}

/* @verifies OBSW-SRS-111 */
void test_low_matching_id_no_safe(void)
{
    setup();
    /* Even if event ID matches, severity < HIGH should not trigger */
    obsw_s5_report(&s5, OBSW_S5_LOW, 0xDEAD, NULL, 0);
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
}

/* @verifies OBSW-SRS-111 */
void test_no_fsm_pointer_no_crash(void)
{
    setup();
    s5.fsm = NULL;
    /* Must not crash even with matching HIGH event */
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK,
                          obsw_s5_report(&s5, OBSW_S5_HIGH, 0xDEAD, NULL, 0));
}

/* @verifies OBSW-SRS-111 */
void test_second_safe_trigger_id_works(void)
{
    setup();
    obsw_s5_report(&s5, OBSW_S5_HIGH, 0x00FF, NULL, 0);
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
}

/* @verifies OBSW-SRS-111 */
void test_set_safe_triggers_replaces_the_list(void)
{
    setup();
    static const uint16_t ids[] = {0x0A01, 0x0A02, 0x0A03};
    TEST_ASSERT_TRUE(obsw_s5_set_safe_triggers(&s5, ids, 3));
    TEST_ASSERT_EQUAL_UINT8(3, s5.safe_trigger_count);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(ids, s5.safe_trigger_ids, 3);

    obsw_s5_report(&s5, OBSW_S5_HIGH, 0xDEAD, NULL, 0); /* was in the old list */
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
    obsw_s5_report(&s5, OBSW_S5_HIGH, 0x0A03, NULL, 0);
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));

    TEST_ASSERT_TRUE(obsw_s5_set_safe_triggers(&s5, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(0, s5.safe_trigger_count);
}

/* @verifies OBSW-SRS-111 */
void test_set_safe_triggers_never_writes_past_the_list(void)
{
    setup();
    uint16_t ids[OBSW_S5_MAX_SAFE_TRIGGERS + 1U];
    for (uint8_t i = 0; i < OBSW_S5_MAX_SAFE_TRIGGERS + 1U; i++)
        ids[i] = (uint16_t)(0x0B00U + i);

    TEST_ASSERT_FALSE(obsw_s5_set_safe_triggers(&s5, ids, OBSW_S5_MAX_SAFE_TRIGGERS + 1U));
    TEST_ASSERT_EQUAL_UINT8(OBSW_S5_MAX_SAFE_TRIGGERS, s5.safe_trigger_count);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(ids, s5.safe_trigger_ids, OBSW_S5_MAX_SAFE_TRIGGERS);

    TEST_ASSERT_FALSE(obsw_s5_set_safe_triggers(&s5, NULL, 1));
    TEST_ASSERT_FALSE(obsw_s5_set_safe_triggers(NULL, ids, 1));
    TEST_ASSERT_EQUAL_UINT8(OBSW_S5_MAX_SAFE_TRIGGERS, s5.safe_trigger_count); /* untouched */
}

/* The list every target loads is the generated one: each SRDB safe trigger,
 * watchdog_expiry included, commands SAFE.
 * @verifies OBSW-SRS-111 */
void test_generated_srdb_list_commands_safe_for_every_safe_trigger(void)
{
    static const uint16_t ids[] = SRDB_SAFE_TRIGGER_IDS;
    TEST_ASSERT_EQUAL_size_t(SRDB_SAFE_TRIGGER_COUNT, sizeof(ids) / sizeof(ids[0]));

    bool has_watchdog = false;
    for (uint8_t i = 0; i < SRDB_SAFE_TRIGGER_COUNT; i++) {
        setup();
        TEST_ASSERT_TRUE(obsw_s5_set_safe_triggers(&s5, ids, SRDB_SAFE_TRIGGER_COUNT));
        TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
        obsw_s5_report(&s5, OBSW_S5_HIGH, ids[i], NULL, 0);
        TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
        has_watchdog = has_watchdog || (ids[i] == SRDB_EVENT_WATCHDOG_EXPIRY);
    }
    TEST_ASSERT_TRUE(has_watchdog);

    setup();
    TEST_ASSERT_TRUE(obsw_s5_set_safe_triggers(&s5, ids, SRDB_SAFE_TRIGGER_COUNT));
    obsw_s5_report(&s5, OBSW_S5_HIGH, SRDB_EVENT_SAFE_MODE_ENTRY, NULL, 0); /* HIGH, not a trigger */
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_high_matching_id_triggers_safe);
    RUN_TEST(test_high_non_matching_id_no_safe);
    RUN_TEST(test_low_matching_id_no_safe);
    RUN_TEST(test_no_fsm_pointer_no_crash);
    RUN_TEST(test_second_safe_trigger_id_works);
    RUN_TEST(test_set_safe_triggers_replaces_the_list);
    RUN_TEST(test_set_safe_triggers_never_writes_past_the_list);
    RUN_TEST(test_generated_srdb_list_commands_safe_for_every_safe_trigger);
    return UNITY_END();
}
