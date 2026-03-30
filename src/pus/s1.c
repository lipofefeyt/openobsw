#include "obsw/pus/s1.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* TM(1,x) data field layout                                           */
/*                                                                     */
/* All S1 reports embed the original TC packet ID + sequence control  */
/* as the first 4 bytes of the TM data field.                         */
/*                                                                     */
/* Byte 0-1: TC Packet ID    (APID + type + sec_hdr_flag)             */
/* Byte 2-3: TC Seq Control  (seq_flags + seq_count)                  */
/* Byte 4-5: Failure code    (success reports omit this)              */
/* ------------------------------------------------------------------ */

static void encode_tc_ref(const obsw_tc_t *tc, uint8_t *buf)
{
    uint16_t pkt_id = (uint16_t)((1U << 12) | (1U << 11) | (tc->apid & 0x7FFU));
    buf[0] = (uint8_t)(pkt_id >> 8);
    buf[1] = (uint8_t)(pkt_id & 0xFFU);
    uint16_t seq = (uint16_t)((0x3U << 14) | (tc->seq_count & 0x3FFFU));
    buf[2] = (uint8_t)(seq >> 8);
    buf[3] = (uint8_t)(seq & 0xFFU);
}

int obsw_s1_accept_success(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc)
{
    if (!ctx || !tc)
        return OBSW_PUS_TM_ERR_NULL;
    uint8_t data[4];
    encode_tc_ref(tc, data);
    uint16_t seq = ctx->msg_counter++;
    return obsw_pus_tm_build(ctx->tm_store, ctx->apid, seq,
                             1, 1, seq, 0, ctx->timestamp,
                             data, sizeof(data));
}

int obsw_s1_accept_failure(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc,
                           uint16_t failure_code)
{
    if (!ctx || !tc)
        return OBSW_PUS_TM_ERR_NULL;
    uint8_t data[6];
    encode_tc_ref(tc, data);
    data[4] = (uint8_t)(failure_code >> 8);
    data[5] = (uint8_t)(failure_code & 0xFFU);
    uint16_t seq = ctx->msg_counter++;
    return obsw_pus_tm_build(ctx->tm_store, ctx->apid, seq,
                             1, 2, seq, 0, ctx->timestamp,
                             data, sizeof(data));
}

int obsw_s1_completion_success(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc)
{
    if (!ctx || !tc)
        return OBSW_PUS_TM_ERR_NULL;
    uint8_t data[4];
    encode_tc_ref(tc, data);
    uint16_t seq = ctx->msg_counter++;
    return obsw_pus_tm_build(ctx->tm_store, ctx->apid, seq,
                             1, 7, seq, 0, ctx->timestamp,
                             data, sizeof(data));
}

int obsw_s1_completion_failure(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc,
                               uint16_t failure_code)
{
    if (!ctx || !tc)
        return OBSW_PUS_TM_ERR_NULL;
    uint8_t data[6];
    encode_tc_ref(tc, data);
    data[4] = (uint8_t)(failure_code >> 8);
    data[5] = (uint8_t)(failure_code & 0xFFU);
    uint16_t seq = ctx->msg_counter++;
    return obsw_pus_tm_build(ctx->tm_store, ctx->apid, seq,
                             1, 8, seq, 0, ctx->timestamp,
                             data, sizeof(data));
}