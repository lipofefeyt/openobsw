#include "obsw/ccsds/space_packet.h"
#include "obsw/pus/s19.h"
#include "obsw/pus/s5.h"
#include "obsw/tm/store.h"
#include "unity.h"

#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define EV_A 0x0201U
#define EV_B 0x0202U

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s5_ctx_t s5;
static obsw_s19_ctx_t s19;
static obsw_s19_def_t defs[2];
static obsw_tc_dispatcher_t disp;

/* Action target: counts calls and records the subservice it saw */
static int action_calls;
static uint8_t action_sub;
static int action_handler(const obsw_tc_t *tc, obsw_tc_responder_t r, void *ctx)
{
    (void)r;
    (void)ctx;
    action_calls++;
    action_sub = tc->subservice;
    return 0;
}

static obsw_tc_route_t routes[] = {
    {.apid = 0xFFFF, .service = 17, .subservice = 1, .handler = action_handler, .ctx = NULL},
    {.apid = 0xFFFF, .service = 17, .subservice = 3, .handler = action_handler, .ctx = NULL},
    {.apid = 0xFFFF, .service = 19, .subservice = 1, .handler = obsw_s19_add, .ctx = &s19},
};

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);
    memset(&s1, 0, sizeof(s1));
    s1.tm_store = &store;
    memset(&s5, 0, sizeof(s5));
    s5.tm_store = &store;
    obsw_s19_init(&s19, &s1, defs, 2);
    s5.on_event     = obsw_s19_on_event;
    s5.on_event_ctx = &s19;
    obsw_tc_dispatcher_init(&disp, routes, 3, obsw_s1_dispatch_responder, &s1);
    action_calls = 0;
    action_sub   = 0;
}

/* Build a PUS-C TC space packet svc/sub with n bytes of app data */
static uint16_t make_pkt(uint8_t *buf, uint8_t svc, uint8_t sub, uint8_t n)
{
    obsw_sp_primary_hdr_t h = {.type         = OBSW_SP_TYPE_TC,
                               .sec_hdr_flag = 1,
                               .apid         = 0x010,
                               .seq_flags    = OBSW_SP_SEQ_UNSEGMENTED,
                               .seq_count    = 7,
                               .data_len     = (uint16_t)(4U + n)};
    obsw_sp_encode_primary(&h, buf);
    buf[6]  = 0x11;
    buf[7]  = svc;
    buf[8]  = sub;
    buf[9]  = 0;
    buf[10] = 0;
    memset(buf + 11, 0xA5, n);
    return (uint16_t)(11U + n);
}

static uint8_t ud[96];

/* TC(19,1) user data: event_id | action packet */
static obsw_tc_t make_add(uint16_t event_id, uint8_t svc, uint8_t sub, uint8_t n)
{
    ud[0]        = (uint8_t)(event_id >> 8);
    ud[1]        = (uint8_t)event_id;
    uint16_t len = make_pkt(ud + 2, svc, sub, n);
    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 19,
                    .subservice    = 1,
                    .seq_count     = 3,
                    .user_data     = ud,
                    .user_data_len = (uint16_t)(len + 2U)};
    return tc;
}

static void add_ok(uint16_t event_id, uint8_t svc, uint8_t sub)
{
    obsw_tc_t tc = make_add(event_id, svc, sub, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s19_add(&tc, NULL, &s19));
    obsw_tm_store_init(&store);
}

static obsw_tc_t make_simple(uint8_t sub, uint16_t arg, uint16_t len)
{
    ud[0]        = (uint8_t)(arg >> 8);
    ud[1]        = (uint8_t)arg;
    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 19,
                    .subservice    = sub,
                    .seq_count     = 4,
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

/* ---- Definitions ---------------------------------------------------- */

/* @verifies OBSW-SRS-300, OBSW-SRS-082 */
void test_add_stores_action(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(EV_A, 17, 1, 3);
    TEST_ASSERT_EQUAL_INT(0, obsw_s19_add(&tc, NULL, &s19));
    TEST_ASSERT_EQUAL_UINT8(1, s19.count);
    TEST_ASSERT_EQUAL_HEX16(EV_A, defs[0].event_id);
    TEST_ASSERT_EQUAL_UINT16(14, defs[0].action_len);
    TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store)); /* TM(1,1), TM(1,7) */
}

/* @verifies OBSW-SRS-300 */
void test_add_same_event_replaces(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    add_ok(EV_A, 17, 3);
    TEST_ASSERT_EQUAL_UINT8(1, s19.count);
    TEST_ASSERT_EQUAL_UINT8(3, defs[0].action[8]);
}

/* @verifies OBSW-SRS-301, OBSW-SRS-083 */
void test_add_rejects_malformed_action(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(EV_A, 17, 1, 0);
    tc.user_data_len = 2U + 10U; /* shorter than a PUS TC */
    TEST_ASSERT_NOT_EQUAL(0, obsw_s19_add(&tc, NULL, &s19));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);

    obsw_tm_store_init(&store);
    tc = make_add(EV_A, 17, 1, 2);
    tc.user_data_len--; /* packet length field no longer matches */
    TEST_ASSERT_NOT_EQUAL(0, obsw_s19_add(&tc, NULL, &s19));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
    TEST_ASSERT_EQUAL_UINT8(0, s19.count);
}

