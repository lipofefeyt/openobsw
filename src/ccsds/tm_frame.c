/**
 * @file tm_frame.c
 * @brief TM Transfer Frame builder — CCSDS 132.0-B-2
 */

#include "obsw/ccsds/tm_frame.h"
#include "obsw/ccsds/tc_frame.h" /* reuse obsw_crc16_ccitt */
#include <string.h>

/* ------------------------------------------------------------------ */
/* TM Transfer Frame primary header layout (CCSDS 132.0-B-2 §4.1.2)  */
/*                                                                     */
/* Byte 0: TFVN(2)   | SCID[9:4](6)                                   */
/* Byte 1: SCID[3:0] | VCID(3)    | OCF(1)                            */
/* Byte 2: MC Frame Count (8)                                          */
/* Byte 3: VC Frame Count (8)                                          */
/* Byte 4: SecHdr(1) | Sync(1) | PktOrder(1) | SegLenID(2) | FHP[10:8](3) */
/* Byte 5: FHP[7:0]  (8)                                               */
/* ------------------------------------------------------------------ */

/* First Header Pointer: no packet starts in this frame (idle/fill) */
#define FHP_NO_PACKET 0x07FFU
/* First Header Pointer: packet header starts at offset 0 */
#define FHP_START_ZERO 0x0000U

/* Idle CCSDS space packet APID = 0x7FF, all other fields zero */
static void write_idle_packet(uint8_t *buf, uint16_t len)
{
    if (len < 7U)
        return; /* minimum: 6 header + 1 data byte */
    memset(buf, 0, len);
    /* Primary header: type=TM, APID=0x7FF, seq=unsegmented, data_len=len-7 */
    buf[0] = 0x07U; /* version=0, type=TM, no sec hdr, APID[10:8]=0x7 */
    buf[1] = 0xFFU; /* APID[7:0] */
    buf[2] = 0xC0U; /* seq_flags=0b11 (unsegmented), seq_count=0 */
    buf[3] = 0x00U;
    uint16_t data_len = (uint16_t)(len - 7U);
    buf[4] = (uint8_t)(data_len >> 8);
    buf[5] = (uint8_t)(data_len & 0xFFU);
    /* data field filled with 0x00 (already memset) */
}

int obsw_tm_frame_build(const obsw_tm_frame_config_t *cfg,
                        const uint8_t *payload,
                        uint16_t payload_len,
                        uint8_t *buf,
                        size_t buf_len,
                        size_t *written)
{
    if (!cfg || !buf || !written)
        return OBSW_TM_FRAME_ERR_NULL;
    if (!payload && payload_len)
        return OBSW_TM_FRAME_ERR_NULL;

    uint8_t fecf = cfg->enable_fecf ? 1U : 0U;
    size_t total = (size_t)OBSW_TM_FRAME_PRIMARY_HDR_LEN + cfg->frame_data_field_len + (fecf ? OBSW_TM_FRAME_FECF_LEN : 0U);

    if (buf_len < total)
        return OBSW_TM_FRAME_ERR_BUF_SMALL;

    /* ---- Primary header ---- */
    uint16_t fhp = (payload_len > 0U) ? FHP_START_ZERO : FHP_NO_PACKET;

    buf[0] = (uint8_t)(((cfg->spacecraft_id >> 4) & 0x3FU));
    /* TFVN=0b00 in top 2 bits, SCID[9:4] in lower 6 */
    buf[0] |= (uint8_t)(0x00U); /* TFVN=0 already */

    buf[1] = (uint8_t)(((cfg->spacecraft_id & 0x0FU) << 4) | ((cfg->virtual_channel_id & 0x07U) << 1) | 0x00U); /* OCF flag = 0 */

    buf[2] = cfg->master_channel_frame_count;
    buf[3] = cfg->virtual_channel_frame_count;

    /* Data field status word: no secondary header, not sync, not packet order,
       segment length ID = 0b11 (CCSDS unsegmented), FHP */
    uint16_t dfst = (uint16_t)((0U << 15)   /* no secondary header   */
                               | (0U << 14) /* sync flag = 0         */
                               | (0U << 13) /* packet order = 0      */
                               | (3U << 11) /* segment length = 0b11 */
                               | (fhp & 0x07FFU));
    buf[4] = (uint8_t)(dfst >> 8);
    buf[5] = (uint8_t)(dfst & 0xFFU);

    /* ---- Data field ---- */
    uint8_t *data_zone = buf + OBSW_TM_FRAME_PRIMARY_HDR_LEN;
    uint16_t zone_len = cfg->frame_data_field_len;

    if (payload_len >= zone_len)
    {
        /* Payload fills or overfills the zone — truncate to zone */
        memcpy(data_zone, payload, zone_len);
    }
    else
    {
        if (payload_len > 0U)
            memcpy(data_zone, payload, payload_len);
        /* Fill remainder with an idle space packet */
        uint16_t fill = (uint16_t)(zone_len - payload_len);
        write_idle_packet(data_zone + payload_len, fill);
    }

    /* ---- FECF (CRC-16/CCITT) ---- */
    if (fecf)
    {
        uint16_t crc = obsw_crc16_ccitt(buf, total - OBSW_TM_FRAME_FECF_LEN);
        buf[total - 2U] = (uint8_t)(crc >> 8);
        buf[total - 1U] = (uint8_t)(crc & 0xFFU);
    }

    *written = total;
    return OBSW_TM_FRAME_OK;
}

const char *obsw_tm_frame_strerror(int err)
{
    switch (err)
    {
    case OBSW_TM_FRAME_OK:
        return "OK";
    case OBSW_TM_FRAME_ERR_NULL:
        return "NULL pointer";
    case OBSW_TM_FRAME_ERR_BUF_SMALL:
        return "buffer too small";
    case OBSW_TM_FRAME_ERR_NO_DATA:
        return "no data";
    default:
        return "unknown error";
    }
}