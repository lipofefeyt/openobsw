#include "unity.h"
#include "obsw/pus/s17.h"
#include "obsw/tm/store.h"
#include "obsw/ccsds/space_packet.h"

void setUp(void) {}
void tearDown(void) {}

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s17_ctx_t s17;
static obsw_tc_t tc;

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);
    s1.tm_store = &store;
    s1.apid = 0x010;
    s1.msg_counter = 0;
    s1.timestamp = 0;

    s17.tm_store = &store;
    s17.s1 = &s1;
    s17.apid = 0x010;
    s17.msg_counter = 0;
    s17.timestamp = 0;

    tc.apid = 0x010;
    tc.service = 17;
    tc.subservice = 1;
    tc.seq_count = 1;
    tc.user_data = NULL;
    tc.user_data_len = 0;
}

void test_ping_enqueues_three_packets(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(0, obsw_s17_ping(&tc, NULL, &s17));
    /* TM(1,1) + TM(17,2) + TM(1,7) = 3 packets */
    TEST_ASSERT_EQUAL_size_t(3, obsw_tm_store_count(&store));
}

void test_ping_first_packet_is_tm11(void)
{
    setup_ctx();
    obsw_s17_ping(&tc, NULL, &s17);

    uint8_t buf[256];
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]); /* svc  = 1 */
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]); /* subsvc = 1 */
}

void test_ping_second_packet_is_tm172(void)
{
    setup_ctx();
    obsw_s17_ping(&tc, NULL, &s17);

    uint8_t buf[256];
    uint16_t len = 0;
    /* discard TM(1,1) */
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    /* read TM(17,2) */
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    TEST_ASSERT_EQUAL_UINT8(17, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]);
}

void test_ping_third_packet_is_tm17(void)
{
    setup_ctx();
    obsw_s17_ping(&tc, NULL, &s17);

    uint8_t buf[256];
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]); /* svc  = 1 */
    TEST_ASSERT_EQUAL_UINT8(7, pkt.payload[2]); /* subsvc = 7 */
}

void test_null_ctx_returns_error(void)
{
    setup_ctx();
    TEST_ASSERT_NOT_EQUAL(0, obsw_s17_ping(&tc, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ping_enqueues_three_packets);
    RUN_TEST(test_ping_first_packet_is_tm11);
    RUN_TEST(test_ping_second_packet_is_tm172);
    RUN_TEST(test_ping_third_packet_is_tm17);
    RUN_TEST(test_null_ctx_returns_error);
    return UNITY_END();
}