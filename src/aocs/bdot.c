/**
 * @file bdot.c
 * @brief B-dot detumbling controller implementation.
 *
 * Implements the B-dot law: m_cmd = -k * dB/dt, where dB/dt is estimated
 * via finite difference. Used in SAFE mode until angular rates are small
 * enough for the ADCS PD controller to take over.
 */
#include "obsw/aocs/bdot.h"

#include <math.h>
#include <string.h>

void obsw_bdot_init(obsw_bdot_ctx_t *ctx, const obsw_bdot_config_t *config)
{
    if (!ctx || !config)
        return;
    ctx->config      = *config;
    ctx->initialised = false;
    memset(ctx->b_prev,         0, sizeof(ctx->b_prev));
    memset(ctx->dbdt_raw_prev,  0, sizeof(ctx->dbdt_raw_prev));
    memset(ctx->dbdt_filt,      0, sizeof(ctx->dbdt_filt));
}

void obsw_bdot_reset(obsw_bdot_ctx_t *ctx)
{
    if (!ctx)
        return;
    ctx->initialised = false;
    memset(ctx->b_prev,        0, sizeof(ctx->b_prev));
    memset(ctx->dbdt_raw_prev, 0, sizeof(ctx->dbdt_raw_prev));
    memset(ctx->dbdt_filt,     0, sizeof(ctx->dbdt_filt));
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
        out->dbdt_filt[0] = out->dbdt_filt[1] = out->dbdt_filt[2] = 0.0f;
        return;
    }

    /* Finite difference derivative */
    float dbdt[3];
    dbdt[0] = (b[0] - ctx->b_prev[0]) / dt;
    dbdt[1] = (b[1] - ctx->b_prev[1]) / dt;
    dbdt[2] = (b[2] - ctx->b_prev[2]) / dt;

    /* Optional first-order high-pass filter: y[k] = α(y[k-1] + x[k] - x[k-1])
     * Rejects slow orbital-field variation while passing attitude-rate dB/dt.
     * α = τ/(τ+dt); τ=0 disables the filter (dbdt_filt = dbdt). */
    float dbdt_eff[3];
    if (ctx->config.hpf_tau > 0.0f) {
        float alpha = ctx->config.hpf_tau / (ctx->config.hpf_tau + dt);
        for (int i = 0; i < 3; i++) {
            ctx->dbdt_filt[i] = alpha * (ctx->dbdt_filt[i]
                                          + dbdt[i] - ctx->dbdt_raw_prev[i]);
            ctx->dbdt_raw_prev[i] = dbdt[i];
            dbdt_eff[i] = ctx->dbdt_filt[i];
        }
    } else {
        dbdt_eff[0] = dbdt[0];
        dbdt_eff[1] = dbdt[1];
        dbdt_eff[2] = dbdt[2];
    }

    /* B-dot law: m_cmd = -k * dB/dt_eff */
    float k = ctx->config.gain;
    float m[3];
    m[0] = -k * dbdt_eff[0];
    m[1] = -k * dbdt_eff[1];
    m[2] = -k * dbdt_eff[2];

    /* Saturate dipole commands */
    float max = ctx->config.max_dipole;
    for (int i = 0; i < 3; i++) {
        if (m[i] > max)
            m[i] = max;
        if (m[i] < -max)
            m[i] = -max;
    }

    out->m_cmd[0]     = m[0];
    out->m_cmd[1]     = m[1];
    out->m_cmd[2]     = m[2];
    out->dbdt[0]      = dbdt[0];
    out->dbdt[1]      = dbdt[1];
    out->dbdt[2]      = dbdt[2];
    out->dbdt_filt[0] = dbdt_eff[0];
    out->dbdt_filt[1] = dbdt_eff[1];
    out->dbdt_filt[2] = dbdt_eff[2];

    /* Update previous measurement */
    ctx->b_prev[0] = b[0];
    ctx->b_prev[1] = b[1];
    ctx->b_prev[2] = b[2];
}