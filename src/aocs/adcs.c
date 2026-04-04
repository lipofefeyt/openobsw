#include "obsw/aocs/adcs.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Quaternion math                                                     */
/* ------------------------------------------------------------------ */

bool obsw_quat_normalise(obsw_quat_t *q)
{
    float norm = sqrtf(q->w * q->w + q->x * q->x + q->y * q->y + q->z * q->z);
    if (norm < 1e-10f)
        return false;
    float inv = 1.0f / norm;
    q->w *= inv;
    q->x *= inv;
    q->y *= inv;
    q->z *= inv;
    return true;
}

obsw_quat_t obsw_quat_conjugate(const obsw_quat_t *q)
{
    obsw_quat_t r = {q->w, -q->x, -q->y, -q->z};
    return r;
}

obsw_quat_t obsw_quat_multiply(const obsw_quat_t *a, const obsw_quat_t *b)
{
    obsw_quat_t r;
    r.w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
    r.x = a->w * b->x + a->x * b->w + a->y * b->z - a->z * b->y;
    r.y = a->w * b->y - a->x * b->z + a->y * b->w + a->z * b->x;
    r.z = a->w * b->z + a->x * b->y - a->y * b->x + a->z * b->w;
    return r;
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

void obsw_adcs_init(obsw_adcs_ctx_t *ctx, const obsw_adcs_config_t *config)
{
    if (!ctx || !config)
        return;
    ctx->config = *config;
    /* Target: identity quaternion */
    ctx->q_cmd = (obsw_quat_t){1.0f, 0.0f, 0.0f, 0.0f};
}

void obsw_adcs_set_target(obsw_adcs_ctx_t *ctx, const obsw_quat_t *q)
{
    if (!ctx || !q)
        return;
    ctx->q_cmd = *q;
    obsw_quat_normalise(&ctx->q_cmd);
}

bool obsw_adcs_step(obsw_adcs_ctx_t *ctx,
                    const obsw_quat_t *q_meas,
                    const float omega[3],
                    obsw_adcs_output_t *out)
{
    if (!ctx || !q_meas || !omega || !out)
        return false;

    /* Normalise measured quaternion */
    obsw_quat_t q_m = *q_meas;
    if (!obsw_quat_normalise(&q_m))
        return false;

    /* Error quaternion: q_err = q_cmd ⊗ q_meas* */
    obsw_quat_t q_mc  = obsw_quat_conjugate(&q_m);
    obsw_quat_t q_err = obsw_quat_multiply(&ctx->q_cmd, &q_mc);
    obsw_quat_normalise(&q_err);

    /* Ensure short-path rotation (w >= 0) */
    if (q_err.w < 0.0f) {
        q_err.w = -q_err.w;
        q_err.x = -q_err.x;
        q_err.y = -q_err.y;
        q_err.z = -q_err.z;
    }

    /* PD torque: τ = -Kp * q_err_vec - Kd * ω */
    float kp = ctx->config.kp;
    float kd = ctx->config.kd;
    float tau[3];
    tau[0] = -kp * q_err.x - kd * omega[0];
    tau[1] = -kp * q_err.y - kd * omega[1];
    tau[2] = -kp * q_err.z - kd * omega[2];

    /* Saturate */
    float max = ctx->config.max_torque;
    for (int i = 0; i < 3; i++) {
        if (tau[i] > max)
            tau[i] = max;
        if (tau[i] < -max)
            tau[i] = -max;
    }

    out->torque_cmd[0] = tau[0];
    out->torque_cmd[1] = tau[1];
    out->torque_cmd[2] = tau[2];
    out->q_err         = q_err;
    out->angle_err_rad = 2.0f * acosf(q_err.w);

    return true;
}