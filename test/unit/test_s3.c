#include "obsw/ccsds/space_packet.h"
#include "obsw/pus/s3.h"
#include "obsw/tm/store.h"
#include "unity.h"

#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s3_ctx_t s3;

static uint8_t app_u8   = 0xAB;
static uint16_t app_u16 = 0x1234;
static uint32_t app_u32 = 0xDEADBEEF;

static obsw_s3_param_t params[] = {
    {.ptr = &app_u8, .size = OBSW_S3_PARAM_U8},
    {.ptr = &app_u16, .size = OBSW_S3_PARAM_U16},
    {.ptr = &app_u32, .size = OBSW_S3_PARAM_U32},
};

static obsw_s3_set_t sets[4] = {
    {
        .set_id         = 1,
        .params         = params,
        .param_count    = 3,
        .interval_ticks = 0,
        .countdown      = 0,
        .enabled        = false,
    },
};

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);
    s1.tm_store            = &store;
    s1.apid                = 0x010;
    s1.msg_counter         = 0;
    s1.timestamp           = 0;
    s3.tm_store            = &store;
    s3.s1                  = &s1;
    s3.apid                = 0x010;
    s3.msg_counter         = 0;
    s3.timestamp           = 0;
    s3.sets                = sets;
    s3.set_count           = 1;
    s3.set_capacity        = 0; /* TC(3,1) disabled unless setup_define() */
    s3.param_pool          = NULL;
    s3.param_pool_len      = 0;
    s3.param_pool_used     = 0;
    s3.param_table         = NULL;
    s3.param_table_len     = 0;
    sets[0].enabled        = false;
    sets[0].interval_ticks = 0;
    sets[0].countdown      = 0;
}

/* buf must be caller-owned */
static obsw_sp_packet_t dequeue_pkt(uint8_t *buf, size_t buf_len)
{
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, buf_len, &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

static obsw_tc_t make_enable_tc(uint8_t set_id, uint32_t interval, uint8_t *buf)
{
    buf[0]       = set_id;
    buf[1]       = (uint8_t)(interval >> 24);
    buf[2]       = (uint8_t)(interval >> 16);
    buf[3]       = (uint8_t)(interval >> 8);
    buf[4]       = (uint8_t)(interval & 0xFFU);
    obsw_tc_t tc = {
        .apid          = 0x010,
        .service       = 3,
        .subservice    = 5,
        .seq_count     = 1,
        .user_data     = buf,
        .user_data_len = 5,
    };
    return tc;
}

static obsw_tc_t make_disable_tc(uint8_t set_id, uint8_t *buf)
{
    buf[0]       = set_id;
    obsw_tc_t tc = {
        .apid          = 0x010,
        .service       = 3,
        .subservice    = 6,
        .seq_count     = 2,
        .user_data     = buf,
        .user_data_len = 1,
    };
    return tc;
}

/* @verifies OBSW-SRS-091 */
void test_enable_sets_interval_and_enabled(void)
{
    setup_ctx();
    uint8_t buf[5];
    obsw_tc_t tc = make_enable_tc(1, 10, buf);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_enable(&tc, NULL, &s3));
    TEST_ASSERT_TRUE(sets[0].enabled);
    TEST_ASSERT_EQUAL_UINT32(10, sets[0].interval_ticks);
}

/* @verifies OBSW-SRS-091, OBSW-SRS-082 */
void test_enable_emits_tm11_and_tm17(void)
{
    setup_ctx();
    uint8_t buf[5];
    obsw_tc_t tc = make_enable_tc(1, 5, buf);
    obsw_s3_enable(&tc, NULL, &s3);
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]);
}

/* @verifies OBSW-SRS-091 */
void test_enable_unknown_set_emits_failure(void)
{
    setup_ctx();
    uint8_t buf[5];
    obsw_tc_t tc = make_enable_tc(99, 5, buf);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s3_enable(&tc, NULL, &s3));
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]);
}

