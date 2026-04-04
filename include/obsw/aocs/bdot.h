/**
 * @file bdot.h
 * @brief B-dot detumbling controller.
 *
 * Implements the classical B-dot algorithm for safe mode detumbling:
 *
 *   m_cmd = -k * dB/dt
 *
 * where:
 *   m_cmd  = magnetorquer dipole command [A·m²]
 *   k      = controller gain [A·m²·s/T]
 *   dB/dt  = magnetic field time derivative [T/s]
 *
 * The derivative is approximated by finite difference:
 *   dB/dt ≈ (B_now - B_prev) / dt
 *
 * Only requires a magnetometer — no attitude knowledge needed.
 * This is appropriate for safe mode where the star tracker may be
 * unavailable or the spacecraft may be tumbling.
 *
 * References:
 *   Markley & Crassidis, "Fundamentals of Spacecraft Attitude Determination
 *   and Control", Springer 2014, §7.3.
 */
#ifndef OBSW_AOCS_BDOT_H
#define OBSW_AOCS_BDOT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    float gain;       /**< Controller gain k [A·m²·s/T]           */
    float max_dipole; /**< Saturation limit [A·m²]                 */
} obsw_bdot_config_t;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    obsw_bdot_config_t config;
    float b_prev[3];  /**< Previous B-field measurement [T]        */
    bool initialised; /**< True after first measurement            */
} obsw_bdot_ctx_t;

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    float m_cmd[3]; /**< Dipole command [A·m²] (x, y, z)        */
    float dbdt[3];  /**< Estimated dB/dt [T/s]                   */
} obsw_bdot_output_t;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/**
 * Initialise B-dot controller with given configuration.
 */
void obsw_bdot_init(obsw_bdot_ctx_t *ctx, const obsw_bdot_config_t *config);

/**
 * Reset the controller state (clears previous B-field).
 * Call when re-entering safe mode.
 */
void obsw_bdot_reset(obsw_bdot_ctx_t *ctx);

/**
 * Run one B-dot step.
 *
 * @param ctx    Controller state.
 * @param b      Current magnetometer measurement [T] (3-element array).
 * @param dt     Timestep [s].
 * @param out    Output dipole commands and derivative estimate.
 */
void obsw_bdot_step(obsw_bdot_ctx_t *ctx, const float b[3], float dt, obsw_bdot_output_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_AOCS_BDOT_H */