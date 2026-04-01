#include "obsw/ccsds/space_packet.h"
#include "obsw/ccsds/tc_frame.h"
#include "obsw/pus/s6.h"
#include "obsw/tm/store.h"
#include "unity.h"

#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

/* S6 encodes addresses as 4-byte BE fields (PUS-C standard).
 * On 64-bit hosts, data segment addresses may exceed 32 bits.
 * Tests that write/read real memory are skipped in that case —
 * they run correctly on the 32-bit MSP430 target. */
#define ADDR_FITS_32BIT(p) ((uintptr_t)(p) <= 0xFFFFFFFFUL)

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s6_ctx_t s6;

/* Global buffers so addresses are in BSS — more likely to be < 4GB */
static uint8_t g_load_target[4];
static const uint8_t g_check_region[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static const uint8_t g_dump_region[]  = {0x01, 0x02, 0x03, 0x04};
static const uint8_t g_bad_region[]   = {0xAA, 0xBB};

static void setup_ctx(void)
{
    memset(g_load_target, 0, sizeof(g_load_target));
    obsw_tm_store_init(&store);
    s1.tm_store    = &store;
    s1.apid        = 0x010;
    s1.msg_counter = 0;
    s1.timestamp   = 0;
    s6.tm_store    = &store;
    s6.s1          = &s1;
    s6.apid        = 0x010;
    s6.msg_counter = 0;
    s6.timestamp   = 0;
}

static void encode_addr(uint8_t *ud, uintptr_t addr)
{
    ud[0] = (uint8_t)((addr >> 24) & 0xFFU);
    ud[1] = (uint8_t)((addr >> 16) & 0xFFU);
    ud[2] = (uint8_t)((addr >> 8) & 0xFFU);
    ud[3] = (uint8_t)(addr & 0xFFU);
}

static obsw_sp_packet_t dequeue_pkt(uint8_t *buf, size_t buf_len)
{
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, buf_len, &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

/* ------------------------------------------------------------------ */
/* TC(6,2) Load                                                        */
/* ------------------------------------------------------------------ */

void test_load_writes_to_memory(void)
{
    setup_ctx();
    if (!ADDR_FITS_32BIT(g_load_target)) {
        TEST_IGNORE_MESSAGE("64-bit host: address > 32 bits, skip memory write test");
        return;
    }
    uintptr_t addr = (uintptr_t)g_load_target;
    uint8_t ud[10];
    encode_addr(ud, addr);
    ud[4] = 0;
    ud[5] = 4;
    ud[6] = 0xDE;
    ud[7] = 0xAD;
    ud[8] = 0xBE;
    ud[9] = 0xEF;

    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 6,
                    .subservice    = 2,
                    .seq_count     = 1,
                    .user_data     = ud,
                    .user_data_len = 10};
    TEST_ASSERT_EQUAL_INT(0, obsw_s6_load(&tc, NULL, &s6));
    TEST_ASSERT_EQUAL_HEX8(0xDE, g_load_target[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, g_load_target[3]);
}

void test_load_too_short_rejects(void)
{
    setup_ctx();
    uint8_t ud[3] = {0};
    obsw_tc_t tc  = {.apid          = 0x010,
                     .service       = 6,
                     .subservice    = 2,
                     .seq_count     = 1,
                     .user_data     = ud,
                     .user_data_len = 3};
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
}

/* ------------------------------------------------------------------ */
/* TC(6,5) Check                                                       */
/* ------------------------------------------------------------------ */

void test_check_success_emits_tm6_10(void)
{
    setup_ctx();
    if (!ADDR_FITS_32BIT(g_check_region)) {
        TEST_IGNORE_MESSAGE("64-bit host: address > 32 bits, skip");
        return;
    }
    uint16_t expected_crc = obsw_crc16_ccitt(g_check_region, sizeof(g_check_region));
    uint8_t ud[9];
    encode_addr(ud, (uintptr_t)g_check_region);
    ud[4] = 0;
    ud[5] = (uint8_t)sizeof(g_check_region);
    ud[6] = 0;
    ud[7] = (uint8_t)(expected_crc >> 8);
    ud[8] = (uint8_t)(expected_crc & 0xFFU);

    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 6,
                    .subservice    = 5,
                    .seq_count     = 1,
                    .user_data     = ud,
                    .user_data_len = 9};
    TEST_ASSERT_EQUAL_INT(0, obsw_s6_check(&tc, NULL, &s6));

    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf));
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(10, pkt.payload[2]);
}

