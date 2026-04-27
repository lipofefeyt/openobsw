/**
 * @file test_tc_pipeline.c
 * @brief Integration test: raw TC frame → dispatcher → TM ack → TM frame out.
 *
 * Exercises the full uplink/downlink path end-to-end:
 *   raw bytes → TC frame decode → space packet parse →
 *   dispatcher route → handler → TM store enqueue →
 *   TM store dequeue → TM frame build → raw bytes out
 */
#include "unity.h"
#include "obsw/ccsds/tc_frame.h"
#include "obsw/ccsds/tm_frame.h"
#include "obsw/ccsds/space_packet.h"
#include "obsw/tc/dispatcher.h"
#include "obsw/tm/store.h"
#include <string.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Test state                                                          */
/* ------------------------------------------------------------------ */

static obsw_tm_store_t tm_store;
static int handler_called;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static size_t make_tc_frame_with_pus(uint8_t *buf, size_t buf_len,
                                     uint16_t scid, uint16_t apid,
                                     uint8_t svc, uint8_t subsvc,
                                     uint8_t seq_num)
{
    (void)buf_len;

    /* Build PUS-C TC space packet: 6 primary hdr + 4 secondary hdr */
    uint8_t sp[11]; /* 6 primary + 5 PUS-C secondary */
    obsw_sp_primary_hdr_t hdr = {
        .type = OBSW_SP_TYPE_TC,
        .sec_hdr_flag = 1,
        .apid = apid,
        .seq_flags = OBSW_SP_SEQ_UNSEGMENTED,
        .seq_count = 1,
        .data_len = 4, /* 5 bytes PUS-C secondary header - 1 */
    };
    obsw_sp_encode_primary(&hdr, sp);
    sp[6] = 0x11; /* PUS-C ver + ack flags */
    sp[7] = svc;
    sp[8] = subsvc;
    sp[9] = 0x00;
    sp[10] = 0x00; /* src_id LSB */

    /* Wrap in TC Transfer Frame */
    size_t sp_len = 11;
    size_t total = OBSW_TC_FRAME_PRIMARY_HDR_LEN + sp_len + OBSW_TC_FRAME_CRC_LEN;
    uint16_t flen = (uint16_t)(total - 1U);

    buf[0] = (uint8_t)((scid >> 8) & 0x03U);
    buf[1] = (uint8_t)(scid & 0xFFU);
    buf[2] = (uint8_t)(((flen >> 8) & 0x03U));
    buf[3] = (uint8_t)(flen & 0xFFU);
    buf[4] = seq_num;
    memcpy(buf + OBSW_TC_FRAME_PRIMARY_HDR_LEN, sp, sp_len);

    uint16_t crc = obsw_crc16_ccitt(buf, total - 2U);
    buf[total - 2U] = (uint8_t)(crc >> 8);
    buf[total - 1U] = (uint8_t)(crc & 0xFFU);

    return total;
}

/* Responder: enqueues a minimal TM ack packet into the TM store */
static void pipeline_responder(uint8_t flags, const obsw_tc_t *tc, void *ctx)
{
    (void)ctx;
    /* Build a 7-byte TM ack space packet: 6 hdr + 1 data byte (flags) */
    uint8_t ack[7];
    obsw_sp_primary_hdr_t hdr = {
        .type = OBSW_SP_TYPE_TM,
        .sec_hdr_flag = 0,
        .apid = tc->apid,
        .seq_flags = OBSW_SP_SEQ_UNSEGMENTED,
        .seq_count = tc->seq_count,
        .data_len = 0,
    };
    obsw_sp_encode_primary(&hdr, ack);
    ack[6] = flags;
    obsw_tm_store_enqueue(&tm_store, ack, sizeof(ack));
}

static int s17_handler(const obsw_tc_t *tc,
                       obsw_tc_responder_t respond,
                       void *ctx)
{
    (void)ctx;
    handler_called++;
    respond(OBSW_TC_ACK_ACCEPT | OBSW_TC_ACK_COMPLETE, tc, NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

void test_full_uplink_downlink_pipeline(void)
{
    /* Setup */
    obsw_tm_store_init(&tm_store);
    handler_called = 0;

    obsw_tc_route_t routes[] = {
        {.apid = 0xFFFF, .service = 17, .subservice = 1, .handler = s17_handler, .ctx = NULL},
    };
    obsw_tc_dispatcher_t dispatcher;
    obsw_tc_dispatcher_init(&dispatcher, routes, 1,
                            pipeline_responder, NULL);

    /* 1. Build a raw TC frame for S17/1 on SCID=0x001 */
    uint8_t tc_raw[32];
    size_t tc_len = make_tc_frame_with_pus(tc_raw, sizeof(tc_raw),
                                           0x001, 0x010, 17, 1, 5);

    /* 2. Decode TC frame */
    obsw_tc_frame_t frm;
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_OK,
                          obsw_tc_frame_decode(tc_raw, tc_len, &frm));

    /* 3. Feed space packet to dispatcher */
    TEST_ASSERT_EQUAL_INT(OBSW_TC_OK,
                          obsw_tc_dispatcher_feed(&dispatcher, frm.data, frm.data_len));

    /* 4. Handler was called exactly once */
    TEST_ASSERT_EQUAL_INT(1, handler_called);

    /* 5. TM store has one ack packet */
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&tm_store));

    /* 6. Dequeue and wrap in TM frame */
    uint8_t ack_pkt[32];
    uint16_t ack_len = 0;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_OK,
                          obsw_tm_store_dequeue(&tm_store, ack_pkt, sizeof(ack_pkt), &ack_len));
    TEST_ASSERT_EQUAL_UINT16(7, ack_len);

    obsw_tm_frame_config_t tm_cfg = {
        .spacecraft_id = 0x001,
        .virtual_channel_id = 0,
        .master_channel_frame_count = 0,
        .virtual_channel_frame_count = 0,
        .enable_fecf = 1,
        .frame_data_field_len = 64,
    };
    uint8_t tm_raw[128];
    size_t tm_written = 0;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_FRAME_OK,
                          obsw_tm_frame_build(&tm_cfg, ack_pkt, ack_len,
                                              tm_raw, sizeof(tm_raw), &tm_written));

    /* 7. TM frame is the expected size */
    TEST_ASSERT_EQUAL_size_t(
        OBSW_TM_FRAME_PRIMARY_HDR_LEN + 64U + OBSW_TM_FRAME_FECF_LEN,
        tm_written);

    /* 8. TM store now empty */
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&tm_store));
}

void test_pipeline_bad_crc_rejected(void)
{
    uint8_t tc_raw[32];
    size_t tc_len = make_tc_frame_with_pus(tc_raw, sizeof(tc_raw),
                                           0x001, 0x010, 17, 1, 0);
    /* Corrupt payload */
    tc_raw[7] ^= 0xFFU;

    obsw_tc_frame_t frm;
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_ERR_CRC,
                          obsw_tc_frame_decode(tc_raw, tc_len, &frm));
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_uplink_downlink_pipeline);
    RUN_TEST(test_pipeline_bad_crc_rejected);
    return UNITY_END();
}