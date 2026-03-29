#include "obsw/ccsds/space_packet.h"
#include <string.h>

int obsw_sp_encode_primary(const obsw_sp_primary_hdr_t *hdr, uint8_t *buf)
{
    if (!hdr || !buf)
        return OBSW_SP_ERR_NULL;

    /* Word 0: version(3) | type(1) | sec_hdr_flag(1) | apid(11) */
    buf[0] = (uint8_t)(((hdr->version & 0x07U) << 5) |
                       ((hdr->type & 0x01U) << 4) |
                       ((hdr->sec_hdr_flag & 0x01U) << 3) |
                       ((hdr->apid >> 8) & 0x07U));
    buf[1] = (uint8_t)(hdr->apid & 0xFFU);

    /* Word 1: seq_flags(2) | seq_count(14) */
    buf[2] = (uint8_t)(((hdr->seq_flags & 0x03U) << 6) |
                       ((hdr->seq_count >> 8) & 0x3FU));
    buf[3] = (uint8_t)(hdr->seq_count & 0xFFU);

    /* Word 2: data_len(16) */
    buf[4] = (uint8_t)(hdr->data_len >> 8);
    buf[5] = (uint8_t)(hdr->data_len & 0xFFU);

    return OBSW_SP_OK;
}

int obsw_sp_decode_primary(const uint8_t *buf, size_t buf_len,
                           obsw_sp_primary_hdr_t *hdr)
{
    if (!buf || !hdr)
        return OBSW_SP_ERR_NULL;
    if (buf_len < OBSW_SP_PRIMARY_HEADER_LEN)
        return OBSW_SP_ERR_BUF_TOO_SMALL;

    hdr->version = (buf[0] >> 5) & 0x07U;
    hdr->type = (buf[0] >> 4) & 0x01U;
    hdr->sec_hdr_flag = (buf[0] >> 3) & 0x01U;
    hdr->apid = (uint16_t)(((uint16_t)(buf[0] & 0x07U) << 8) | buf[1]);
    hdr->seq_flags = (buf[2] >> 6) & 0x03U;
    hdr->seq_count = (uint16_t)(((uint16_t)(buf[2] & 0x3FU) << 8) | buf[3]);
    hdr->data_len = (uint16_t)(((uint16_t)buf[4] << 8) | buf[5]);

    if (hdr->version != 0U)
        return OBSW_SP_ERR_BAD_VERSION;

    return OBSW_SP_OK;
}

int obsw_sp_parse(const uint8_t *buf, size_t buf_len, obsw_sp_packet_t *pkt)
{
    if (!buf || !pkt)
        return OBSW_SP_ERR_NULL;

    int rc = obsw_sp_decode_primary(buf, buf_len, &pkt->hdr);
    if (rc != OBSW_SP_OK)
        return rc;

    size_t expected = obsw_sp_total_len(pkt->hdr.data_len);
    if (buf_len < expected)
        return OBSW_SP_ERR_BAD_LENGTH;

    pkt->payload = buf + OBSW_SP_PRIMARY_HEADER_LEN;
    pkt->payload_len = pkt->hdr.data_len + 1U;

    return OBSW_SP_OK;
}