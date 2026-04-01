#include "obsw/ccsds/space_packet.h"
#include "obsw/fdir/fsm.h"
#include "obsw/pus/s8.h"
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
static obsw_s8_ctx_t s8;
static obsw_fsm_ctx_t fsm;

static int fn_call_count;
static int fn_recover(const uint8_t *args, uint8_t args_len, void *ctx)
{
    (void)args;
    (void)args_len;
    fn_call_count++;
    obsw_fsm_to_nominal((obsw_fsm_ctx_t *)ctx);
    return 0;
}

static int fn_failing(const uint8_t *args, uint8_t args_len, void *ctx)
{
    (void)args;
    (void)args_len;
    (void)ctx;
    return -1;
}

static obsw_s8_entry_t table[] = {
    {.function_id = OBSW_S8_FN_RECOVER_NOMINAL, .fn = fn_recover, .ctx = &fsm},
    {.function_id = 99, .fn = fn_failing, .ctx = NULL},
};

static void setup_ctx(void)
{
    fn_call_count = 0;
    obsw_tm_store_init(&store);
    obsw_fsm_config_t cfg = {0};
    obsw_fsm_init(&fsm, &cfg);

    s1.tm_store    = &store;
    s1.apid        = 0x010;
    s1.msg_counter = 0;
    s1.timestamp   = 0;

    s8.tm_store    = &store;
    s8.s1          = &s1;
    s8.apid        = 0x010;
    s8.msg_counter = 0;
    s8.timestamp   = 0;
    s8.table       = table;
    s8.table_len   = 2;
}

/* Build TC(8,1) with given function_id and no args */
static obsw_tc_t make_s8_tc(uint16_t fn_id, uint8_t *ud)
{
    ud[0]        = (uint8_t)(fn_id >> 8);
    ud[1]        = (uint8_t)(fn_id & 0xFF);
    ud[2]        = 0; /* args_len = 0 */
    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 8,
                    .subservice    = 1,
                    .seq_count     = 1,
                    .user_data     = ud,
                    .user_data_len = 3};
    return tc;
}

static obsw_sp_packet_t dequeue_pkt(uint8_t *buf, size_t buf_len)
{
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, buf_len, &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

void test_perform_known_function(void)
{
    setup_ctx();
    obsw_fsm_to_safe(&fsm);

    uint8_t ud[3];
    obsw_tc_t tc = make_s8_tc(OBSW_S8_FN_RECOVER_NOMINAL, ud);
    TEST_ASSERT_EQUAL_INT(0, obsw_s8_perform(&tc, NULL, &s8));
    TEST_ASSERT_EQUAL_INT(1, fn_call_count);
    TEST_ASSERT_FALSE(obsw_fsm_is_safe(&fsm));
}

void test_perform_emits_tm11_and_tm17(void)
{
    setup_ctx();
    uint8_t ud[3];
    obsw_tc_t tc = make_s8_tc(OBSW_S8_FN_RECOVER_NOMINAL, ud);
    obsw_s8_perform(&tc, NULL, &s8);
    /* TM(1,1) accept + TM(1,7) complete */
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]);
}

void test_unknown_function_emits_failure(void)
{
    setup_ctx();
    uint8_t ud[3];
    obsw_tc_t tc = make_s8_tc(0xBEEF, ud);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s8_perform(&tc, NULL, &s8));
    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]); /* TM(1,2) failure */
}

void test_failing_function_emits_tm18(void)
{
    setup_ctx();
    uint8_t ud[3];
    obsw_tc_t tc = make_s8_tc(99, ud);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s8_perform(&tc, NULL, &s8));
    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf)); /* TM(1,1) accept */
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(8, pkt.payload[2]); /* TM(1,8) completion failure */
}

void test_null_ctx_returns_error(void)
{
    setup_ctx();
    uint8_t ud[3];
    obsw_tc_t tc = make_s8_tc(1, ud);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s8_perform(&tc, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_perform_known_function);
    RUN_TEST(test_perform_emits_tm11_and_tm17);
    RUN_TEST(test_unknown_function_emits_failure);
    RUN_TEST(test_failing_function_emits_tm18);
    RUN_TEST(test_null_ctx_returns_error);
    return UNITY_END();
}