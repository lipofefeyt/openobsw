/**
 * @file sensor_inject.h
 * @brief Simulation-only sensor injection frame parser.
 *
 * Extends the obsw_sim pipe protocol with type-prefixed frames:
 *
 *   Type 0x01 — TC uplink (existing, unchanged)
 *     [0x01][uint16 BE length][TC frame bytes]
 *
 *   Type 0x02 — Sensor data injection (simulation only)
 *     [0x02][uint16 BE length][obsw_sensor_frame_t bytes]
 *
 * This interface is SIMULATION-ONLY. It does not exist in flight
 * software — real sensor data arrives via 1553/SpaceWire bus messages.
 * It is isolated here so the flight code path (src/) never sees it.
 *
 * The OBCEmulatorAdapter in OpenSVF reads sensor values from
 * ParameterStore and packages them as type-0x02 frames each tick.
 */
#ifndef OBSW_SIM_SENSOR_INJECT_H
#define OBSW_SIM_SENSOR_INJECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Frame type bytes                                                    */
/* ------------------------------------------------------------------ */

#define OBSW_FRAME_TC 0x01U     /**< TC uplink frame                */
#define OBSW_FRAME_SENSOR 0x02U /**< Sensor injection frame (sim)   */

/* ------------------------------------------------------------------ */
/* Sensor data frame                                                   */
/* ------------------------------------------------------------------ */

/**
 * Sensor data injected by OBCEmulatorAdapter each simulation tick.
 *
 * All values use SI units to match OpenSVF equipment model outputs:
 *   Magnetometer: Tesla [T]
 *   Gyro:         rad/s
 *   Quaternion:   unit quaternion [w, x, y, z]
 *
 * Valid flags indicate whether the sensor was healthy this tick.
 * The AOCS controller must check validity before using any sensor.
 */
#pragma pack(push, 1)
typedef struct {
    /* Magnetometer — body frame [T] */
    float mag_x;
    float mag_y;
    float mag_z;
    uint8_t mag_valid;

    /* Star tracker — body-to-inertial quaternion [w, x, y, z] */
    float st_q_w;
    float st_q_x;
    float st_q_y;
    float st_q_z;
    uint8_t st_valid;

    /* Gyro — angular velocity body frame [rad/s] */
    float gyro_x;
    float gyro_y;
    float gyro_z;
    uint8_t gyro_valid;

    /* Simulation time [s] — for derivative calculations */
    float sim_time;

    /* OBC thermal sensor (wire protocol v4). Raw counts in the SRDB
     * encoding of obc_temperature: degC = 0.1 * raw - 40 (85 degC = 1250).
     * When thermal_valid is 1 the sim writes the value into obc_temperature
     * (S20 0x0001) before the tick's monitoring and HK; when 0 the parameter
     * keeps its last value, so TC(20,1) still sets it. */
    int16_t obc_temperature_raw;
    uint8_t thermal_valid;
} obsw_sensor_frame_t;
#pragma pack(pop)

/** Wire protocol version; printed in the start-up banner. */
#define OBSW_WIRE_PROTOCOL_VERSION 4U

#define OBSW_SENSOR_FRAME_LEN ((uint16_t)sizeof(obsw_sensor_frame_t)) /* 50 B */
#define OBSW_SENSOR_FRAME_LEN_V3 47U /**< v3 frame (no thermal fields), rejected */

_Static_assert(sizeof(obsw_sensor_frame_t) == 50U, "obsw_sensor_frame_t must be 50 B (v4)");

/* ------------------------------------------------------------------ */
/* Frame reading                                                       */
/* ------------------------------------------------------------------ */

/**
 * Read one type-prefixed frame from stdin.
 *
 * Reads the type byte, then the 2-byte length, then the body into buf.
 * Returns the frame type (OBSW_FRAME_TC or OBSW_FRAME_SENSOR),
 * or 0 on EOF/error.
 *
 * @param buf      Destination buffer.
 * @param buf_len  Buffer capacity in bytes.
 * @param out_len  Set to the number of bytes read into buf.
 */
uint8_t obsw_sim_read_frame(uint8_t *buf, uint16_t buf_len, uint16_t *out_len);

/**
 * Parse a sensor injection frame from a byte buffer.
 * Returns true only if len is exactly OBSW_SENSOR_FRAME_LEN; a frame of any
 * other length (a v3 sender's 47 B, for one) is not parsed.
 */
bool obsw_sim_parse_sensor(const uint8_t *buf, uint16_t len, obsw_sensor_frame_t *out);

#ifdef __cplusplus
}
#endif


/* ------------------------------------------------------------------ */
/* Type 0x03 — Actuator output frame (obsw_sim → SVF)                 */
/* ------------------------------------------------------------------ */

#define OBSW_FRAME_TM       0x04U /**< TM packet frame (sim stdout) */
#define OBSW_FRAME_ACTUATOR 0x03U

#pragma pack(push, 1)
typedef struct {
    float mtq_dipole_x;   /**< MTQ dipole X [Am²] — b-dot output   */
    float mtq_dipole_y;
    float mtq_dipole_z;
    float rw_torque_x;    /**< RW torque X [Nm]  — ADCS output     */
    float rw_torque_y;
    float rw_torque_z;
    uint8_t controller;   /**< 0=bdot (SAFE), 1=adcs (NOMINAL)     */
    float sim_time;       /**< Simulation time [s]                  */
} obsw_actuator_frame_t;
#pragma pack(pop)

#define OBSW_ACTUATOR_FRAME_LEN ((uint16_t)sizeof(obsw_actuator_frame_t))

#endif /* OBSW_SIM_SENSOR_INJECT_H */