/* @verifies OBSW-SRS-091, OBSW-SRS-083 */
void test_enable_truncated_emits_illegal_app_data(void)
{
    setup_ctx();
    uint8_t buf[5];
    obsw_tc_t tc = make_enable_tc(1, 5, buf);
    tc.user_data_len = 4;
    TEST_ASSERT_NOT_EQUAL(0, obsw_s3_enable(&tc, NULL, &s3));
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]);
    TEST_ASSERT_EQUAL_HEX16(OBSW_S1_FAIL_ILLEGAL_APP_DATA,
                            (uint16_t)((pkt.payload[15] << 8) | pkt.payload[16]));
    TEST_ASSERT_FALSE(sets[0].enabled);
}

/* @verifies OBSW-SRS-091 */
void test_disable_clears_enabled(void)
{
    setup_ctx();
    sets[0].enabled = true;
    uint8_t buf[1];
    obsw_tc_t tc = make_disable_tc(1, buf);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_disable(&tc, NULL, &s3));
    TEST_ASSERT_FALSE(sets[0].enabled);
}

/* @verifies OBSW-SRS-090 */
void test_tick_does_not_emit_when_disabled(void)
{
    setup_ctx();
    sets[0].enabled        = false;
    sets[0].interval_ticks = 2;
    sets[0].countdown      = 2;
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store));
}

/* @verifies OBSW-SRS-090 */
void test_tick_emits_on_countdown_expiry(void)
{
    setup_ctx();
    sets[0].enabled        = true;
    sets[0].interval_ticks = 3;
    sets[0].countdown      = 3;
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store));
    obsw_s3_tick(&s3);
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
}

/* @verifies OBSW-SRS-090 */
void test_tick_reloads_countdown(void)
{
    setup_ctx();
    sets[0].enabled        = true;
    sets[0].interval_ticks = 2;
    sets[0].countdown      = 2;
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
    obsw_s3_tick(&s3);
    obsw_s3_tick(&s3);
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
}

/* @verifies OBSW-SRS-090 */
void test_tick_report_is_tm3_25(void)
{
    setup_ctx();
    sets[0].enabled        = true;
    sets[0].interval_ticks = 1;
    sets[0].countdown      = 1;
    obsw_s3_tick(&s3);
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(3, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(25, pkt.payload[2]);
}

/* @verifies OBSW-SRS-095 */
void test_on_demand_report_emits_tm3_25(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK, obsw_s3_report(&s3, 1));
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(3, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(25, pkt.payload[2]);
}

/* TM(3,25) app data at payload[11]: set_id then parameters, big-endian (#82) */
/* @verifies OBSW-SRS-092 */
void test_report_params_are_big_endian(void)
{
    setup_ctx();
    obsw_s3_report(&s3, 1);
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    const uint8_t expected[] = {1, 0xAB, 0x12, 0x34, 0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_UINT16(11U + sizeof(expected), pkt.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, pkt.payload + 11, sizeof(expected));
}

/* @verifies OBSW-SRS-092 */
void test_report_float_is_big_endian_ieee754(void)
{
    static float f = 1.0f; /* 0x3F800000 */
    static obsw_s3_param_t fparams[] = {{.ptr = &f, .size = OBSW_S3_PARAM_U32}};
    static obsw_s3_set_t fset[] = {{.set_id = 7, .params = fparams, .param_count = 1}};
    setup_ctx();
    s3.sets = fset;
    obsw_s3_report(&s3, 7);
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    const uint8_t expected[] = {7, 0x3F, 0x80, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, pkt.payload + 11, sizeof(expected));
}

/* ---- TC(3,1) define ---------------------------------------------- */

static obsw_s20_param_t s20_table[] = {
    {.param_id = 0x2001, .value = {.u32 = 0x01020304}},
    {.param_id = 0x2002, .value = {.f32 = 1.0f}},
};
static obsw_s3_param_t pool[4];

static void setup_define(void)
{
    setup_ctx();
    s3.set_capacity    = 4;
    s3.param_pool      = pool;
    s3.param_pool_len  = 4;
    s3.param_table     = s20_table;
    s3.param_table_len = 2;
}

/* set_id(1) | interval(4 BE) | n(1) | param_id(2 BE) x n */
static obsw_tc_t make_define_tc(uint8_t *buf, uint8_t set_id, uint32_t interval,
                                const uint16_t *ids, uint8_t n)
{
    buf[0] = set_id;
    buf[1] = (uint8_t)(interval >> 24);
    buf[2] = (uint8_t)(interval >> 16);
    buf[3] = (uint8_t)(interval >> 8);
    buf[4] = (uint8_t)interval;
    buf[5] = n;
    for (uint8_t i = 0; i < n; i++) {
        buf[6 + 2 * i] = (uint8_t)(ids[i] >> 8);
        buf[7 + 2 * i] = (uint8_t)ids[i];
    }
    obsw_tc_t tc = {
        .apid          = 0x010,
        .service       = 3,
        .subservice    = 1,
        .seq_count     = 3,
        .user_data     = buf,
        .user_data_len = (uint16_t)(6 + 2 * n),
    };
    return tc;
}

/* Expect exactly one TM(1,2) with @p code and nothing else */
static void assert_define_rejected(const obsw_tc_t *tc, uint16_t code)
{
    TEST_ASSERT_NOT_EQUAL(0, obsw_s3_define(tc, NULL, &s3));
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]);
    TEST_ASSERT_EQUAL_HEX16(code, (uint16_t)((pkt.payload[15] << 8) | pkt.payload[16]));
    TEST_ASSERT_EQUAL_UINT8(1, s3.set_count);
    TEST_ASSERT_EQUAL_UINT16(0, s3.param_pool_used);
}

