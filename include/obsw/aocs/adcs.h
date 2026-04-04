/**
 * @file adcs.h
 * @brief Attitude determination and PD control for nominal mode.
 *
 * Nominal mode AOCS:
 *   - Attitude: quaternion directly from star tracker
 *   - Rate:     angular velocity from gyro
 *   - Control:  PD controller on error quaternion → RW torque commands
 *
 * Error quaternion (multiplicative):
 *   q_err = q_cmd ⊗ q_meas*    (q_meas* = conjugate)
 *
 * PD torque command:
 *   τ_cmd = -Kp * q_err_vec - Kd * ω
 *
 * where q_err_vec is the vector part of q_err (x, y, z components).
 *
 * References:
 *   Markley & Crassidis, "Fundamentals of Spacecraft Attitude Determination
 *   and Control", Springer 2014, §8.2.
 */
#ifndef OBSW_AOCS_ADCS_H
#define OBSW_AOCS_ADCS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Quaternion convention: [w, x, y, z], w = scalar part               */
/* ------------------------------------------------------------------ */

typedef struct {
    float w, x, y, z;
} obsw_quat_t;

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    float kp;         /**< Proportional gain [N·m/rad]             */
    float kd;         /**< Derivative gain [N·m·s/rad]             */
    float max_torque; /**< RW torque saturation limit [N·m]        */
} obsw_adcs_config_t;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    obsw_adcs_config_t config;
    obsw_quat_t q_cmd; /**< Target attitude quaternion          */
} obsw_adcs_ctx_t;

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    float torque_cmd[3]; /**< RW torque commands [N·m] (x,y,z)   */
    obsw_quat_t q_err;   /**< Attitude error quaternion           */
    float angle_err_rad; /**< Scalar attitude error [rad]         */
} obsw_adcs_output_t;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/**
 * Initialise ADCS controller with given configuration.
 * Initial target attitude is identity quaternion [1,0,0,0].
 */
void obsw_adcs_init(obsw_adcs_ctx_t *ctx, const obsw_adcs_config_t *config);

/**
 * Set the target attitude command.
 * @param q  Desired attitude quaternion (will be normalised internally).
 */
void obsw_adcs_set_target(obsw_adcs_ctx_t *ctx, const obsw_quat_t *q);

/**
 * Run one PD control step.
 *
 * @param ctx    Controller state.
 * @param q_meas Current attitude quaternion from star tracker.
 * @param omega  Current angular velocity from gyro [rad/s] (3-element).
 * @param out    Output torque commands and error metrics.
 * @return true if star tracker is valid and control ran, false otherwise.
 */
bool obsw_adcs_step(obsw_adcs_ctx_t *ctx,
                    const obsw_quat_t *q_meas,
                    const float omega[3],
                    obsw_adcs_output_t *out);

/**
 * Normalise a quaternion in-place.
 * Returns false if the quaternion has near-zero norm (invalid).
 */
bool obsw_quat_normalise(obsw_quat_t *q);

/**
 * Quaternion conjugate: q* = [w, -x, -y, -z]
 */
obsw_quat_t obsw_quat_conjugate(const obsw_quat_t *q);

/**
 * Quaternion multiplication: q = a ⊗ b
 */
obsw_quat_t obsw_quat_multiply(const obsw_quat_t *a, const obsw_quat_t *b);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_AOCS_ADCS_H */