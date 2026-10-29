/**
 * @file s12.c
 * @brief PUS-C Service 12 — On-Board Monitoring implementation.
 *
 * A fixed table of limit checks on S20 parameters, evaluated each control
 * tick. Out-of-limit transitions are reported as S5 events, latched until
 * the value returns inside the limit.
 */
#include "obsw/pus/s12.h"

#include "obsw/util/bytes.h"

#include <math.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Bit reinterpretation through the S20 value union (deviation 19.2) */
static float be_f32(const uint8_t *p)
{
    obsw_s20_value_t v;
    v.u32 = obsw_be32(p);
    return v.f32;
}

static obsw_s12_def_t *find_def(obsw_s12_ctx_t *s, uint16_t param_id)
{
    for (uint8_t i = 0; i < s->count; i++) {
        if (s->defs[i].param_id == param_id) {
            return &s->defs[i];
        }
    }
    return NULL;
}

static void report(obsw_s12_ctx_t *s, const obsw_s12_def_t *d, uint16_t event_id, float value)
{
    obsw_s20_value_t v;
    v.f32 = value;
    const uint8_t aux[4] = {(uint8_t)(v.u32 >> 24), (uint8_t)(v.u32 >> 16), (uint8_t)(v.u32 >> 8),
                            (uint8_t)v.u32};
    (void)obsw_s5_report(s->s5, d->severity, event_id, aux, (uint8_t)sizeof(aux));
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void obsw_s12_init(obsw_s12_ctx_t *ctx,
                   obsw_s1_ctx_t *s1,
                   obsw_s5_ctx_t *s5,
                   const obsw_s20_param_t *params,
                   uint8_t param_count,
                   obsw_s12_def_t *defs,
                   uint8_t capacity)
{
    if (ctx == NULL) {
        return;
    }
    ctx->s1          = s1;
    ctx->s5          = s5;
    ctx->params      = params;
    ctx->param_count = param_count;
    ctx->defs        = defs;
    ctx->capacity    = (defs != NULL) ? capacity : 0U;
    ctx->count       = 0U;
    ctx->enabled     = true;
}

/* ------------------------------------------------------------------ */
/* TC handlers                                                         */
/* ------------------------------------------------------------------ */

int obsw_s12_enable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s12_ctx_t *s = (obsw_s12_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    for (uint8_t i = 0; i < s->count; i++) {
        s->defs[i].in_low  = false;
        s->defs[i].in_high = false;
    }
    s->enabled = true;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s12_disable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s12_ctx_t *s = (obsw_s12_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->enabled = false;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s12_add(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s12_ctx_t *s = (obsw_s12_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    if ((tc->user_data == NULL) || (tc->user_data_len < OBSW_S12_ADD_LEN)) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    const uint8_t *u  = tc->user_data;
    uint16_t param_id = obsw_be16(u);
    float low         = be_f32(u + 2);
    float high        = be_f32(u + 6);
    uint8_t severity  = u[14];

    bool bad = (obsw_s20_find(s->params, s->param_count, param_id) == NULL) ||
               (severity < (uint8_t)OBSW_S5_INFO) || (severity > (uint8_t)OBSW_S5_HIGH) ||
               ((isnan(low) == 0) && (isnan(high) == 0) && (low > high));
    if (bad) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    obsw_s12_def_t *d = find_def(s, param_id);
    if ((d == NULL) && (s->count >= s->capacity)) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    if (d == NULL) {
        d = &s->defs[s->count];
        s->count++;
    }
    d->param_id   = param_id;
    d->low        = low;
    d->high       = high;
    d->event_low  = obsw_be16(u + 10);
    d->event_high = obsw_be16(u + 12);
    d->severity   = (obsw_s5_severity_t)severity;
    d->in_low     = false;
    d->in_high    = false;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s12_delete(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s12_ctx_t *s = (obsw_s12_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    if ((tc->user_data == NULL) || (tc->user_data_len < OBSW_S12_DELETE_LEN)) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }
    obsw_s12_def_t *d = find_def(s, obsw_be16(tc->user_data));
    if (d == NULL) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    /* Keep the table packed: move the last definition into the hole */
    s->count--;
    *d = s->defs[s->count];
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s12_delete_all(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s12_ctx_t *s = (obsw_s12_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->count = 0U;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Periodic check                                                      */
/* ------------------------------------------------------------------ */

void obsw_s12_check(obsw_s12_ctx_t *ctx)
{
    if ((ctx == NULL) || !ctx->enabled) {
        return;
    }
    for (uint8_t i = 0; i < ctx->count; i++) {
        obsw_s12_def_t *d = &ctx->defs[i];
        const obsw_s20_param_t *p = obsw_s20_find(ctx->params, ctx->param_count, d->param_id);
        if (p == NULL) {
            continue;
        }
        float v = obsw_s20_as_float(p);

        /* Comparisons with a NaN limit are false: that check never trips */
        if (v < d->low) {
            if (!d->in_low) {
                d->in_low = true;
                report(ctx, d, d->event_low, v);
            }
        } else {
            d->in_low = false;
        }

        if (v > d->high) {
            if (!d->in_high) {
                d->in_high = true;
                report(ctx, d, d->event_high, v);
            }
        } else {
            d->in_high = false;
        }
    }
}
