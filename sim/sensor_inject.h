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
} obsw_sensor_frame_t;
#pragma pack(pop)

#define OBSW_SENSOR_FRAME_LEN ((uint16_t)sizeof(obsw_sensor_frame_t))

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
 * Returns true if the buffer is large enough and parsing succeeded.
 */
bool obsw_sim_parse_sensor(const uint8_t *buf, uint16_t len, obsw_sensor_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_SIM_SENSOR_INJECT_H */