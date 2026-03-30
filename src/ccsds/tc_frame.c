/**
 * @file tc_frame.c
 * @brief TC Transfer Frame decode — CCSDS 232.0-B-4
 */

#include "obsw/ccsds/tc_frame.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* CRC-16/CCITT — poly 0x1021, init 0xFFFF, no final XOR              */
/* ------------------------------------------------------------------ */

uint16_t obsw_crc16_ccitt(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)buf[i] << 8);
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x8000U)
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* TC Transfer Frame primary header layout (CCSDS 232.0-B-4 §4.1.2)  */
/*                                                                     */
/* Byte 0: TFVN(2) | Bypass(1) | CC(1) | Rsvd(2) | SCID[9:8](2)      */
/* Byte 1: SCID[7:0](8)                                                */
/* Byte 2: VCID(6) | FrameLen[9:8](2)                                 */
/* Byte 3: FrameLen[7:0](8)                                            */
/* Byte 4: FrameSeqNum(8)                                              */
/* ------------------------------------------------------------------ */

int obsw_tc_frame_decode(const uint8_t *buf, size_t len,
                         obsw_tc_frame_t *frm)
{
    if (!buf || !frm)
        return OBSW_TC_FRAME_ERR_NULL;
    if (len < OBSW_TC_FRAME_MIN_LEN)
        return OBSW_TC_FRAME_ERR_TOO_SHORT;

    /* CRC check: last 2 bytes are FECF, covers all preceding bytes */
    uint16_t computed = obsw_crc16_ccitt(buf, len - 2U);
    uint16_t received = (uint16_t)(((uint16_t)buf[len - 2U] << 8) | buf[len - 1U]);
    if (computed != received)
        return OBSW_TC_FRAME_ERR_CRC;

    /* Parse primary header */
    obsw_tc_frame_header_t *h = &frm->header;

    h->version = (buf[0] >> 6) & 0x03U;
    h->bypass_flag = (buf[0] >> 5) & 0x01U;
    h->ctrl_cmd_flag = (buf[0] >> 4) & 0x01U;
    /* bits[3:2] reserved */
    h->spacecraft_id = (uint16_t)(((uint16_t)(buf[0] & 0x03U) << 8) | buf[1]);
    h->virtual_channel_id = (buf[2] >> 2) & 0x3FU;
    h->frame_len = (uint16_t)(((uint16_t)(buf[2] & 0x03U) << 8) | buf[3]);
    h->frame_seq_num = buf[4];

    /* frame_len field = total octets - 1 (CCSDS convention) */
    if ((size_t)(h->frame_len + 1U) != len)
        return OBSW_TC_FRAME_ERR_TOO_SHORT;

    frm->data = buf + OBSW_TC_FRAME_PRIMARY_HDR_LEN;
    frm->data_len = (uint16_t)(len - OBSW_TC_FRAME_PRIMARY_HDR_LEN - OBSW_TC_FRAME_CRC_LEN);
    return OBSW_TC_FRAME_OK;
}

const char *obsw_tc_frame_strerror(int err)
{
    switch (err)
    {
    case OBSW_TC_FRAME_OK:
        return "OK";
    case OBSW_TC_FRAME_ERR_NULL:
        return "NULL pointer";
    case OBSW_TC_FRAME_ERR_TOO_SHORT:
        return "frame too short";
    case OBSW_TC_FRAME_ERR_CRC:
        return "CRC mismatch";
    case OBSW_TC_FRAME_ERR_BUF_SMALL:
        return "buffer too small";
    default:
        return "unknown error";
    }
}