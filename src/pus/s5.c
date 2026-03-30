#include "obsw/pus/s5.h"
#include "obsw/fdir/fsm.h"
#include <string.h>

int obsw_s5_report(obsw_s5_ctx_t *ctx,
                   obsw_s5_severity_t severity,
                   uint16_t event_id,
                   const uint8_t *data,
                   uint8_t data_len)
{
    if (!ctx)
        return OBSW_PUS_TM_ERR_NULL;
    if (data_len > 0 && !data)
        return OBSW_PUS_TM_ERR_NULL;

    /* TM(5,x) data field: event_id (2 bytes) + auxiliary data */
    uint8_t buf[2U + 255U];
    buf[0] = (uint8_t)(event_id >> 8);
    buf[1] = (uint8_t)(event_id & 0xFFU);
    if (data_len > 0U)
        memcpy(buf + 2U, data, data_len);

    uint16_t seq = ctx->msg_counter++;
    int rc = obsw_pus_tm_build(ctx->tm_store, ctx->apid, seq,
                               5, (uint8_t)severity, seq, 0,
                               ctx->timestamp,
                               buf, (uint16_t)(2U + data_len));

    /* FDIR coupling: HIGH severity + matching event ID → safe mode */
    if (rc == OBSW_PUS_TM_OK &&
        severity == OBSW_S5_HIGH &&
        ctx->fsm != NULL)
    {
        for (uint8_t i = 0; i < ctx->safe_trigger_count; i++)
        {
            if (ctx->safe_trigger_ids[i] == event_id)
            {
                obsw_fsm_to_safe((obsw_fsm_ctx_t *)ctx->fsm);
                break;
            }
        }
    }

    return rc;
}