/* @verifies OBSW-SRS-093, OBSW-SRS-082 */
void test_define_creates_disabled_set(void)
{
    setup_define();
    uint8_t buf[16];
    const uint16_t ids[] = {0x2002, 0x2001};
    obsw_tc_t tc = make_define_tc(buf, 9, 3, ids, 2);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_define(&tc, NULL, &s3));
    TEST_ASSERT_EQUAL_UINT8(2, s3.set_count);
    TEST_ASSERT_EQUAL_UINT16(2, s3.param_pool_used);
    TEST_ASSERT_EQUAL_UINT8(9, sets[1].set_id);
    TEST_ASSERT_EQUAL_UINT8(2, sets[1].param_count);
    TEST_ASSERT_EQUAL_UINT32(3, sets[1].interval_ticks);
    TEST_ASSERT_FALSE(sets[1].enabled);

    /* TM(1,1) then TM(1,7) */
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]);
    pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(7, pkt.payload[2]);
}

/* @verifies OBSW-SRS-093 */
void test_defined_set_reports_live_s20_values_in_order(void)
{
    setup_define();
    uint8_t buf[16];
    const uint16_t ids[] = {0x2002, 0x2001};
    obsw_tc_t tc = make_define_tc(buf, 9, 1, ids, 2);
    obsw_s3_define(&tc, NULL, &s3);
    obsw_tm_store_init(&store);

    s20_table[0].value.u32 = 0x0A0B0C0D; /* changed after define: report is live */
    obsw_s3_report(&s3, 9);
    uint8_t pkt_buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    const uint8_t expected[] = {9, 0x3F, 0x80, 0x00, 0x00, 0x0A, 0x0B, 0x0C, 0x0D};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, pkt.payload + 11, sizeof(expected));
    s20_table[0].value.u32 = 0x01020304;
}

/* @verifies OBSW-SRS-093, OBSW-SRS-091 */
void test_defined_set_runs_after_enable(void)
{
    setup_define();
    uint8_t buf[16];
    const uint16_t ids[] = {0x2001};
    obsw_tc_t tc = make_define_tc(buf, 9, 1, ids, 1);
    obsw_s3_define(&tc, NULL, &s3);
    obsw_s3_tick(&s3);
    obsw_tm_store_init(&store);
    obsw_s3_tick(&s3);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store)); /* still disabled */

    uint8_t ebuf[5];
    obsw_tc_t en = make_enable_tc(9, 1, ebuf);
    obsw_s3_enable(&en, NULL, &s3);
    obsw_tm_store_init(&store);
    obsw_s3_tick(&s3);
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
}

