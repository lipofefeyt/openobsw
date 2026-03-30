#include "obsw/pus/pus_tm.h"
#include "obsw/ccsds/space_packet.h"
#include <string.h>

int obsw_pus_tm_build(obsw_tm_store_t *store,
                      uint16_t apid,
                      uint16_t seq_count,
                      uint8_t svc,
                      uint8_t subsvc,
                      uint16_t msg_counter,
                      uint16_t dest_id,
                      uint32_t timestamp,
                      const uint8_t *data,
                      uint16_t data_len)
{
    if (!store)
        return OBSW_PUS_TM_ERR_NULL;
    if (data_len > 0 && !data)
        return OBSW_PUS_TM_ERR_NULL;

    /* Total payload = PUS secondary header + application data */
    uint16_t payload_len = (uint16_t)(OBSW_PUS_TM_SEC_HDR_LEN + data_len);
    /* data_len field in primary header = total octets in data field - 1 */
    uint16_t pkt_data_len = (uint16_t)(payload_len - 1U);

    uint16_t total = (uint16_t)(OBSW_SP_PRIMARY_HEADER_LEN + payload_len);
    if (total > OBSW_TM_MAX_PACKET_LEN)
        return OBSW_PUS_TM_ERR_TOO_LARGE;

    uint8_t buf[OBSW_TM_MAX_PACKET_LEN];

    /* ---- Primary header ---- */
    obsw_sp_primary_hdr_t hdr = {
        .version = 0,
        .type = OBSW_SP_TYPE_TM,
        .sec_hdr_flag = 1,
        .apid = apid,
        .seq_flags = OBSW_SP_SEQ_UNSEGMENTED,
        .seq_count = seq_count,
        .data_len = pkt_data_len,
    };
    obsw_sp_encode_primary(&hdr, buf);

    /* ---- PUS-C TM secondary header ---- */
    uint8_t *p = buf + OBSW_SP_PRIMARY_HEADER_LEN;
    *p++ = OBSW_PUS_VERSION;
    *p++ = svc;
    *p++ = subsvc;
    *p++ = (uint8_t)(msg_counter >> 8);
    *p++ = (uint8_t)(msg_counter & 0xFFU);
    *p++ = (uint8_t)(dest_id >> 8);
    *p++ = (uint8_t)(dest_id & 0xFFU);
    *p++ = (uint8_t)(timestamp >> 24);
    *p++ = (uint8_t)(timestamp >> 16);
    *p++ = (uint8_t)(timestamp >> 8);
    *p++ = (uint8_t)(timestamp & 0xFFU);

    /* ---- Application data ---- */
    if (data_len > 0U)
        memcpy(p, data, data_len);

    int rc = obsw_tm_store_enqueue(store, buf, total);
    return (rc == OBSW_TM_OK) ? OBSW_PUS_TM_OK : OBSW_PUS_TM_ERR_STORE;
}