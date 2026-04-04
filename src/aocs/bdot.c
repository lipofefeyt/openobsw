#include "obsw/aocs/bdot.h"

#include <math.h>
#include <string.h>

void obsw_bdot_init(obsw_bdot_ctx_t *ctx, const obsw_bdot_config_t *config)
{
    if (!ctx || !config)
        return;
    ctx->config      = *config;
    ctx->initialised = false;
    memset(ctx->b_prev, 0, sizeof(ctx->b_prev));
}

void obsw_bdot_reset(obsw_bdot_ctx_t *ctx)
{
    if (!ctx)
        return;
    ctx->initialised = false;
    memset(ctx->b_prev, 0, sizeof(ctx->b_prev));
}

void obsw_bdot_step(obsw_bdot_ctx_t *ctx, const float b[3], float dt, obsw_bdot_output_t *out)
{
    if (!ctx || !b || !out || dt <= 0.0f)
        return;

    if (!ctx->initialised) {
        /* First measurement — no derivative yet, output zero */
        ctx->b_prev[0]   = b[0];
        ctx->b_prev[1]   = b[1];
        ctx->b_prev[2]   = b[2];
        ctx->initialised = true;
        out->m_cmd[0] = out->m_cmd[1] = out->m_cmd[2] = 0.0f;
        out->dbdt[0] = out->dbdt[1] = out->dbdt[2] = 0.0f;
        return;
    }

    /* Finite difference derivative */
    float dbdt[3];
    dbdt[0] = (b[0] - ctx->b_prev[0]) / dt;
    dbdt[1] = (b[1] - ctx->b_prev[1]) / dt;
    dbdt[2] = (b[2] - ctx->b_prev[2]) / dt;

    /* B-dot law: m_cmd = -k * dB/dt */
    float k = ctx->config.gain;
    float m[3];
    m[0] = -k * dbdt[0];
    m[1] = -k * dbdt[1];
    m[2] = -k * dbdt[2];

    /* Saturate dipole commands */
    float max = ctx->config.max_dipole;
    for (int i = 0; i < 3; i++) {
        if (m[i] > max)
            m[i] = max;
        if (m[i] < -max)
            m[i] = -max;
    }

    out->m_cmd[0] = m[0];
    out->m_cmd[1] = m[1];
    out->m_cmd[2] = m[2];
    out->dbdt[0]  = dbdt[0];
    out->dbdt[1]  = dbdt[1];
    out->dbdt[2]  = dbdt[2];

    /* Update previous measurement */
    ctx->b_prev[0] = b[0];
    ctx->b_prev[1] = b[1];
    ctx->b_prev[2] = b[2];
}