/* @verifies OBSW-SRS-094 */
void test_define_rejects_existing_set_id(void)
{
    setup_define();
    uint8_t buf[16];
    const uint16_t ids[] = {0x2001};
    obsw_tc_t tc = make_define_tc(buf, 1, 1, ids, 1);
    assert_define_rejected(&tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-094 */
void test_define_rejects_unknown_param(void)
{
    setup_define();
    uint8_t buf[16];
    const uint16_t ids[] = {0x2001, 0x7777};
    obsw_tc_t tc = make_define_tc(buf, 9, 1, ids, 2);
    assert_define_rejected(&tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-094, OBSW-SRS-083 */
void test_define_rejects_length_mismatch(void)
{
    setup_define();
    uint8_t buf[16];
    const uint16_t ids[] = {0x2001, 0x2002};
    obsw_tc_t tc = make_define_tc(buf, 9, 1, ids, 2);
    tc.user_data_len--; /* last param_id truncated */
    assert_define_rejected(&tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
    tc.user_data_len = 5; /* shorter than the fixed header */
    obsw_tm_store_init(&store);
    assert_define_rejected(&tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-094 */
void test_define_rejects_zero_params(void)
{
    setup_define();
    uint8_t buf[16];
    obsw_tc_t tc = make_define_tc(buf, 9, 1, NULL, 0);
    assert_define_rejected(&tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-094 */
void test_define_rejects_when_pool_full(void)
{
    setup_define();
    s3.param_pool_len = 1;
    uint8_t buf[16];
    const uint16_t ids[] = {0x2001, 0x2002};
    obsw_tc_t tc = make_define_tc(buf, 9, 1, ids, 2);
    assert_define_rejected(&tc, OBSW_S1_FAIL_EXEC_ERROR);
}

/* @verifies OBSW-SRS-094 */
void test_define_rejects_when_no_set_slot(void)
{
    setup_define();
    s3.set_capacity = 1;
    uint8_t buf[16];
    const uint16_t ids[] = {0x2001};
    obsw_tc_t tc = make_define_tc(buf, 9, 1, ids, 1);
    assert_define_rejected(&tc, OBSW_S1_FAIL_EXEC_ERROR);
}

/* @verifies OBSW-SRS-094 */
void test_define_rejected_without_pool(void)
{
    setup_ctx(); /* default ctx: no pool, no capacity */
    s3.param_table     = s20_table;
    s3.param_table_len = 2;
    uint8_t buf[16];
    const uint16_t ids[] = {0x2001};
    obsw_tc_t tc = make_define_tc(buf, 9, 1, ids, 1);
    assert_define_rejected(&tc, OBSW_S1_FAIL_EXEC_ERROR);
}

/* ---- TC(3,3) delete ---------------------------------------------- */

/* Define set_id with the given S20 parameter IDs (asserts success) */
static void define_ok(uint8_t set_id, const uint16_t *ids, uint8_t n)
{
    uint8_t buf[16];
    obsw_tc_t tc = make_define_tc(buf, set_id, 1, ids, n);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_define(&tc, NULL, &s3));
    obsw_tm_store_init(&store);
}

static obsw_tc_t make_delete_tc(uint8_t *buf, const uint8_t *sids, uint8_t n)
{
    buf[0] = n;
    memcpy(buf + 1, sids, n);
    obsw_tc_t tc = {
        .apid          = 0x010,
        .service       = 3,
        .subservice    = 3,
        .seq_count     = 5,
        .user_data     = buf,
        .user_data_len = (uint16_t)(1 + n),
    };
    return tc;
}

/* Expect TM(1,2) 0x0005 and no change to sets or pool */
static void assert_delete_rejected(const uint8_t *sids, uint8_t n, uint16_t len_adjust)
{
    uint8_t buf[8];
    uint8_t count = s3.set_count;
    uint16_t used = s3.param_pool_used;
    obsw_tc_t tc  = make_delete_tc(buf, sids, n);
    tc.user_data_len = (uint16_t)(tc.user_data_len + len_adjust);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s3_delete(&tc, NULL, &s3));
    uint8_t pkt_buf[64];
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]);
    TEST_ASSERT_EQUAL_HEX16(OBSW_S1_FAIL_ILLEGAL_APP_DATA,
                            (uint16_t)((pkt.payload[15] << 8) | pkt.payload[16]));
    TEST_ASSERT_EQUAL_UINT8(count, s3.set_count);
    TEST_ASSERT_EQUAL_UINT16(used, s3.param_pool_used);
    obsw_tm_store_init(&store);
}

/* @verifies OBSW-SRS-096, OBSW-SRS-082 */
void test_delete_defined_set_frees_slot_and_pool(void)
{
    setup_define();
    const uint16_t ids[] = {0x2001, 0x2002};
    define_ok(9, ids, 2);
    uint8_t buf[8];
    const uint8_t sids[] = {9};
    obsw_tc_t tc = make_delete_tc(buf, sids, 1);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_delete(&tc, NULL, &s3));
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store)); /* TM(1,1), TM(1,7) */
    TEST_ASSERT_EQUAL_UINT8(1, s3.set_count);
    TEST_ASSERT_EQUAL_UINT16(0, s3.param_pool_used);
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_ERR_NULL, obsw_s3_report(&s3, 9));
}

/* @verifies OBSW-SRS-096 */
void test_define_delete_define_reuses_slot_and_pool(void)
{
    setup_define();
    const uint16_t two[] = {0x2001, 0x2002};
    const uint16_t one[] = {0x2002};
    define_ok(9, two, 2);
    define_ok(10, two, 2); /* pool now full (4 of 4) */
    uint8_t buf[8];
    const uint8_t sids[] = {9};
    obsw_tc_t tc = make_delete_tc(buf, sids, 1);
    obsw_s3_delete(&tc, NULL, &s3);
    obsw_tm_store_init(&store);
    define_ok(9, one, 1); /* fits again, same SID */
    TEST_ASSERT_EQUAL_UINT8(3, s3.set_count);
    TEST_ASSERT_EQUAL_UINT16(3, s3.param_pool_used);
}

/* @verifies OBSW-SRS-096 */
void test_delete_middle_set_keeps_others_reporting_correctly(void)
{
    setup_define();
    const uint16_t a[] = {0x2001};
    const uint16_t b[] = {0x2002, 0x2001};
    const uint16_t c[] = {0x2002};
    define_ok(20, a, 1);
    define_ok(21, b, 2);
    define_ok(22, c, 1);
    uint8_t buf[8];
    const uint8_t sids[] = {21};
    obsw_tc_t tc = make_delete_tc(buf, sids, 1);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_delete(&tc, NULL, &s3));
    obsw_tm_store_init(&store);
    TEST_ASSERT_EQUAL_UINT16(2, s3.param_pool_used);

    /* Remaining sets in definition order, values still from the right params */
    TEST_ASSERT_EQUAL_UINT8(20, sets[1].set_id);
    TEST_ASSERT_EQUAL_UINT8(22, sets[2].set_id);
    uint8_t pkt_buf[64];
    obsw_s3_report(&s3, 20);
    obsw_sp_packet_t pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    const uint8_t r20[] = {20, 0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r20, pkt.payload + 11, sizeof(r20));
    obsw_s3_report(&s3, 22);
    pkt = dequeue_pkt(pkt_buf, sizeof(pkt_buf));
    const uint8_t r22[] = {22, 0x3F, 0x80, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r22, pkt.payload + 11, sizeof(r22));
}

