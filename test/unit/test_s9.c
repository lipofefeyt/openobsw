/**
 * @file test_s9.c
 * @brief Unit tests for PUS-C Service 9 - Time Management.
 *
 * Tests TC(9,128) set on-board time against a caller-owned obsw_obt_t.
 */

#include "obsw/ccsds/space_packet.h"
#include "obsw/pus/s9.h"
#include "obsw/tm/store.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s9_ctx_t s9;
static obsw_obt_t obt;

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);

    s1.tm_store    = &store;
    s1.apid        = 0x010;
    s1.msg_counter = 0;
    s1.timestamp   = 0;

    obt.coarse = 0U;
    obt.fine   = 0U;

    s9.s1  = &s1;
    s9.obt = &obt;
}

/* Build TC(9,128) set OBT: coarse(4 BE) | fine(2 BE) */
static obsw_tc_t make_set_obt_tc(uint32_t coarse, uint16_t fine, uint8_t *ud)
{
    ud[0]        = (uint8_t)(coarse >> 24);
    ud[1]        = (uint8_t)(coarse >> 16);
    ud[2]        = (uint8_t)(coarse >> 8);
    ud[3]        = (uint8_t)(coarse & 0xFFU);
    ud[4]        = (uint8_t)(fine >> 8);
    ud[5]        = (uint8_t)(fine & 0xFFU);
    obsw_tc_t tc = {
        .apid          = 0x010,
        .service       = 9,
        .subservice    = 128,
        .seq_count     = 1,
        .user_data     = ud,
        .user_data_len = 6,
    };
    return tc;
}

/* Dequeue one packet and parse it */
static obsw_sp_packet_t dequeue_pkt(uint8_t *buf, size_t buf_len)
{
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, (uint16_t)buf_len, &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

/* ------------------------------------------------------------------ */
/* TC(9,128) - set OBT                                                 */
/* ------------------------------------------------------------------ */

void test_set_obt_returns_ok(void)
{
    setup_ctx();
    uint8_t ud[6];
    obsw_tc_t tc = make_set_obt_tc(1000U, 0x8000U, ud);
    TEST_ASSERT_EQUAL_INT(0, obsw_s9_set_obt(&tc, NULL, &s9));
    TEST_ASSERT_EQUAL_UINT32(1000U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(0x8000U, obt.fine);
}

void test_set_obt_overwrites_prior_value(void)
{
    setup_ctx();
    obt.coarse = 42U;
    obt.fine   = 7U;

    uint8_t ud[6];
    obsw_tc_t tc = make_set_obt_tc(500000U, 0U, ud);
    obsw_s9_set_obt(&tc, NULL, &s9);

    TEST_ASSERT_EQUAL_UINT32(500000U, obt.coarse);
    TEST_ASSERT_EQUAL_UINT16(0U, obt.fine);
}

void test_set_obt_emits_tm11_and_tm17(void)
{
    setup_ctx();
    uint8_t ud[6];
    obsw_tc_t tc = make_set_obt_tc(100U, 0U, ud);
    obsw_s9_set_obt(&tc, NULL, &s9);

    /* TM(1,1) + TM(1,7) = 2 packets; no TM(9,x) response is defined */
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));

    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]); /* svc=1 */
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]); /* subsvc=1 - accept */

    pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]); /* svc=1 */
    TEST_ASSERT_EQUAL_UINT8(7, pkt.payload[2]); /* subsvc=7 - complete */
}

void test_set_obt_truncated_ud_emits_tm12(void)
{
    setup_ctx();
    uint8_t ud[3] = {0x00, 0x00, 0x00};
    obsw_tc_t tc  = {
         .apid          = 0x010,
         .service       = 9,
         .subservice    = 128,
         .seq_count     = 1,
         .user_data     = ud,
         .user_data_len = 3,
    };
    TEST_ASSERT_NOT_EQUAL(0, obsw_s9_set_obt(&tc, NULL, &s9));

    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]); /* TM(1,2) - accept fail */

    /* OBT must be left untouched by a rejected TC */
    TEST_ASSERT_EQUAL_UINT32(0U, obt.coarse);
}

void test_set_obt_null_user_data_emits_tm12(void)
{
    setup_ctx();
    obsw_tc_t tc = {
        .apid          = 0x010,
        .service       = 9,
        .subservice    = 128,
        .seq_count     = 1,
        .user_data     = NULL,
        .user_data_len = 0,
    };
    TEST_ASSERT_NOT_EQUAL(0, obsw_s9_set_obt(&tc, NULL, &s9));
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
}

void test_set_obt_null_ctx_returns_error(void)
{
    setup_ctx();
    uint8_t ud[6];
    obsw_tc_t tc = make_set_obt_tc(1U, 0U, ud);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s9_set_obt(&tc, NULL, NULL));
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_set_obt_returns_ok);
    RUN_TEST(test_set_obt_overwrites_prior_value);
    RUN_TEST(test_set_obt_emits_tm11_and_tm17);
    RUN_TEST(test_set_obt_truncated_ud_emits_tm12);
    RUN_TEST(test_set_obt_null_user_data_emits_tm12);
    RUN_TEST(test_set_obt_null_ctx_returns_error);

    return UNITY_END();
}
