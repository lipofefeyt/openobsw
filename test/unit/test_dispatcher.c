#include "unity.h"
#include "obsw/tc/dispatcher.h"
#include "obsw/ccsds/space_packet.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Test infrastructure                                                 */
/* ------------------------------------------------------------------ */

static uint8_t last_ack_flags;
static uint16_t last_ack_apid;
static int handler_call_count;

static void test_responder(uint8_t flags, const obsw_tc_t *tc, void *ctx)
{
    (void)ctx;
    last_ack_flags = flags;
    last_ack_apid = tc->apid;
}

static int noop_handler(const obsw_tc_t *tc,
                        obsw_tc_responder_t respond,
                        void *ctx)
{
    (void)ctx;
    handler_call_count++;
    respond(OBSW_TC_ACK_ACCEPT | OBSW_TC_ACK_COMPLETE, tc, NULL);
    return 0;
}

static int failing_handler(const obsw_tc_t *tc,
                           obsw_tc_responder_t respond,
                           void *ctx)
{
    (void)tc;
    (void)respond;
    (void)ctx;
    return -1;
}

/* Build a minimal PUS-C TC frame for svc/subsvc on apid */
static size_t make_tc_frame(uint8_t *buf, size_t buf_len,
                            uint16_t apid, uint8_t svc, uint8_t subsvc)
{
    (void)buf_len;
    obsw_sp_primary_hdr_t hdr = {
        .type = OBSW_SP_TYPE_TC,
        .sec_hdr_flag = 1,
        .apid = apid,
        .seq_flags = OBSW_SP_SEQ_UNSEGMENTED,
        .seq_count = 1,
        .data_len = 3, /* 4 bytes of PUS secondary header */
    };
    obsw_sp_encode_primary(&hdr, buf);
    buf[6] = 0x11; /* PUS-C version + ack flags */
    buf[7] = svc;
    buf[8] = subsvc;
    buf[9] = 0x00;
    return 10;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

void test_init_null_args(void)
{
    obsw_tc_dispatcher_t d;
    obsw_tc_route_t table[1];
    TEST_ASSERT_EQUAL_INT(OBSW_TC_ERR_NULL,
                          obsw_tc_dispatcher_init(NULL, table, 1, test_responder, NULL));
    TEST_ASSERT_EQUAL_INT(OBSW_TC_ERR_NULL,
                          obsw_tc_dispatcher_init(&d, NULL, 1, test_responder, NULL));
    TEST_ASSERT_EQUAL_INT(OBSW_TC_ERR_NULL,
                          obsw_tc_dispatcher_init(&d, table, 1, NULL, NULL));
}

void test_dispatch_matching_route(void)
{
    handler_call_count = 0;

    obsw_tc_route_t table[] = {
        {.apid = 0x010, .service = 17, .subservice = 1, .handler = noop_handler, .ctx = NULL},
    };
    obsw_tc_dispatcher_t d;
    obsw_tc_dispatcher_init(&d, table, 1, test_responder, NULL);

    uint8_t frame[16];
    size_t len = make_tc_frame(frame, sizeof(frame), 0x010, 17, 1);

    TEST_ASSERT_EQUAL_INT(OBSW_TC_OK, obsw_tc_dispatcher_feed(&d, frame, len));
    TEST_ASSERT_EQUAL_INT(1, handler_call_count);
    TEST_ASSERT_EQUAL_UINT8(OBSW_TC_ACK_ACCEPT | OBSW_TC_ACK_COMPLETE,
                            last_ack_flags);
}

void test_dispatch_no_route(void)
{
    obsw_tc_route_t table[] = {
        {.apid = 0x010, .service = 17, .subservice = 1, .handler = noop_handler, .ctx = NULL},
    };
    obsw_tc_dispatcher_t d;
    obsw_tc_dispatcher_init(&d, table, 1, test_responder, NULL);

    uint8_t frame[16];
    /* send svc 3 — not in table */
    size_t len = make_tc_frame(frame, sizeof(frame), 0x010, 3, 25);

    TEST_ASSERT_EQUAL_INT(OBSW_TC_ERR_NO_ROUTE,
                          obsw_tc_dispatcher_feed(&d, frame, len));
    TEST_ASSERT_EQUAL_UINT8(OBSW_TC_ACK_REJECT, last_ack_flags);
}

void test_dispatch_wildcard_apid(void)
{
    handler_call_count = 0;

    obsw_tc_route_t table[] = {
        {.apid = 0xFFFF, .service = 17, .subservice = 1, .handler = noop_handler, .ctx = NULL},
    };
    obsw_tc_dispatcher_t d;
    obsw_tc_dispatcher_init(&d, table, 1, test_responder, NULL);

    uint8_t frame[16];
    size_t len = make_tc_frame(frame, sizeof(frame), 0x7FF, 17, 1);

    TEST_ASSERT_EQUAL_INT(OBSW_TC_OK, obsw_tc_dispatcher_feed(&d, frame, len));
    TEST_ASSERT_EQUAL_INT(1, handler_call_count);
}

void test_dispatch_handler_error(void)
{
    obsw_tc_route_t table[] = {
        {.apid = 0x010, .service = 5, .subservice = 1, .handler = failing_handler, .ctx = NULL},
    };
    obsw_tc_dispatcher_t d;
    obsw_tc_dispatcher_init(&d, table, 1, test_responder, NULL);

    uint8_t frame[16];
    size_t len = make_tc_frame(frame, sizeof(frame), 0x010, 5, 1);

    TEST_ASSERT_EQUAL_INT(OBSW_TC_ERR_HANDLER,
                          obsw_tc_dispatcher_feed(&d, frame, len));
}

void test_dispatch_first_match_wins(void)
{
    handler_call_count = 0;

    obsw_tc_route_t table[] = {
        {.apid = 0x010, .service = 17, .subservice = 1, .handler = noop_handler, .ctx = NULL},
        {.apid = 0x010, .service = 17, .subservice = 1, .handler = noop_handler, .ctx = NULL},
    };
    obsw_tc_dispatcher_t d;
    obsw_tc_dispatcher_init(&d, table, 2, test_responder, NULL);

    uint8_t frame[16];
    size_t len = make_tc_frame(frame, sizeof(frame), 0x010, 17, 1);

    obsw_tc_dispatcher_feed(&d, frame, len);
    /* Only the first entry should fire */
    TEST_ASSERT_EQUAL_INT(1, handler_call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_null_args);
    RUN_TEST(test_dispatch_matching_route);
    RUN_TEST(test_dispatch_no_route);
    RUN_TEST(test_dispatch_wildcard_apid);
    RUN_TEST(test_dispatch_handler_error);
    RUN_TEST(test_dispatch_first_match_wins);
    return UNITY_END();
}