/* @verifies OBSW-SRS-096 */
void test_delete_several_in_one_tc(void)
{
    setup_define();
    const uint16_t a[] = {0x2001};
    define_ok(20, a, 1);
    define_ok(21, a, 1);
    uint8_t buf[8];
    const uint8_t sids[] = {21, 20};
    obsw_tc_t tc = make_delete_tc(buf, sids, 2);
    TEST_ASSERT_EQUAL_INT(0, obsw_s3_delete(&tc, NULL, &s3));
    TEST_ASSERT_EQUAL_UINT8(1, s3.set_count);
    TEST_ASSERT_EQUAL_UINT16(0, s3.param_pool_used);
}

/* @verifies OBSW-SRS-096 */
void test_delete_rejects_boot_enabled_unknown_and_repeated(void)
{
    setup_define();
    const uint16_t a[] = {0x2001};
    define_ok(20, a, 1);
    define_ok(21, a, 1);

    const uint8_t boot[] = {20, 1}; /* set 1 is a boot-time set */
    assert_delete_rejected(boot, 2, 0);
    const uint8_t unknown[] = {20, 99};
    assert_delete_rejected(unknown, 2, 0);
    const uint8_t repeated[] = {20, 20};
    assert_delete_rejected(repeated, 2, 0);

    sets[2].enabled = true; /* set 21 */
    const uint8_t enabled[] = {21};
    assert_delete_rejected(enabled, 1, 0);
    sets[2].enabled = false;
}

