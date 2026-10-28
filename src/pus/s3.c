/**
 * @file s3.c
 * @brief PUS-C Service 3 — Housekeeping Reporting implementation.
 *
 * Manages periodic housekeeping sets: TC(3,1) define, TC(3,5) enable, TC(3,6) disable,
 * obsw_s3_tick() drives countdown timers, and TM(3,25) packets are emitted
 * when a set's countdown reaches zero.
 */
#include "obsw/pus/s3.h"
#include "obsw/util/bytes.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/** Find a housekeeping set by ID; returns NULL if not registered. */
static obsw_s3_set_t *find_set(obsw_s3_ctx_t *ctx, uint8_t set_id)
{
    for (uint8_t i = 0; i < ctx->set_count; i++)
    {
        if (ctx->sets[i].set_id == set_id)
        {
            return &ctx->sets[i];
        }
    }
    return NULL;
}

/** Read a native-endian value of @p size bytes and write it big-endian. */
static void put_be(uint8_t *dst, const void *src, obsw_s3_param_size_t size)
{
    uint32_t v = 0;
    if (size == OBSW_S3_PARAM_U8) {
        uint8_t u8;
        (void)memcpy(&u8, src, 1);
        v = u8;
    } else if (size == OBSW_S3_PARAM_U16) {
        uint16_t u16;
        (void)memcpy(&u16, src, 2);
        v = u16;
    } else {
        (void)memcpy(&v, src, 4); /* uint32_t, int32_t or float */
    }
    uint32_t n = (uint32_t)size;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = (uint8_t)(v >> (8U * (n - 1U - i)));
    }
}

/** Serialise all parameters of a set into buf, big-endian (PUS-C);
 *  returns byte count, or 0 on overflow. */
static uint16_t serialise_set(const obsw_s3_set_t *set,
                              uint8_t *buf, uint16_t buf_len)
{
    uint16_t offset = 0;
    for (uint8_t i = 0; i < set->param_count; i++)
    {
        const obsw_s3_param_t *p = &set->params[i];
        if (((uint32_t)offset + (uint32_t)p->size) > buf_len)
        {
            return 0;
        }
        put_be(buf + offset, p->ptr, p->size);
        offset = (uint16_t)(offset + (uint16_t)p->size);
    }
    return offset;
}

/** Emit a TM(3,25) housekeeping report for the given set. */
static int emit_hk_report(obsw_s3_ctx_t *ctx, const obsw_s3_set_t *set)
{
    uint8_t buf[OBSW_TM_MAX_PACKET_LEN];
    buf[0] = set->set_id;
    uint16_t param_len = serialise_set(set, buf + 1U,
                                       (uint16_t)(sizeof(buf) - 1U));
    if (param_len == 0U && set->param_count > 0U)
    {
        return OBSW_PUS_TM_ERR_TOO_LARGE;
    }

    uint16_t seq = ctx->msg_counter++;
    return obsw_pus_tm_build(ctx->tm_store, ctx->apid, seq,
                             3, 25, seq, 0, ctx->timestamp,
                             buf, (uint16_t)(1U + param_len));
}

/** TM(3,25) data bytes available after primary + PUS secondary headers */
#define S3_MAX_REPORT_DATA \
    (OBSW_TM_MAX_PACKET_LEN - OBSW_SP_PRIMARY_HEADER_LEN - OBSW_PUS_TM_SEC_HDR_LEN)

