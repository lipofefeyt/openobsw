#include "obsw/ccsds/space_packet.h"
#include "obsw/pus/s11.h"
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
static obsw_s11_ctx_t s11;
static obsw_s11_activity_t acts[3];
static obsw_tc_dispatcher_t disp;

/* Released TCs land here: record subservices in release order */
static uint8_t released_sub[8];
static int released_n;
static int target(const obsw_tc_t *tc, obsw_tc_responder_t r, void *ctx)
{
    (void)r;
    (void)ctx;
    if (released_n < 8) {
        released_sub[released_n] = tc->subservice;
    }
    released_n++;
    return 0;
}

static obsw_tc_route_t routes[] = {
    {.apid = 0xFFFF, .service = 17, .subservice = 1, .handler = target, .ctx = NULL},
    {.apid = 0xFFFF, .service = 17, .subservice = 2, .handler = target, .ctx = NULL},
    {.apid = 0xFFFF, .service = 17, .subservice = 3, .handler = target, .ctx = NULL},
};

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);
    memset(&s1, 0, sizeof(s1));
    s1.tm_store = &store;
    obsw_s11_init(&s11, &s1, acts, 3);
    obsw_tc_dispatcher_init(&disp, routes, 3, obsw_s1_dispatch_responder, &s1);
    released_n = 0;
}

static uint8_t ud[96];

/* TC(11,4): tag coarse.fine | PUS TC packet svc/sub with n app bytes */
static obsw_tc_t make_insert(uint32_t coarse, uint16_t fine, uint8_t svc, uint8_t sub, uint8_t n)
{
    ud[0] = (uint8_t)(coarse >> 24);
    ud[1] = (uint8_t)(coarse >> 16);
    ud[2] = (uint8_t)(coarse >> 8);
    ud[3] = (uint8_t)coarse;
    ud[4] = (uint8_t)(fine >> 8);
    ud[5] = (uint8_t)fine;
    obsw_sp_primary_hdr_t h = {.type         = OBSW_SP_TYPE_TC,
                               .sec_hdr_flag = 1,
                               .apid         = 0x010,
                               .seq_flags    = OBSW_SP_SEQ_UNSEGMENTED,
                               .seq_count    = 5,
                               .data_len     = (uint16_t)(4U + n)};
    obsw_sp_encode_primary(&h, ud + 6);
    ud[12] = 0x11;
    ud[13] = svc;
    ud[14] = sub;
    ud[15] = 0;
    ud[16] = 0;
    memset(ud + 17, 0x5A, n);
    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 11,
                    .subservice    = 4,
                    .seq_count     = 2,
                    .user_data     = ud,
                    .user_data_len = (uint16_t)(6U + 11U + n)};
    return tc;
}

static void insert_ok(uint32_t coarse, uint16_t fine, uint8_t sub)
{
    obsw_tc_t tc = make_insert(coarse, fine, 17, sub, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s11_insert(&tc, NULL, &s11));
    obsw_tm_store_init(&store);
}

static obsw_tc_t make_simple(uint8_t sub, uint16_t arg, uint16_t len)
{
    ud[0]        = (uint8_t)(arg >> 8);
    ud[1]        = (uint8_t)arg;
    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 11,
                    .subservice    = sub,
                    .seq_count     = 3,
                    .user_data     = ud,
                    .user_data_len = len};
    return tc;
}

static void expect_rejected(uint16_t code)
{
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
    uint8_t buf[64];
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, sizeof(buf), &len);
    obsw_sp_packet_t p;
    obsw_sp_parse(buf, len, &p);
    TEST_ASSERT_EQUAL_UINT8(2, p.payload[2]);
    TEST_ASSERT_EQUAL_HEX16(code, (uint16_t)((p.payload[15] << 8) | p.payload[16]));
}

static uint8_t run_at(uint32_t coarse, uint16_t fine)
{
    obsw_obt_t t = {.coarse = coarse, .fine = fine, .rem = 0};
    return obsw_s11_run(&s11, &t, &disp);
}

/* ---- Insert ---------------------------------------------------------- */

/* @verifies OBSW-SRS-280, OBSW-SRS-082 */
void test_insert_stores_activity_with_sequential_ids(void)
{
    setup_ctx();
    obsw_tc_t tc = make_insert(100, 0, 17, 1, 2);
    TEST_ASSERT_EQUAL_INT(0, obsw_s11_insert(&tc, NULL, &s11));
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store)); /* TM(1,1), TM(1,7) */
    insert_ok(200, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(2, s11.count);
    TEST_ASSERT_EQUAL_UINT16(1, acts[0].request_id);
    TEST_ASSERT_EQUAL_UINT16(2, acts[1].request_id);
    TEST_ASSERT_EQUAL_UINT32(100, acts[0].coarse);
    TEST_ASSERT_EQUAL_UINT16(13, acts[0].tc_len);
}

/* @verifies OBSW-SRS-281, OBSW-SRS-083 */
void test_insert_rejects_malformed_tc(void)
{
    setup_ctx();
    obsw_tc_t tc = make_insert(100, 0, 17, 1, 2);
    tc.user_data_len--; /* embedded packet length no longer matches */
    TEST_ASSERT_NOT_EQUAL(0, obsw_s11_insert(&tc, NULL, &s11));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);

    obsw_tm_store_init(&store);
    tc.user_data_len = OBSW_S11_TAG_LEN; /* tag only */
    TEST_ASSERT_NOT_EQUAL(0, obsw_s11_insert(&tc, NULL, &s11));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
    TEST_ASSERT_EQUAL_UINT8(0, s11.count);
}

