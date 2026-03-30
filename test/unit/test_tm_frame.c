#include "unity.h"
#include "obsw/ccsds/tm_frame.h"
#include "obsw/ccsds/tc_frame.h" /* obsw_crc16_ccitt */
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Default config helper                                               */
/* ------------------------------------------------------------------ */

static obsw_tm_frame_config_t default_cfg(void)
{
    obsw_tm_frame_config_t cfg;
    cfg.spacecraft_id = 0x0AB;
    cfg.virtual_channel_id = 0;
    cfg.master_channel_frame_count = 0;
    cfg.virtual_channel_frame_count = 0;
    cfg.enable_fecf = 1;
    cfg.frame_data_field_len = 64;
    return cfg;
}

/* ------------------------------------------------------------------ */
/* Happy path                                                          */
/* ------------------------------------------------------------------ */

void test_build_with_payload(void)
{
    obsw_tm_frame_config_t cfg = default_cfg();
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[128];
    size_t written = 0;

    TEST_ASSERT_EQUAL_INT(OBSW_TM_FRAME_OK,
                          obsw_tm_frame_build(&cfg, payload, sizeof(payload),
                                              buf, sizeof(buf), &written));

    /* Total = 6 hdr + 64 data + 2 CRC = 72 */
    size_t expected = OBSW_TM_FRAME_PRIMARY_HDR_LEN + cfg.frame_data_field_len + OBSW_TM_FRAME_FECF_LEN;
    TEST_ASSERT_EQUAL_size_t(expected, written);

    /* Payload starts at byte 6 */
    TEST_ASSERT_EQUAL_MEMORY(payload, buf + OBSW_TM_FRAME_PRIMARY_HDR_LEN,
                             sizeof(payload));
}

void test_build_no_payload_idle_fill(void)
{
    obsw_tm_frame_config_t cfg = default_cfg();
    uint8_t buf[128];
    size_t written = 0;

    TEST_ASSERT_EQUAL_INT(OBSW_TM_FRAME_OK,
                          obsw_tm_frame_build(&cfg, NULL, 0, buf, sizeof(buf), &written));

    /* Data zone should start with idle packet APID = 0x7FF */
    uint8_t *data = buf + OBSW_TM_FRAME_PRIMARY_HDR_LEN;
    /* byte[0] of idle: version=0, type=TM, no sec hdr, APID[10:8]=0x7 */
    TEST_ASSERT_EQUAL_HEX8(0x07U, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, data[1]);
}

void test_build_fecf_valid(void)
{
    obsw_tm_frame_config_t cfg = default_cfg();
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t buf[128];
    size_t written = 0;

    obsw_tm_frame_build(&cfg, payload, sizeof(payload),
                        buf, sizeof(buf), &written);

    /* Verify the appended CRC matches */
    uint16_t expected_crc = obsw_crc16_ccitt(buf, written - 2U);
    uint16_t appended_crc = (uint16_t)(((uint16_t)buf[written - 2U] << 8) | buf[written - 1U]);
    TEST_ASSERT_EQUAL_HEX16(expected_crc, appended_crc);
}

void test_build_no_fecf(void)
{
    obsw_tm_frame_config_t cfg = default_cfg();
    cfg.enable_fecf = 0;

    uint8_t payload[] = {0xAA};
    uint8_t buf[128];
    size_t written = 0;

    TEST_ASSERT_EQUAL_INT(OBSW_TM_FRAME_OK,
                          obsw_tm_frame_build(&cfg, payload, sizeof(payload),
                                              buf, sizeof(buf), &written));

    /* Total = 6 hdr + 64 data, no CRC */
    TEST_ASSERT_EQUAL_size_t(
        OBSW_TM_FRAME_PRIMARY_HDR_LEN + cfg.frame_data_field_len,
        written);
}

void test_build_frame_count_embedded(void)
{
    obsw_tm_frame_config_t cfg = default_cfg();
    cfg.master_channel_frame_count = 42;
    cfg.virtual_channel_frame_count = 7;

    uint8_t buf[128];
    size_t written = 0;
    obsw_tm_frame_build(&cfg, NULL, 0, buf, sizeof(buf), &written);

    TEST_ASSERT_EQUAL_UINT8(42, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(7, buf[3]);
}

/* ------------------------------------------------------------------ */
/* Error paths                                                         */
/* ------------------------------------------------------------------ */

void test_build_null_cfg(void)
{
    uint8_t buf[128];
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_FRAME_ERR_NULL,
                          obsw_tm_frame_build(NULL, NULL, 0, buf, sizeof(buf), &written));
}

void test_build_null_buf(void)
{
    obsw_tm_frame_config_t cfg = default_cfg();
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_FRAME_ERR_NULL,
                          obsw_tm_frame_build(&cfg, NULL, 0, NULL, 0, &written));
}

void test_build_buf_too_small(void)
{
    obsw_tm_frame_config_t cfg = default_cfg(); /* wants 72 bytes */
    uint8_t buf[10];
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_FRAME_ERR_BUF_SMALL,
                          obsw_tm_frame_build(&cfg, NULL, 0, buf, sizeof(buf), &written));
}

/* ------------------------------------------------------------------ */
/* strerror                                                            */
/* ------------------------------------------------------------------ */

void test_strerror_known_codes(void)
{
    TEST_ASSERT_NOT_NULL(obsw_tm_frame_strerror(OBSW_TM_FRAME_OK));
    TEST_ASSERT_NOT_NULL(obsw_tm_frame_strerror(OBSW_TM_FRAME_ERR_NULL));
    TEST_ASSERT_NOT_NULL(obsw_tm_frame_strerror(OBSW_TM_FRAME_ERR_BUF_SMALL));
    TEST_ASSERT_NOT_NULL(obsw_tm_frame_strerror(OBSW_TM_FRAME_ERR_NO_DATA));
    TEST_ASSERT_NOT_NULL(obsw_tm_frame_strerror(99));
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_with_payload);
    RUN_TEST(test_build_no_payload_idle_fill);
    RUN_TEST(test_build_fecf_valid);
    RUN_TEST(test_build_no_fecf);
    RUN_TEST(test_build_frame_count_embedded);
    RUN_TEST(test_build_null_cfg);
    RUN_TEST(test_build_null_buf);
    RUN_TEST(test_build_buf_too_small);
    RUN_TEST(test_strerror_known_codes);
    return UNITY_END();
}