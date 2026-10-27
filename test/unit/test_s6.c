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

/* Ground address map used by every test:
 *   0x00001000  g_ram  16 B  READ | WRITE
 *   0x00008000  g_rom   9 B  READ only
 *   0xFFFFFFF0  g_top  16 B  READ (exercises 32-bit wrap-around)
 * Ground addresses are independent of where the buffers really live, so
 * these tests run on 64-bit hosts too. */
#define RAM_ADDR 0x00001000UL
#define ROM_ADDR 0x00008000UL
#define TOP_ADDR 0xFFFFFFF0UL

static uint8_t g_ram[16];
static const uint8_t g_rom[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static const uint8_t g_top[16];

static const obsw_s6_region_t g_regions[] = {
    {.start  = RAM_ADDR,
     .size   = sizeof(g_ram),
     .base   = g_ram,
     .access = OBSW_S6_ACCESS_READ | OBSW_S6_ACCESS_WRITE},
    {.start = ROM_ADDR, .size = sizeof(g_rom), .base = g_rom, .access = OBSW_S6_ACCESS_READ},
    {.start = TOP_ADDR, .size = sizeof(g_top), .base = g_top, .access = OBSW_S6_ACCESS_READ},
};

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s6_ctx_t s6;

static void setup_ctx(void)
{
    memset(g_ram, 0, sizeof(g_ram));
    obsw_tm_store_init(&store);
    s1.tm_store     = &store;
    s1.apid         = 0x010;
    s1.msg_counter  = 0;
    s1.timestamp    = 0;
    s6.tm_store     = &store;
    s6.s1           = &s1;
    s6.apid         = 0x010;
    s6.msg_counter  = 0;
    s6.timestamp    = 0;
    s6.regions      = g_regions;
    s6.region_count = (uint8_t)(sizeof(g_regions) / sizeof(g_regions[0]));
}

static void encode_addr(uint8_t *ud, uint32_t addr)
{
    ud[0] = (uint8_t)((addr >> 24) & 0xFFU);
    ud[1] = (uint8_t)((addr >> 16) & 0xFFU);
    ud[2] = (uint8_t)((addr >> 8) & 0xFFU);
    ud[3] = (uint8_t)(addr & 0xFFU);
}

static void encode_u16(uint8_t *ud, uint16_t v)
{
    ud[0] = (uint8_t)(v >> 8);
    ud[1] = (uint8_t)(v & 0xFFU);
}

static obsw_sp_packet_t dequeue_pkt(uint8_t *buf, size_t buf_len)
{
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, buf_len, &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

/* Assert the next TM is TM(1,2) carrying @p code, and that it is the only TM. */
static void assert_rejected(uint16_t code)
{
    uint8_t buf[256];
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]);
    const uint8_t *app = pkt.payload + OBSW_PUS_TM_SEC_HDR_LEN;
    TEST_ASSERT_EQUAL_HEX16(code, (uint16_t)(((uint16_t)app[4] << 8) | app[5]));
    TEST_ASSERT_EQUAL_UINT8(0, obsw_tm_store_count(&store));
}

static obsw_tc_t make_tc(uint8_t subsvc, const uint8_t *ud, uint16_t len)
{
    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 6,
                    .subservice    = subsvc,
                    .seq_count     = 1,
                    .user_data     = ud,
                    .user_data_len = len};
    return tc;
}

/* TC(6,2) user data: addr | len | data */
static uint16_t build_load(uint8_t *ud, uint32_t addr, const uint8_t *data, uint16_t len)
{
    encode_addr(ud, addr);
    encode_u16(ud + 4, len);
    memcpy(ud + 6, data, len);
    return (uint16_t)(6U + len);
}

/* TC(6,5) / TC(6,9) user data: addr | len | mem_id [| crc] */
static void build_range(uint8_t *ud, uint32_t addr, uint16_t len)
{
    encode_addr(ud, addr);
    encode_u16(ud + 4, len);
    ud[6] = 0;
}

static const uint8_t k_pattern[4] = {0xDE, 0xAD, 0xBE, 0xEF};

/* ------------------------------------------------------------------ */
/* TC(6,2) Load                                                        */
/* ------------------------------------------------------------------ */

