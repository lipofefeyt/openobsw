#include "unity.h"
#include "obsw/ccsds/space_packet.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Encode / decode round-trip                                          */
/* ------------------------------------------------------------------ */

void test_encode_decode_roundtrip(void)
{
    obsw_sp_primary_hdr_t orig = {
        .version = 0,
        .type = OBSW_SP_TYPE_TC,
        .sec_hdr_flag = 1,
        .apid = 0x1AB,
        .seq_flags = OBSW_SP_SEQ_UNSEGMENTED,
        .seq_count = 42,
        .data_len = 9,
    };

    uint8_t buf[6];
    TEST_ASSERT_EQUAL_INT(OBSW_SP_OK, obsw_sp_encode_primary(&orig, buf));

    obsw_sp_primary_hdr_t decoded;
    TEST_ASSERT_EQUAL_INT(OBSW_SP_OK,
                          obsw_sp_decode_primary(buf, sizeof(buf), &decoded));

    TEST_ASSERT_EQUAL_UINT8(orig.version, decoded.version);
    TEST_ASSERT_EQUAL_UINT8(orig.type, decoded.type);
    TEST_ASSERT_EQUAL_UINT8(orig.sec_hdr_flag, decoded.sec_hdr_flag);
    TEST_ASSERT_EQUAL_UINT16(orig.apid, decoded.apid);
    TEST_ASSERT_EQUAL_UINT8(orig.seq_flags, decoded.seq_flags);
    TEST_ASSERT_EQUAL_UINT16(orig.seq_count, decoded.seq_count);
    TEST_ASSERT_EQUAL_UINT16(orig.data_len, decoded.data_len);
}

void test_encode_null_args(void)
{
    uint8_t buf[6];
    obsw_sp_primary_hdr_t hdr = {0};
    TEST_ASSERT_EQUAL_INT(OBSW_SP_ERR_NULL, obsw_sp_encode_primary(NULL, buf));
    TEST_ASSERT_EQUAL_INT(OBSW_SP_ERR_NULL, obsw_sp_encode_primary(&hdr, NULL));
}

void test_decode_buf_too_small(void)
{
    uint8_t buf[5] = {0};
    obsw_sp_primary_hdr_t hdr;
    TEST_ASSERT_EQUAL_INT(OBSW_SP_ERR_BUF_TOO_SMALL,
                          obsw_sp_decode_primary(buf, sizeof(buf), &hdr));
}

void test_apid_max_value(void)
{
    obsw_sp_primary_hdr_t orig = {.apid = 0x7FF};
    uint8_t buf[6];
    obsw_sp_encode_primary(&orig, buf);

    obsw_sp_primary_hdr_t decoded;
    obsw_sp_decode_primary(buf, sizeof(buf), &decoded);
    TEST_ASSERT_EQUAL_UINT16(0x7FF, decoded.apid);
}

void test_seq_count_max_value(void)
{
    obsw_sp_primary_hdr_t orig = {.seq_count = 0x3FFF};
    uint8_t buf[6];
    obsw_sp_encode_primary(&orig, buf);

    obsw_sp_primary_hdr_t decoded;
    obsw_sp_decode_primary(buf, sizeof(buf), &decoded);
    TEST_ASSERT_EQUAL_UINT16(0x3FFF, decoded.seq_count);
}

/* ------------------------------------------------------------------ */
/* Full packet parse                                                   */
/* ------------------------------------------------------------------ */

void test_parse_valid_packet(void)
{
    /* Build a minimal TC packet: 6-byte primary + 4-byte payload */
    obsw_sp_primary_hdr_t hdr = {
        .type = OBSW_SP_TYPE_TC,
        .sec_hdr_flag = 1,
        .apid = 0x010,
        .seq_flags = OBSW_SP_SEQ_UNSEGMENTED,
        .seq_count = 1,
        .data_len = 3, /* payload = 4 bytes (data_len + 1) */
    };

    uint8_t frame[10] = {0};
    obsw_sp_encode_primary(&hdr, frame);
    frame[6] = 0x11; /* PUS ver + ack */
    frame[7] = 17;   /* service  (S17 = are-you-alive) */
    frame[8] = 1;    /* subservice */
    frame[9] = 0x00; /* pad */

    obsw_sp_packet_t pkt;
    TEST_ASSERT_EQUAL_INT(OBSW_SP_OK, obsw_sp_parse(frame, sizeof(frame), &pkt));
    TEST_ASSERT_EQUAL_UINT16(0x010, pkt.hdr.apid);
    TEST_ASSERT_EQUAL_UINT8(17, pkt.payload[1]); /* service */
    TEST_ASSERT_EQUAL_UINT16(4, pkt.payload_len);
}

void test_parse_truncated_packet(void)
{
    obsw_sp_primary_hdr_t hdr = {.data_len = 10};
    uint8_t frame[8] = {0}; /* too short for data_len=10 */
    obsw_sp_encode_primary(&hdr, frame);

    obsw_sp_packet_t pkt;
    TEST_ASSERT_EQUAL_INT(OBSW_SP_ERR_BAD_LENGTH,
                          obsw_sp_parse(frame, sizeof(frame), &pkt));
}

/* ------------------------------------------------------------------ */
/* Total length helper                                                 */
/* ------------------------------------------------------------------ */

void test_total_len(void)
{
    /* data_len field = N means N+1 bytes of data + 6 header bytes */
    TEST_ASSERT_EQUAL_size_t(7, obsw_sp_total_len(0));
    TEST_ASSERT_EQUAL_size_t(11, obsw_sp_total_len(4));
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_encode_null_args);
    RUN_TEST(test_decode_buf_too_small);
    RUN_TEST(test_apid_max_value);
    RUN_TEST(test_seq_count_max_value);
    RUN_TEST(test_parse_valid_packet);
    RUN_TEST(test_parse_truncated_packet);
    RUN_TEST(test_total_len);
    return UNITY_END();
}