/* @verifies OBSW-SRS-301 */
void test_add_rejects_oversized_action(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(EV_A, 17, 1, (uint8_t)(OBSW_S19_ACTION_MAX_LEN - 10U));
    TEST_ASSERT_NOT_EQUAL(0, obsw_s19_add(&tc, NULL, &s19));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-301 */
void test_add_rejects_s19_action(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(EV_A, 19, 3, 0);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s19_add(&tc, NULL, &s19));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-301 */
void test_add_when_full_rejected(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    add_ok(EV_B, 17, 1);
    obsw_tc_t tc = make_add(0x0303, 17, 1, 0);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s19_add(&tc, NULL, &s19));
    expect_rejected(OBSW_S1_FAIL_EXEC_ERROR);
}

/* @verifies OBSW-SRS-303 */
void test_delete_one_and_all(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    add_ok(EV_B, 17, 3);
    obsw_tc_t del = make_simple(2, EV_A, 2);
    TEST_ASSERT_EQUAL_INT(0, obsw_s19_delete(&del, NULL, &s19));
    TEST_ASSERT_EQUAL_UINT8(1, s19.count);
    TEST_ASSERT_EQUAL_HEX16(EV_B, defs[0].event_id);

    obsw_tm_store_init(&store);
    del = make_simple(2, 0x7777, 2);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s19_delete(&del, NULL, &s19));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);

    obsw_tc_t all = make_simple(3, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s19_delete_all(&all, NULL, &s19));
    TEST_ASSERT_EQUAL_UINT8(0, s19.count);
}

/* ---- Reaction -------------------------------------------------------- */

/* @verifies OBSW-SRS-302 */
void test_event_queues_action_and_run_dispatches_it(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 3);
    obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, action_calls); /* deferred, not re-entrant */
    TEST_ASSERT_EQUAL_UINT8(1, obsw_s19_run(&s19, &disp));
    TEST_ASSERT_EQUAL_INT(1, action_calls);
    TEST_ASSERT_EQUAL_UINT8(3, action_sub);
    TEST_ASSERT_EQUAL_UINT8(0, obsw_s19_run(&s19, &disp)); /* queue drained */
}

/* @verifies OBSW-SRS-302 */
void test_unrelated_event_does_nothing(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    obsw_s5_report(&s5, OBSW_S5_HIGH, EV_B, NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(0, obsw_s19_run(&s19, &disp));
}

/* @verifies OBSW-SRS-302 */
void test_event_raised_even_when_tm_store_full(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    uint8_t pkt[8] = {0};
    while (obsw_tm_store_enqueue(&store, pkt, sizeof(pkt)) == OBSW_TM_OK) {
    }
    obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(1, obsw_s19_run(&s19, &disp));
}

/* @verifies OBSW-SRS-302 */
void test_action_deleted_before_run_is_skipped(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);
    obsw_tc_t del = make_simple(2, EV_A, 2);
    obsw_s19_delete(&del, NULL, &s19);
    TEST_ASSERT_EQUAL_UINT8(0, obsw_s19_run(&s19, &disp));
    TEST_ASSERT_EQUAL_INT(0, action_calls);
}

/* @verifies OBSW-SRS-305 */
void test_queue_overflow_counts_drops(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    for (uint8_t i = 0; i < OBSW_S19_QUEUE_LEN + 2U; i++) {
        obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);
    }
    TEST_ASSERT_EQUAL_UINT16(2, s19.dropped);
    TEST_ASSERT_EQUAL_UINT8(OBSW_S19_QUEUE_LEN, obsw_s19_run(&s19, &disp));
}

/* @verifies OBSW-SRS-304 */
void test_disable_stops_reactions_enable_resumes(void)
{
    setup_ctx();
    add_ok(EV_A, 17, 1);
    obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);

    obsw_tc_t off = make_simple(5, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s19_disable(&off, NULL, &s19));
    TEST_ASSERT_EQUAL_UINT8(0, obsw_s19_run(&s19, &disp)); /* queued action dropped */
    obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(0, obsw_s19_run(&s19, &disp));

    obsw_tc_t on = make_simple(4, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s19_enable(&on, NULL, &s19));
    obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(1, obsw_s19_run(&s19, &disp));
}

/* @verifies OBSW-SRS-302 */
void test_action_goes_through_dispatcher_acceptance(void)
{
    setup_ctx();
    add_ok(EV_A, 42, 1); /* no route for service 42 */
    obsw_s5_report(&s5, OBSW_S5_LOW, EV_A, NULL, 0);
    obsw_tm_store_init(&store);
    obsw_s19_run(&s19, &disp);
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_SVC);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_stores_action);
    RUN_TEST(test_add_same_event_replaces);
    RUN_TEST(test_add_rejects_malformed_action);
    RUN_TEST(test_add_rejects_oversized_action);
    RUN_TEST(test_add_rejects_s19_action);
    RUN_TEST(test_add_when_full_rejected);
    RUN_TEST(test_delete_one_and_all);
    RUN_TEST(test_event_queues_action_and_run_dispatches_it);
    RUN_TEST(test_unrelated_event_does_nothing);
    RUN_TEST(test_event_raised_even_when_tm_store_full);
    RUN_TEST(test_action_deleted_before_run_is_skipped);
    RUN_TEST(test_queue_overflow_counts_drops);
    RUN_TEST(test_disable_stops_reactions_enable_resumes);
    RUN_TEST(test_action_goes_through_dispatcher_acceptance);
    return UNITY_END();
}