void test_load_writes_to_memory(void)
{
    setup_ctx();
    uint8_t ud[16];
    uint16_t n   = build_load(ud, RAM_ADDR + 4U, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, n);

    TEST_ASSERT_EQUAL_INT(0, obsw_s6_load(&tc, NULL, &s6));
    TEST_ASSERT_EQUAL_MEMORY(k_pattern, g_ram + 4, sizeof(k_pattern));
    TEST_ASSERT_EQUAL_HEX8(0x00, g_ram[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_ram[8]);
}

void test_load_to_last_bytes_of_region(void)
{
    setup_ctx();
    uint8_t ud[16];
    uint16_t n   = build_load(ud, RAM_ADDR + 12U, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, n);

    TEST_ASSERT_EQUAL_INT(0, obsw_s6_load(&tc, NULL, &s6));
    TEST_ASSERT_EQUAL_MEMORY(k_pattern, g_ram + 12, sizeof(k_pattern));
}

void test_load_straddling_region_end_rejected(void)
{
    setup_ctx();
    uint8_t ud[16];
    uint16_t n   = build_load(ud, RAM_ADDR + 13U, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, n);

    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_MEM_ACCESS);
    uint8_t zero[sizeof(g_ram)] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zero, g_ram, sizeof(g_ram));
}

void test_load_before_region_start_rejected(void)
{
    setup_ctx();
    uint8_t ud[16];
    uint16_t n   = build_load(ud, RAM_ADDR - 1U, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, n);

    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_MEM_ACCESS);
}

void test_load_to_read_only_region_rejected(void)
{
    setup_ctx();
    uint8_t ud[16];
    uint16_t n   = build_load(ud, ROM_ADDR, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, n);

    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_MEM_ACCESS);
    TEST_ASSERT_EQUAL_HEX8('1', g_rom[0]);
}

void test_load_without_regions_rejected(void)
{
    setup_ctx();
    s6.regions      = NULL;
    s6.region_count = 0;
    uint8_t ud[16];
    uint16_t n   = build_load(ud, RAM_ADDR, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, n);

    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_MEM_ACCESS);
}

void test_load_data_len_wrap_rejected(void)
{
    /* data_len = 0xFFFF: 6 + 0xFFFF wraps to 5 in 16 bits, which used to
     * pass the length check and over-read the TC buffer. */
    setup_ctx();
    uint8_t ud[10];
    encode_addr(ud, RAM_ADDR);
    encode_u16(ud + 4, 0xFFFFU);
    memcpy(ud + 6, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, sizeof(ud));

    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

void test_load_truncated_data_rejected(void)
{
    setup_ctx();
    uint8_t ud[16];
    build_load(ud, RAM_ADDR, k_pattern, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(2, ud, 8); /* declares 4 data bytes, carries 2 */

    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

void test_load_too_short_rejects(void)
{
    setup_ctx();
    uint8_t ud[3] = {0};
    obsw_tc_t tc  = make_tc(2, ud, sizeof(ud));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_load(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* ------------------------------------------------------------------ */
/* TC(6,5) Check                                                       */
/* ------------------------------------------------------------------ */

static obsw_tc_t make_check(uint8_t *ud, uint32_t addr, uint16_t len, uint16_t crc)
{
    build_range(ud, addr, len);
    encode_u16(ud + 7, crc);
    return make_tc(5, ud, OBSW_S6_CHECK_LEN);
}

void test_check_success_emits_tm6_10(void)
{
    setup_ctx();
    uint8_t ud[OBSW_S6_CHECK_LEN];
    obsw_tc_t tc = make_check(ud, ROM_ADDR, sizeof(g_rom), obsw_crc16_ccitt(g_rom, sizeof(g_rom)));
    TEST_ASSERT_EQUAL_INT(0, obsw_s6_check(&tc, NULL, &s6));

    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf)); /* TM(1,1) */
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(10, pkt.payload[2]);
}

void test_check_failure_emits_tm6_11(void)
{
    setup_ctx();
    uint8_t ud[OBSW_S6_CHECK_LEN];
    obsw_tc_t tc = make_check(ud, ROM_ADDR, sizeof(g_rom), 0xFFFFU);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_check(&tc, NULL, &s6));

    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf)); /* TM(1,1) */
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(11, pkt.payload[2]);
    const uint8_t *app = pkt.payload + OBSW_PUS_TM_SEC_HDR_LEN;
    uint16_t actual    = obsw_crc16_ccitt(g_rom, sizeof(g_rom));
    TEST_ASSERT_EQUAL_HEX16(actual, (uint16_t)(((uint16_t)app[6] << 8) | app[7]));
}

void test_check_out_of_region_rejected(void)
{
    setup_ctx();
    uint8_t ud[OBSW_S6_CHECK_LEN];
    obsw_tc_t tc = make_check(ud, ROM_ADDR, (uint16_t)(sizeof(g_rom) + 1U), 0);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_check(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_MEM_ACCESS);
}