/** Look up an S20 parameter by ID; returns NULL if not in the table. */
static const obsw_s20_param_t *find_param(const obsw_s3_ctx_t *ctx, uint16_t id)
{
    for (uint8_t i = 0; i < ctx->param_table_len; i++)
    {
        if (ctx->param_table[i].param_id == id)
        {
            return &ctx->param_table[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* TC handlers                                                         */
/* ------------------------------------------------------------------ */

int obsw_s3_define(const obsw_tc_t *tc,
                   obsw_tc_responder_t respond,
                   void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s3_ctx_t *s = (obsw_s3_ctx_t *)ctx;
    if (!s || !tc)
    {
        return -1;
    }

    if (!tc->user_data || tc->user_data_len < 6U)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    uint8_t set_id    = tc->user_data[0];
    uint32_t interval = obsw_be32(tc->user_data + 1);
    uint8_t n         = tc->user_data[5];
    const uint8_t *ids = tc->user_data + 6;

    /* Exact length, a fresh SID, and a report that fits one TM(3,25) */
    if (n == 0U || tc->user_data_len != 6U + 2U * (uint16_t)n || find_set(s, set_id) ||
        1U + 4U * (uint32_t)n > S3_MAX_REPORT_DATA)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        if (!find_param(s, obsw_be16(ids + 2U * i)))
        {
            (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
            return -1;
        }
    }

    if (!s->param_pool || s->set_count >= s->set_capacity ||
        s->param_pool_len - s->param_pool_used < n)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);

    obsw_s3_param_t *params = &s->param_pool[s->param_pool_used];
    for (uint8_t i = 0; i < n; i++)
    {
        params[i].ptr  = &find_param(s, obsw_be16(ids + 2U * i))->value;
        params[i].size = OBSW_S3_PARAM_U32;
    }
    s->param_pool_used = (uint16_t)(s->param_pool_used + n);

    obsw_s3_set_t *set  = &s->sets[s->set_count];
    set->set_id         = set_id;
    set->params         = params;
    set->param_count    = n;
    set->interval_ticks = interval;
    set->countdown      = interval;
    set->enabled        = false;
    s->set_count++; /* last, so obsw_s3_tick never sees a half-built set */

    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

/* Pool offset of a set created by TC(3,1), or -1 for a boot-time set.
 * Equality only: the pool and boot-time parameter arrays are distinct
 * objects, so ordering comparisons between them would be undefined. */
static int32_t pool_offset(const obsw_s3_ctx_t *ctx, const obsw_s3_set_t *set)
{
    for (uint16_t k = 0; k < ctx->param_pool_used; k++)
    {
        if (&ctx->param_pool[k] == set->params)
        {
            return (int32_t)k;
        }
    }
    return -1;
}

/* Remove a TC(3,1) set: close its gap in the pool and in the set table */
static void remove_defined(obsw_s3_ctx_t *ctx, const obsw_s3_set_t *set)
{
    uint16_t off = (uint16_t)pool_offset(ctx, set);
    uint16_t n   = set->param_count;
    uint16_t end = (uint16_t)(off + n);

    /* Later TC(3,1) sets sit above this one in the pool: shift them down */
    for (uint8_t i = 0; i < ctx->set_count; i++)
    {
        obsw_s3_set_t *other = &ctx->sets[i];
        int32_t o = (other == set) ? -1 : pool_offset(ctx, other);
        if (o >= (int32_t)end)
        {
            other->params = &ctx->param_pool[(uint16_t)o - n];
        }
    }
    for (uint16_t k = end; k < ctx->param_pool_used; k++)
    {
        ctx->param_pool[k - n] = ctx->param_pool[k];
    }
    ctx->param_pool_used = (uint16_t)(ctx->param_pool_used - n);

    /* Keep the remaining sets in order so reports keep their sequence */
    uint8_t idx = 0U;
    while (&ctx->sets[idx] != set)
    {
        idx++;
    }
    for (uint8_t i = idx; (uint8_t)(i + 1U) < ctx->set_count; i++)
    {
        ctx->sets[i] = ctx->sets[i + 1U];
    }
    ctx->set_count--;
}

int obsw_s3_delete(const obsw_tc_t *tc,
                   obsw_tc_responder_t respond,
                   void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s3_ctx_t *s = (obsw_s3_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL))
    {
        return -1;
    }

    const uint8_t *u = tc->user_data;
    uint8_t n        = ((u != NULL) && (tc->user_data_len >= 1U)) ? u[0] : 0U;
    bool ok          = (n > 0U) && (s->param_pool != NULL) &&
              (tc->user_data_len == (uint16_t)(1U + (uint16_t)n));

    /* Check every set before deleting any */
    for (uint8_t i = 0; ok && (i < n); i++)
    {
        const obsw_s3_set_t *set = find_set(s, u[1U + i]);
        ok = (set != NULL) && !set->enabled && (pool_offset(s, set) >= 0);
        for (uint8_t j = 0; ok && (j < i); j++)
        {
            ok = (u[1U + j] != u[1U + i]);
        }
    }
    if (!ok)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    for (uint8_t i = 0; i < n; i++)
    {
        remove_defined(s, find_set(s, u[1U + i]));
    }
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s3_enable(const obsw_tc_t *tc,
                   obsw_tc_responder_t respond,
                   void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s3_ctx_t *s = (obsw_s3_ctx_t *)ctx;
    if (!s || !tc)
    {
        return -1;
    }

    if (!tc->user_data || tc->user_data_len < 5U)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    uint8_t set_id    = tc->user_data[0];
    uint32_t interval = obsw_be32(tc->user_data + 1);

    obsw_s3_set_t *set = find_set(s, set_id);
    if (!set)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    set->interval_ticks = interval;
    set->countdown = interval;
    set->enabled = true;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s3_disable(const obsw_tc_t *tc,
                    obsw_tc_responder_t respond,
                    void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s3_ctx_t *s = (obsw_s3_ctx_t *)ctx;
    if (!s || !tc)
    {
        return -1;
    }

    if (!tc->user_data || tc->user_data_len < 1U)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    obsw_s3_set_t *set = find_set(s, tc->user_data[0]);
    if (!set)
    {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    set->enabled = false;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */

void obsw_s3_tick(obsw_s3_ctx_t *ctx)
{
    if (!ctx)
    {
        return;
    }
    for (uint8_t i = 0; i < ctx->set_count; i++)
    {
        obsw_s3_set_t *set = &ctx->sets[i];
        if (!set->enabled || set->interval_ticks == 0U)
        {
            continue;
        }
        if (set->countdown > 0U)
        {
            set->countdown--;
        }
        if (set->countdown == 0U)
        {
            (void)emit_hk_report(ctx, set);
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
    {
        return OBSW_PUS_TM_ERR_NULL;
    }
    const obsw_s3_set_t *set = find_set(ctx, set_id);
    if (!set)
    {
        return OBSW_PUS_TM_ERR_NULL;
    }
    return emit_hk_report(ctx, set);
}
