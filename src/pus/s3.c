#include "obsw/pus/s3.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static obsw_s3_set_t *find_set(obsw_s3_ctx_t *ctx, uint8_t set_id)
{
    for (uint8_t i = 0; i < ctx->set_count; i++)
    {
        if (ctx->sets[i].set_id == set_id)
            return &ctx->sets[i];
    }
    return NULL;
}

static uint16_t serialise_set(const obsw_s3_set_t *set,
                              uint8_t *buf, uint16_t buf_len)
{
    uint16_t offset = 0;
    for (uint8_t i = 0; i < set->param_count; i++)
    {
        const obsw_s3_param_t *p = &set->params[i];
        if (offset + p->size > buf_len)
            return 0;
        memcpy(buf + offset, p->ptr, p->size);
        offset = (uint16_t)(offset + p->size);
    }
    return offset;
}

static int emit_hk_report(obsw_s3_ctx_t *ctx, obsw_s3_set_t *set)
{
    uint8_t buf[OBSW_TM_MAX_PACKET_LEN];
    buf[0] = set->set_id;
    uint16_t param_len = serialise_set(set, buf + 1U,
                                       (uint16_t)(sizeof(buf) - 1U));
    if (param_len == 0 && set->param_count > 0)
        return OBSW_PUS_TM_ERR_TOO_LARGE;

    uint16_t seq = ctx->msg_counter++;
    return obsw_pus_tm_build(ctx->tm_store, ctx->apid, seq,
                             3, 25, seq, 0, ctx->timestamp,
                             buf, (uint16_t)(1U + param_len));
}

/* ------------------------------------------------------------------ */
/* TC handlers                                                         */
/* ------------------------------------------------------------------ */

int obsw_s3_enable(const obsw_tc_t *tc,
                   obsw_tc_responder_t respond,
                   void *ctx)
{
    (void)respond;
    obsw_s3_ctx_t *s = (obsw_s3_ctx_t *)ctx;
    if (!s || !tc)
        return -1;

    if (!tc->user_data || tc->user_data_len < 5U)
    {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_SUBSVC);
        return -1;
    }

    uint8_t set_id = tc->user_data[0];
    uint32_t interval = ((uint32_t)tc->user_data[1] << 24) | ((uint32_t)tc->user_data[2] << 16) | ((uint32_t)tc->user_data[3] << 8) | (uint32_t)tc->user_data[4];

    obsw_s3_set_t *set = find_set(s, set_id);
    if (!set)
    {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    obsw_s1_accept_success(s->s1, tc);
    set->interval_ticks = interval;
    set->countdown = interval;
    set->enabled = true;
    obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s3_disable(const obsw_tc_t *tc,
                    obsw_tc_responder_t respond,
                    void *ctx)
{
    (void)respond;
    obsw_s3_ctx_t *s = (obsw_s3_ctx_t *)ctx;
    if (!s || !tc)
        return -1;

    if (!tc->user_data || tc->user_data_len < 1U)
    {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_SUBSVC);
        return -1;
    }

    obsw_s3_set_t *set = find_set(s, tc->user_data[0]);
    if (!set)
    {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    obsw_s1_accept_success(s->s1, tc);
    set->enabled = false;
    obsw_s1_completion_success(s->s1, tc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */

void obsw_s3_tick(obsw_s3_ctx_t *ctx)
{
    if (!ctx)
        return;
    for (uint8_t i = 0; i < ctx->set_count; i++)
    {
        obsw_s3_set_t *set = &ctx->sets[i];
        if (!set->enabled || set->interval_ticks == 0U)
            continue;
        if (set->countdown > 0U)
            set->countdown--;
        if (set->countdown == 0U)
        {
            emit_hk_report(ctx, set);
            set->countdown = set->interval_ticks;
        }
    }
}

/* ------------------------------------------------------------------ */
/* On-demand report                                                    */
/* ------------------------------------------------------------------ */

int obsw_s3_report(obsw_s3_ctx_t *ctx, uint8_t set_id)
{
    if (!ctx)
        return OBSW_PUS_TM_ERR_NULL;
    obsw_s3_set_t *set = find_set(ctx, set_id);
    if (!set)
        return OBSW_PUS_TM_ERR_NULL;
    return emit_hk_report(ctx, set);
}