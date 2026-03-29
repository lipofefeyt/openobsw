#include "obsw/tc/dispatcher.h"
#include <string.h>

/* PUS-C secondary header layout (TC): CRC excluded here, minimal fields */
#define PUS_TC_SEC_HDR_LEN 4U /* PUS version(4b) ack(4b) svc(8b) subsvc(8b) src_id(16b) */

int obsw_tc_dispatcher_init(obsw_tc_dispatcher_t *d,
                            obsw_tc_route_t *table,
                            size_t table_len,
                            obsw_tc_responder_t responder,
                            void *responder_ctx)
{
    if (!d || !table || !responder)
        return OBSW_TC_ERR_NULL;

    d->table = table;
    d->table_len = table_len;
    d->responder = responder;
    d->responder_ctx = responder_ctx;

    return OBSW_TC_OK;
}

int obsw_tc_dispatcher_feed(obsw_tc_dispatcher_t *d,
                            const uint8_t *frame,
                            size_t frame_len)
{
    if (!d || !frame)
        return OBSW_TC_ERR_NULL;

    /* Parse primary header */
    obsw_sp_packet_t pkt;
    if (obsw_sp_parse(frame, frame_len, &pkt) != OBSW_SP_OK)
        return OBSW_TC_ERR_PARSE;

    /* Extract PUS secondary header fields */
    if (!pkt.hdr.sec_hdr_flag || pkt.payload_len < PUS_TC_SEC_HDR_LEN)
        return OBSW_TC_ERR_PARSE;

    obsw_tc_t tc;
    tc.apid = pkt.hdr.apid;
    tc.seq_count = pkt.hdr.seq_count;
    tc.service = pkt.payload[1]; /* byte 0: PUS ver+ack, byte 1: svc */
    tc.subservice = pkt.payload[2];
    /* source ID in bytes 3-4, skip for now */
    tc.user_data = (pkt.payload_len > PUS_TC_SEC_HDR_LEN)
                       ? pkt.payload + PUS_TC_SEC_HDR_LEN
                       : NULL;
    tc.user_data_len = (pkt.payload_len > PUS_TC_SEC_HDR_LEN)
                           ? (uint16_t)(pkt.payload_len - PUS_TC_SEC_HDR_LEN)
                           : 0U;

    /* Route lookup — first match wins */
    for (size_t i = 0; i < d->table_len; i++)
    {
        const obsw_tc_route_t *r = &d->table[i];
        if ((r->apid == 0xFFFFU || r->apid == tc.apid) &&
            r->service == tc.service &&
            r->subservice == tc.subservice)
        {
            int rc = r->handler(&tc, d->responder, r->ctx);
            return (rc == 0) ? OBSW_TC_OK : OBSW_TC_ERR_HANDLER;
        }
    }

    /* No route — send reject */
    d->responder(OBSW_TC_ACK_REJECT, &tc, d->responder_ctx);
    return OBSW_TC_ERR_NO_ROUTE;
}