void test_check_failure_emits_tm6_11(void)
{
    setup_ctx();
    if (!ADDR_FITS_32BIT(g_bad_region)) {
        TEST_IGNORE_MESSAGE("64-bit host: address > 32 bits, skip");
        return;
    }
    uint8_t ud[9];
    encode_addr(ud, (uintptr_t)g_bad_region);
    ud[4] = 0;
    ud[5] = (uint8_t)sizeof(g_bad_region);
    ud[6] = 0;
    ud[7] = 0xFF;
    ud[8] = 0xFF; /* wrong CRC */

    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 6,
                    .subservice    = 5,
                    .seq_count     = 1,
                    .user_data     = ud,
                    .user_data_len = 9};
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_check(&tc, NULL, &s6));

    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf));
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(11, pkt.payload[2]);
}

void test_check_too_short_rejects(void)
{
    setup_ctx();
    uint8_t ud[5] = {0};
    obsw_tc_t tc  = {.apid          = 0x010,
                     .service       = 6,
                     .subservice    = 5,
                     .seq_count     = 1,
                     .user_data     = ud,
                     .user_data_len = 5};
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_check(&tc, NULL, &s6));
}

/* ------------------------------------------------------------------ */
/* TC(6,9) Dump                                                        */
/* ------------------------------------------------------------------ */

void test_dump_emits_tm6_6(void)
{
    setup_ctx();
    if (!ADDR_FITS_32BIT(g_dump_region)) {
        TEST_IGNORE_MESSAGE("64-bit host: address > 32 bits, skip");
        return;
    }
    uint8_t ud[7];
    encode_addr(ud, (uintptr_t)g_dump_region);
    ud[4] = 0;
    ud[5] = (uint8_t)sizeof(g_dump_region);
    ud[6] = 0;

    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 6,
                    .subservice    = 9,
                    .seq_count     = 1,
                    .user_data     = ud,
                    .user_data_len = 7};
    TEST_ASSERT_EQUAL_INT(0, obsw_s6_dump(&tc, NULL, &s6));

    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf));
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[2]);
    const uint8_t *data = pkt.payload + OBSW_PUS_TM_SEC_HDR_LEN + 5U;
    TEST_ASSERT_EQUAL_MEMORY(g_dump_region, data, sizeof(g_dump_region));
}

void test_dump_too_large_rejects(void)
{
    setup_ctx();
    uint8_t ud[7] = {0};
    ud[4]         = (uint8_t)(OBSW_TM_MAX_PACKET_LEN >> 8);
    ud[5]         = (uint8_t)(OBSW_TM_MAX_PACKET_LEN & 0xFF);
    obsw_tc_t tc  = {.apid          = 0x010,
                     .service       = 6,
                     .subservice    = 9,
                     .seq_count     = 1,
                     .user_data     = ud,
                     .user_data_len = 7};
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_dump(&tc, NULL, &s6));
}

void test_dump_too_short_rejects(void)
{
    setup_ctx();
    uint8_t ud[4] = {0};
    obsw_tc_t tc  = {.apid          = 0x010,
                     .service       = 6,
                     .subservice    = 9,
                     .seq_count     = 1,
                     .user_data     = ud,
                     .user_data_len = 4};
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_dump(&tc, NULL, &s6));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_writes_to_memory);
    RUN_TEST(test_load_too_short_rejects);
    RUN_TEST(test_check_success_emits_tm6_10);
    RUN_TEST(test_check_failure_emits_tm6_11);
    RUN_TEST(test_check_too_short_rejects);
    RUN_TEST(test_dump_emits_tm6_6);
    RUN_TEST(test_dump_too_large_rejects);
    RUN_TEST(test_dump_too_short_rejects);
    return UNITY_END();
}