/* @verifies OBSW-SRS-281 */
void test_insert_rejects_oversized_and_nested_tc(void)
{
    setup_ctx();
    obsw_tc_t tc = make_insert(100, 0, 17, 1, (uint8_t)(OBSW_S11_TC_MAX_LEN - 10U));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s11_insert(&tc, NULL, &s11));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);

    obsw_tm_store_init(&store);
    tc = make_insert(100, 0, 11, 6, 0);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s11_insert(&tc, NULL, &s11));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-281 */
void test_insert_when_full_rejected(void)
{
    setup_ctx();
    insert_ok(1, 0, 1);
    insert_ok(2, 0, 1);
    insert_ok(3, 0, 1);
    obsw_tc_t tc = make_insert(4, 0, 17, 1, 0);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s11_insert(&tc, NULL, &s11));
    expect_rejected(OBSW_S1_FAIL_EXEC_ERROR);
}

/* ---- Release --------------------------------------------------------- */

/* @verifies OBSW-SRS-282 */
void test_nothing_released_before_tag(void)
{
    setup_ctx();
    insert_ok(100, 0x8000, 1);
    TEST_ASSERT_EQUAL_UINT8(0, run_at(100, 0x7FFF));
    TEST_ASSERT_EQUAL_INT(0, released_n);
    TEST_ASSERT_EQUAL_UINT8(1, run_at(100, 0x8000));
    TEST_ASSERT_EQUAL_INT(1, released_n);
    TEST_ASSERT_EQUAL_UINT8(0, s11.count);
    TEST_ASSERT_EQUAL_UINT8(0, run_at(200, 0)); /* released once only */
}

/* @verifies OBSW-SRS-282 */
void test_release_in_time_order_ties_by_insertion(void)
{
    setup_ctx();
    insert_ok(30, 0, 3);
    insert_ok(10, 0, 1);
    insert_ok(30, 0, 2); /* same tag as the first: inserted later, runs later */
    TEST_ASSERT_EQUAL_UINT8(3, run_at(50, 0));
    TEST_ASSERT_EQUAL_UINT8(1, released_sub[0]);
    TEST_ASSERT_EQUAL_UINT8(3, released_sub[1]);
    TEST_ASSERT_EQUAL_UINT8(2, released_sub[2]);
}

/* @verifies OBSW-SRS-282 */
void test_past_tag_released_on_next_run(void)
{
    setup_ctx();
    insert_ok(0, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(1, run_at(500, 0));
}

/* @verifies OBSW-SRS-282 */
void test_released_tc_goes_through_dispatcher_acceptance(void)
{
    setup_ctx();
    obsw_tc_t tc = make_insert(1, 0, 42, 1, 0); /* no route for service 42 */
    obsw_s11_insert(&tc, NULL, &s11);
    obsw_tm_store_init(&store);
    run_at(2, 0);
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_SVC);
}

/* ---- Delete, enable, disable ----------------------------------------- */

/* @verifies OBSW-SRS-283 */
void test_delete_by_request_id_and_all(void)
{
    setup_ctx();
    insert_ok(10, 0, 1);
    insert_ok(20, 0, 2);
    obsw_tc_t del = make_simple(5, 1, 2);
    TEST_ASSERT_EQUAL_INT(0, obsw_s11_delete(&del, NULL, &s11));
    TEST_ASSERT_EQUAL_UINT8(1, s11.count);
    TEST_ASSERT_EQUAL_UINT16(2, acts[0].request_id);

    obsw_tm_store_init(&store);
    del = make_simple(5, 99, 2);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s11_delete(&del, NULL, &s11));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);

    obsw_tc_t all = make_simple(6, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s11_delete_all(&all, NULL, &s11));
    TEST_ASSERT_EQUAL_UINT8(0, s11.count);
    TEST_ASSERT_EQUAL_UINT8(0, run_at(100, 0));
}

/* @verifies OBSW-SRS-280 */
void test_request_ids_keep_counting_after_delete(void)
{
    setup_ctx();
    insert_ok(10, 0, 1);
    obsw_tc_t all = make_simple(6, 0, 0);
    obsw_s11_delete_all(&all, NULL, &s11);
    insert_ok(10, 0, 1);
    TEST_ASSERT_EQUAL_UINT16(2, acts[0].request_id);
}

/* @verifies OBSW-SRS-284 */
void test_disable_holds_activities_enable_releases_overdue(void)
{
    setup_ctx();
    insert_ok(10, 0, 1);
    obsw_tc_t off = make_simple(18, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s11_disable(&off, NULL, &s11));
    TEST_ASSERT_EQUAL_UINT8(0, run_at(20, 0));
    TEST_ASSERT_EQUAL_UINT8(1, s11.count); /* held, not dropped */

    obsw_tc_t on = make_simple(17, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s11_enable(&on, NULL, &s11));
    TEST_ASSERT_EQUAL_UINT8(1, run_at(20, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_insert_stores_activity_with_sequential_ids);
    RUN_TEST(test_insert_rejects_malformed_tc);
    RUN_TEST(test_insert_rejects_oversized_and_nested_tc);
    RUN_TEST(test_insert_when_full_rejected);
    RUN_TEST(test_nothing_released_before_tag);
    RUN_TEST(test_release_in_time_order_ties_by_insertion);
    RUN_TEST(test_past_tag_released_on_next_run);
    RUN_TEST(test_released_tc_goes_through_dispatcher_acceptance);
    RUN_TEST(test_delete_by_request_id_and_all);
    RUN_TEST(test_request_ids_keep_counting_after_delete);
    RUN_TEST(test_disable_holds_activities_enable_releases_overdue);
    return UNITY_END();
}