void test_check_too_short_rejects(void)
{
    setup_ctx();
    uint8_t ud[5] = {0};
    obsw_tc_t tc  = make_tc(5, ud, sizeof(ud));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_check(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* ------------------------------------------------------------------ */
/* TC(6,9) Dump                                                        */
/* ------------------------------------------------------------------ */

void test_dump_emits_tm6_6(void)
{
    setup_ctx();
    memcpy(g_ram, k_pattern, sizeof(k_pattern));
    uint8_t ud[OBSW_S6_DUMP_LEN];
    build_range(ud, RAM_ADDR, sizeof(k_pattern));
    obsw_tc_t tc = make_tc(9, ud, sizeof(ud));
    TEST_ASSERT_EQUAL_INT(0, obsw_s6_dump(&tc, NULL, &s6));

    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf)); /* TM(1,1) */
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(6, pkt.payload[2]);
    const uint8_t *app = pkt.payload + OBSW_PUS_TM_SEC_HDR_LEN;
    TEST_ASSERT_EQUAL_HEX8(0x00, app[0]); /* ground address echoed, not local */
    TEST_ASSERT_EQUAL_HEX8(0x10, app[2]);
    TEST_ASSERT_EQUAL_MEMORY(k_pattern, app + 5, sizeof(k_pattern));
}

void test_dump_read_only_region(void)
{
    setup_ctx();
    uint8_t ud[OBSW_S6_DUMP_LEN];
    build_range(ud, ROM_ADDR + 2U, 3U);
    obsw_tc_t tc = make_tc(9, ud, sizeof(ud));
    TEST_ASSERT_EQUAL_INT(0, obsw_s6_dump(&tc, NULL, &s6));

    uint8_t buf[256];
    dequeue_pkt(buf, sizeof(buf)); /* TM(1,1) */
    obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_MEMORY(g_rom + 2, pkt.payload + OBSW_PUS_TM_SEC_HDR_LEN + 5U, 3U);
}

void test_dump_near_address_space_top(void)
{
    /* addr + len would wrap past 0xFFFFFFFF; must not alias low memory */
    setup_ctx();
    uint8_t ud[OBSW_S6_DUMP_LEN];
    build_range(ud, TOP_ADDR + 8U, 8U);
    obsw_tc_t ok = make_tc(9, ud, sizeof(ud));
    TEST_ASSERT_EQUAL_INT(0, obsw_s6_dump(&ok, NULL, &s6));

    setup_ctx();
    build_range(ud, TOP_ADDR + 8U, 16U);
    obsw_tc_t wrap = make_tc(9, ud, sizeof(ud));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_dump(&wrap, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_MEM_ACCESS);
}

void test_dump_unmapped_address_rejected(void)
{
    setup_ctx();
    uint8_t ud[OBSW_S6_DUMP_LEN];
    build_range(ud, 0x00000000UL, 4U);
    obsw_tc_t tc = make_tc(9, ud, sizeof(ud));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_dump(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_MEM_ACCESS);
}

void test_dump_too_large_rejects(void)
{
    setup_ctx();
    uint8_t ud[OBSW_S6_DUMP_LEN];
    build_range(ud, RAM_ADDR, (uint16_t)OBSW_TM_MAX_PACKET_LEN);
    obsw_tc_t tc = make_tc(9, ud, sizeof(ud));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_dump(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

void test_dump_too_short_rejects(void)
{
    setup_ctx();
    uint8_t ud[4] = {0};
    obsw_tc_t tc  = make_tc(9, ud, sizeof(ud));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s6_dump(&tc, NULL, &s6));
    assert_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_writes_to_memory);
    RUN_TEST(test_load_to_last_bytes_of_region);
    RUN_TEST(test_load_straddling_region_end_rejected);
    RUN_TEST(test_load_before_region_start_rejected);
    RUN_TEST(test_load_to_read_only_region_rejected);
    RUN_TEST(test_load_without_regions_rejected);
    RUN_TEST(test_load_data_len_wrap_rejected);
    RUN_TEST(test_load_truncated_data_rejected);
    RUN_TEST(test_load_too_short_rejects);
    RUN_TEST(test_check_success_emits_tm6_10);
    RUN_TEST(test_check_failure_emits_tm6_11);
    RUN_TEST(test_check_out_of_region_rejected);
    RUN_TEST(test_check_too_short_rejects);
    RUN_TEST(test_dump_emits_tm6_6);
    RUN_TEST(test_dump_read_only_region);
    RUN_TEST(test_dump_near_address_space_top);
    RUN_TEST(test_dump_unmapped_address_rejected);
    RUN_TEST(test_dump_too_large_rejects);
    RUN_TEST(test_dump_too_short_rejects);
    return UNITY_END();
}
