#include "obsw/ccsds/space_packet.h"
#include "obsw/pus/s1.h"
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
static obsw_tc_t tc;

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);
    s1.tm_store      = &store;
    s1.apid          = 0x010;
    s1.msg_counter   = 0;
    s1.timestamp     = 0x00000001;
    tc.apid          = 0x010;
    tc.service       = 17;
    tc.subservice    = 1;
    tc.seq_count     = 42;
    tc.user_data     = NULL;
    tc.user_data_len = 0;
}

/* buf must be caller-owned — payload pointer lives inside it */
static obsw_sp_packet_t dequeue_pkt(uint8_t *buf, size_t buf_len)
{
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, buf_len, &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

void test_accept_success_enqueues_tm11(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK, obsw_s1_accept_success(&s1, &tc));
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));

    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]);
}

void test_accept_failure_enqueues_tm12(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK,
                          obsw_s1_accept_failure(&s1, &tc, OBSW_S1_FAIL_ILLEGAL_SVC));
    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]);
}

void test_completion_success_enqueues_tm17(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK, obsw_s1_completion_success(&s1, &tc));
    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(7, pkt.payload[2]);
}

void test_completion_failure_enqueues_tm18(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK,
                          obsw_s1_completion_failure(&s1, &tc, OBSW_S1_FAIL_EXEC_ERROR));
    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(8, pkt.payload[2]);
}

void test_null_args_rejected(void)
{
    setup_ctx();
    TEST_ASSERT_NOT_EQUAL(OBSW_PUS_TM_OK, obsw_s1_accept_success(NULL, &tc));
    TEST_ASSERT_NOT_EQUAL(OBSW_PUS_TM_OK, obsw_s1_accept_success(&s1, NULL));
}

void test_msg_counter_increments(void)
{
    setup_ctx();
    s1.msg_counter = 0;
    obsw_s1_accept_success(&s1, &tc);
    obsw_s1_accept_success(&s1, &tc);
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accept_success_enqueues_tm11);
    RUN_TEST(test_accept_failure_enqueues_tm12);
    RUN_TEST(test_completion_success_enqueues_tm17);
    RUN_TEST(test_completion_failure_enqueues_tm18);
    RUN_TEST(test_null_args_rejected);
    RUN_TEST(test_msg_counter_increments);
    return UNITY_END();
}