/* @verifies OBSW-SRS-096, OBSW-SRS-083 */
void test_delete_rejects_malformed(void)
{
    setup_define();
    const uint16_t a[] = {0x2001};
    define_ok(20, a, 1);
    const uint8_t sids[] = {20};
    assert_delete_rejected(sids, 1, (uint16_t)-1);  /* N claims more than sent */
    assert_delete_rejected(sids, 1, 1);             /* trailing byte */
    assert_delete_rejected(sids, 0, 0);             /* N = 0 */
}

/* @verifies OBSW-SRS-096 */
void test_delete_rejected_without_pool(void)
{
    setup_ctx(); /* no TC(3,1) storage: every set is a boot-time set */
    const uint8_t sids[] = {1};
    assert_delete_rejected(sids, 1, 0);
}

/* @verifies OBSW-SRS-095 */
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
    RUN_TEST(test_enable_truncated_emits_illegal_app_data);
    RUN_TEST(test_disable_clears_enabled);
    RUN_TEST(test_tick_does_not_emit_when_disabled);
    RUN_TEST(test_tick_emits_on_countdown_expiry);
    RUN_TEST(test_tick_reloads_countdown);
    RUN_TEST(test_tick_report_is_tm3_25);
    RUN_TEST(test_on_demand_report_emits_tm3_25);
    RUN_TEST(test_on_demand_unknown_set_returns_error);
    RUN_TEST(test_report_params_are_big_endian);
    RUN_TEST(test_report_float_is_big_endian_ieee754);
    RUN_TEST(test_define_creates_disabled_set);
    RUN_TEST(test_defined_set_reports_live_s20_values_in_order);
    RUN_TEST(test_defined_set_runs_after_enable);
    RUN_TEST(test_define_rejects_existing_set_id);
    RUN_TEST(test_define_rejects_unknown_param);
    RUN_TEST(test_define_rejects_length_mismatch);
    RUN_TEST(test_define_rejects_zero_params);
    RUN_TEST(test_define_rejects_when_pool_full);
    RUN_TEST(test_define_rejects_when_no_set_slot);
    RUN_TEST(test_define_rejected_without_pool);
    RUN_TEST(test_delete_defined_set_frees_slot_and_pool);
    RUN_TEST(test_define_delete_define_reuses_slot_and_pool);
    RUN_TEST(test_delete_middle_set_keeps_others_reporting_correctly);
    RUN_TEST(test_delete_several_in_one_tc);
    RUN_TEST(test_delete_rejects_boot_enabled_unknown_and_repeated);
    RUN_TEST(test_delete_rejects_malformed);
    RUN_TEST(test_delete_rejected_without_pool);
    return UNITY_END();
}
