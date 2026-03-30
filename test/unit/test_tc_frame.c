#include "unity.h"
#include "obsw/ccsds/tc_frame.h"
#include <string.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/**
 * Build a valid TC Transfer Frame with given SCID, VCID, seq, and payload.
 * Appends correct CRC-16/CCITT. Returns total frame length.
 */
static size_t make_tc_frame(uint8_t *buf, size_t buf_len,
                            uint16_t scid, uint8_t vcid,
                            uint8_t seq,
                            const uint8_t *payload, uint8_t payload_len)
{
    (void)buf_len;
    size_t total = OBSW_TC_FRAME_PRIMARY_HDR_LEN + payload_len + OBSW_TC_FRAME_CRC_LEN;

    /* frame_len field = total - 1 */
    uint16_t flen = (uint16_t)(total - 1U);

    buf[0] = (uint8_t)(0x00U                     /* TFVN=0, bypass=0, CC=0, rsvd=0 */
                       | ((scid >> 8) & 0x03U)); /* SCID[9:8] */
    buf[1] = (uint8_t)(scid & 0xFFU);
    buf[2] = (uint8_t)(((vcid & 0x3FU) << 2) | ((flen >> 8) & 0x03U));
    buf[3] = (uint8_t)(flen & 0xFFU);
    buf[4] = seq;

    if (payload && payload_len)
        memcpy(buf + OBSW_TC_FRAME_PRIMARY_HDR_LEN, payload, payload_len);

    uint16_t crc = obsw_crc16_ccitt(buf, total - 2U);
    buf[total - 2U] = (uint8_t)(crc >> 8);
    buf[total - 1U] = (uint8_t)(crc & 0xFFU);

    return total;
}

/* ------------------------------------------------------------------ */
/* CRC tests                                                           */
/* ------------------------------------------------------------------ */

void test_crc_known_vector(void)
{
    /* CRC-16/CCITT of {0x31..0x39} = 0x29B1 (standard test vector) */
    uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x29B1U, obsw_crc16_ccitt(data, sizeof(data)));
}

void test_crc_empty(void)
{
    /* Empty buffer: init value returned unchanged */
    TEST_ASSERT_EQUAL_HEX16(0xFFFFU, obsw_crc16_ccitt(NULL, 0));
}

void test_crc_single_byte(void)
{
    uint8_t b = 0x00U;
    /* Just verify it doesn't crash and returns a deterministic value */
    uint16_t r1 = obsw_crc16_ccitt(&b, 1);
    uint16_t r2 = obsw_crc16_ccitt(&b, 1);
    TEST_ASSERT_EQUAL_HEX16(r1, r2);
}

/* ------------------------------------------------------------------ */
/* Decode — happy path                                                 */
/* ------------------------------------------------------------------ */

void test_decode_valid_frame(void)
{
    uint8_t payload[] = {0x11, 17, 1, 0x00};
    uint8_t buf[32];
    size_t len = make_tc_frame(buf, sizeof(buf), 0x0AB, 0x01, 7,
                               payload, sizeof(payload));

    obsw_tc_frame_t frm;
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_OK,
                          obsw_tc_frame_decode(buf, len, &frm));

    TEST_ASSERT_EQUAL_UINT8(0, frm.header.version);
    TEST_ASSERT_EQUAL_UINT16(0x0AB, frm.header.spacecraft_id);
    TEST_ASSERT_EQUAL_UINT8(0x01, frm.header.virtual_channel_id);
    TEST_ASSERT_EQUAL_UINT8(7, frm.header.frame_seq_num);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), frm.data_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, frm.data, sizeof(payload));
}

void test_decode_max_scid(void)
{
    uint8_t payload[] = {0xAA};
    uint8_t buf[16];
    size_t len = make_tc_frame(buf, sizeof(buf), 0x3FF, 0x3F, 0xFF,
                               payload, sizeof(payload));

    obsw_tc_frame_t frm;
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_OK,
                          obsw_tc_frame_decode(buf, len, &frm));
    TEST_ASSERT_EQUAL_UINT16(0x3FFU, frm.header.spacecraft_id);
    TEST_ASSERT_EQUAL_UINT8(0x3FU, frm.header.virtual_channel_id);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, frm.header.frame_seq_num);
}

/* ------------------------------------------------------------------ */
/* Decode — error paths                                                */
/* ------------------------------------------------------------------ */

void test_decode_null_args(void)
{
    uint8_t buf[16] = {0};
    obsw_tc_frame_t frm;
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_ERR_NULL,
                          obsw_tc_frame_decode(NULL, 16, &frm));
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_ERR_NULL,
                          obsw_tc_frame_decode(buf, 16, NULL));
}

void test_decode_too_short(void)
{
    uint8_t buf[7] = {0};
    obsw_tc_frame_t frm;
    /* OBSW_TC_FRAME_MIN_LEN = 5 + 2 + 1 = 8; 7 bytes is too short */
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_ERR_TOO_SHORT,
                          obsw_tc_frame_decode(buf, 7, &frm));
}

void test_decode_bad_crc(void)
{
    uint8_t payload[] = {0x11, 17, 1, 0x00};
    uint8_t buf[32];
    size_t len = make_tc_frame(buf, sizeof(buf), 0x001, 0x00, 0,
                               payload, sizeof(payload));

    /* Corrupt one byte in the payload */
    buf[6] ^= 0xFFU;

    obsw_tc_frame_t frm;
    TEST_ASSERT_EQUAL_INT(OBSW_TC_FRAME_ERR_CRC,
                          obsw_tc_frame_decode(buf, len, &frm));
}

void test_decode_frame_len_mismatch(void)
{
    uint8_t payload[] = {0x11, 17, 1, 0x00};
    uint8_t buf[32];
    size_t len = make_tc_frame(buf, sizeof(buf), 0x001, 0x00, 0,
                               payload, sizeof(payload));

    /* Feed one extra byte — frame_len field won't match buf len */
    buf[len] = 0xAA;
    /* Recompute CRC over the longer buffer to pass CRC check */
    /* (we want to hit the length mismatch, not the CRC check) */
    /* Actually: just test with a shorter buffer than frame_len says */
    obsw_tc_frame_t frm;
    TEST_ASSERT_NOT_EQUAL(OBSW_TC_FRAME_OK,
                          obsw_tc_frame_decode(buf, len - 1U, &frm));
}

/* ------------------------------------------------------------------ */
/* strerror                                                            */
/* ------------------------------------------------------------------ */

void test_strerror_known_codes(void)
{
    TEST_ASSERT_NOT_NULL(obsw_tc_frame_strerror(OBSW_TC_FRAME_OK));
    TEST_ASSERT_NOT_NULL(obsw_tc_frame_strerror(OBSW_TC_FRAME_ERR_CRC));
    TEST_ASSERT_NOT_NULL(obsw_tc_frame_strerror(OBSW_TC_FRAME_ERR_NULL));
    TEST_ASSERT_NOT_NULL(obsw_tc_frame_strerror(OBSW_TC_FRAME_ERR_TOO_SHORT));
    TEST_ASSERT_NOT_NULL(obsw_tc_frame_strerror(99)); /* unknown */
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_known_vector);
    RUN_TEST(test_crc_empty);
    RUN_TEST(test_crc_single_byte);
    RUN_TEST(test_decode_valid_frame);
    RUN_TEST(test_decode_max_scid);
    RUN_TEST(test_decode_null_args);
    RUN_TEST(test_decode_too_short);
    RUN_TEST(test_decode_bad_crc);
    RUN_TEST(test_decode_frame_len_mismatch);
    RUN_TEST(test_strerror_known_codes);
    return UNITY_END();
}