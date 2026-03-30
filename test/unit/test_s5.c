#include "unity.h"
#include "obsw/pus/s5.h"
#include "obsw/tm/store.h"
#include "obsw/ccsds/space_packet.h"

void setUp(void) {}
void tearDown(void) {}

static obsw_tm_store_t store;
static obsw_s5_ctx_t s5;

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);
    s5.tm_store = &store;
    s5.apid = 0x010;
    s5.msg_counter = 0;
    s5.timestamp = 0;
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

void test_info_event_subsvc_1(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK,
                          obsw_s5_report(&s5, OBSW_S5_INFO, 0x0001, NULL, 0));
    obsw_sp_packet_t pkt = dequeue_pkt();
    TEST_ASSERT_EQUAL_UINT8(5, pkt.payload[1]); /* svc */
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]); /* subsvc */
}

void test_high_severity_subsvc_4(void)
{
    setup_ctx();
    TEST_ASSERT_EQUAL_INT(OBSW_PUS_TM_OK,
                          obsw_s5_report(&s5, OBSW_S5_HIGH, 0x00FF, NULL, 0));
    obsw_sp_packet_t pkt = dequeue_pkt();
    TEST_ASSERT_EQUAL_UINT8(5, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(4, pkt.payload[2]);
}

void test_event_id_in_data_field(void)
{
    setup_ctx();
    obsw_s5_report(&s5, OBSW_S5_LOW, 0xABCD, NULL, 0);
    obsw_sp_packet_t pkt = dequeue_pkt();
    /* Event ID starts after PUS secondary header (11 bytes) */
    const uint8_t *data = pkt.payload + 11U;
    TEST_ASSERT_EQUAL_HEX8(0xAB, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, data[1]);
}

void test_auxiliary_data_appended(void)
{
    setup_ctx();
    uint8_t aux[] = {0xDE, 0xAD};
    obsw_s5_report(&s5, OBSW_S5_MEDIUM, 0x0001, aux, sizeof(aux));
    obsw_sp_packet_t pkt = dequeue_pkt();
    const uint8_t *data = pkt.payload + 11U;
    TEST_ASSERT_EQUAL_HEX8(0xDE, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, data[3]);
}

void test_null_ctx_rejected(void)
{
    TEST_ASSERT_NOT_EQUAL(OBSW_PUS_TM_OK,
                          obsw_s5_report(NULL, OBSW_S5_INFO, 0x0001, NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_info_event_subsvc_1);
    RUN_TEST(test_high_severity_subsvc_4);
    RUN_TEST(test_event_id_in_data_field);
    RUN_TEST(test_auxiliary_data_appended);
    RUN_TEST(test_null_ctx_rejected);
    return UNITY_END();
}