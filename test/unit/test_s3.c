#include "unity.h"
#include "obsw/pus/s3.h"
#include "obsw/tm/store.h"
#include "obsw/ccsds/space_packet.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Test fixtures                                                        */
/* ------------------------------------------------------------------ */

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s3_ctx_t s3;

/* Live application variables sampled by HK set */
static uint8_t app_u8 = 0xAB;
static uint16_t app_u16 = 0x1234;
static uint32_t app_u32 = 0xDEADBEEF;

static obsw_s3_param_t params[] = {
    {.ptr = &app_u8, .size = OBSW_S3_PARAM_U8},
    {.ptr = &app_u16, .size = OBSW_S3_PARAM_U16},
    {.ptr = &app_u32, .size = OBSW_S3_PARAM_U32},
};

static obsw_s3_set_t sets[] = {
    {
        .set_id = 1,
        .params = params,
        .param_count = 3,
        .interval_ticks = 0,
        .countdown = 0,
        .enabled = false,
    },
};

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);

    s1.tm_store = &store;
    s1.apid = 0x010;
    s1.msg_counter = 0;
    s1.timestamp = 0;

    s3.tm_store = &store;
    s3.s1 = &s1;
    s3.apid = 0x010;
    s3.msg_counter = 0;
    s3.timestamp = 0;
    s3.sets = sets;
    s3.set_count = 1;

    /* Reset set state */
    sets[0].enabled = false;
    sets[0].interval_ticks = 0;
    sets[0].countdown = 0;
}

/* Build a TC with user_data for TC(3,5) enable: set_id + interval */
static obsw_tc_t make_enable_tc(uint8_t set_id, uint32_t interval,
                                uint8_t *buf)
{
    buf[0] = set_id;
    buf[1] = (uint8_t)(interval >> 24);
    buf[2] = (uint8_t)(interval >> 16);
    buf[3] = (uint8_t)(interval >> 8);
    buf[4] = (uint8_t)(interval & 0xFFU);
    obsw_tc_t tc = {
        .apid = 0x010,
        .service = 3,
        .subservice = 5,
        .seq_count = 1,
        .user_data = buf,
        .user_data_len = 5,
    };
    return tc;
}

static obsw_tc_t make_disable_tc(uint8_t set_id, uint8_t *buf)
{
    buf[0] = set_id;
    obsw_tc_t tc = {
        .apid = 0x010,
        .service = 3,
        .subservice = 6,
        .seq_count = 2,
        .user_data = buf,
        .user_data_len = 1,
    };
    return tc;
}

static obsw_sp_packet_t dequeue_pkt(void)
{
    uint8_t buf[256];
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

/* ------------------------------------------------------------------ */
/* enable / disable TC handler tests                                   */
/* ------------------------------------------------------------------ */

void test_enable_sets_interval_and_enabled(void)
{
    setup_ctx();
    uint8_t buf[5];
    obsw_tc_t tc = make_enable_tc(1, 10, buf);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_enable(&tc, NULL, &s3));
    TEST_ASSERT_TRUE(sets[0].enabled);
    TEST_ASSERT_EQUAL_UINT32(10, sets[0].interval_ticks);
}

void test_enable_emits_tm11_and_tm17(void)
{
    setup_ctx();
    uint8_t buf[5];
    obsw_tc_t tc = make_enable_tc(1, 5, buf);
    obsw_s3_enable(&tc, NULL, &s3);
    /* TM(1,1) accept + TM(1,7) complete */
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
    obsw_sp_packet_t pkt = dequeue_pkt();
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]);
}

void test_enable_unknown_set_emits_failure(void)
{
    setup_ctx();
    uint8_t buf[5];
    obsw_tc_t tc = make_enable_tc(99, 5, buf); /* set_id 99 doesn't exist */
    TEST_ASSERT_NOT_EQUAL(0, obsw_s3_enable(&tc, NULL, &s3));
    obsw_sp_packet_t pkt = dequeue_pkt();
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]); /* TM(1,2) failure */
}

void test_disable_clears_enabled(void)
{
    setup_ctx();
    sets[0].enabled = true;
    uint8_t buf[1];
    obsw_tc_t tc = make_disable_tc(1, buf);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_disable(&tc, NULL, &s3));
    TEST_ASSERT_FALSE(sets[0].enabled);
}

/* ------------------------------------------------------------------ */
/* Tick tests                                                          */
/* ------------------------------------------------------------------ */

void test_tick_does_not_emit_when_disabled(void)
{
    setup_ctx();
    sets[0].enabled = false;
    sets[0].interval_ticks = 2;
    sets[0].countdown = 2;
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store));
}

void test_tick_emits_on_countdown_expiry(void)
{
    setup_ctx();
    sets[0].enabled = true;
    sets[0].interval_ticks = 3;
    sets[0].countdown = 3;
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store));
    obsw_s3_tick(&s3); /* countdown hits 0 */
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
}

void test_tick_reloads_countdown(void)
{
    setup_ctx();
    sets[0].enabled = true;
    sets[0].interval_ticks = 2;
    sets[0].countdown = 2;
    /* First period */
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
    /* Second period — countdown reloaded */
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
}

void test_tick_report_is_tm3_25(void)
{
    setup_ctx();
    sets[0].enabled = true;
    sets[0].interval_ticks = 1;
    sets[0].countdown = 1;
    obsw_s3_tick(&s3);
    obsw_sp_packet_t pkt = dequeue_pkt();
    TEST_ASSERT_EQUAL_UINT8(3, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(25, pkt.payload[2]);
}

/* ------------------------------------------------------------------ */
/* On-demand report                                                    */
/* ------------------------------------------------------------------ */

void test_on_demand_report_emits_tm3_25(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK, obsw_s3_report(&s3, 1));
    obsw_sp_packet_t pkt = dequeue_pkt();
    TEST_ASSERT_EQUAL_UINT8(3, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(25, pkt.payload[2]);
}

void test_on_demand_unknown_set_returns_error(void)
{
    setup_ctx();
    TEST_ASSERT_NOT_EQUAL(OBSW_PUS_TM_OK, obsw_s3_report(&s3, 99));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enable_sets_interval_and_enabled);
    RUN_TEST(test_enable_emits_tm11_and_tm17);
    RUN_TEST(test_enable_unknown_set_emits_failure);
    RUN_TEST(test_disable_clears_enabled);
    RUN_TEST(test_tick_does_not_emit_when_disabled);
    RUN_TEST(test_tick_emits_on_countdown_expiry);
    RUN_TEST(test_tick_reloads_countdown);
    RUN_TEST(test_tick_report_is_tm3_25);
    RUN_TEST(test_on_demand_report_emits_tm3_25);
    RUN_TEST(test_on_demand_unknown_set_returns_error);
    return UNITY_END();
}