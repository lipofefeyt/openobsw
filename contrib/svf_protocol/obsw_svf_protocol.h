/**
 * @file obsw_svf_protocol.h
 * @brief SVF pipe protocol — reference implementation.
 *
 * Drop this header + obsw_svf_protocol.c into your OBSW to add
 * OpenSVF Software-in-the-Loop (SIL) support in ~4 hours.
 *
 * Wire protocol v3 — type-prefixed frames:
 *
 *   SVF → OBSW stdin:
 *     [0x01][uint16 BE len][TC bytes]        TC uplink
 *     [0x02][uint16 BE len][sensor_frame_t]  Sensor injection
 *
 *   OBSW → SVF stdout:
 *     [0x04][uint16 BE len][TM bytes]        PUS TM downlink
 *     [0x03][uint16 BE len][actuator_frame_t] Actuator commands
 *     [0xFF]                                  End of tick
 *
 * Porting:
 *   1. Implement svf_io_read() and svf_io_write() for your platform
 *      (stdin/stdout for host sim, UART for Renode/hardware)
 *   2. Call svf_init() at startup
 *   3. In your main loop: call svf_read_frame() and svf_write_*()
 *
 * Tested on: x86_64 Linux, aarch64 Linux, MSP430, ARM Cortex-A53
 *
 * License: Apache 2.0
 * Part of: https://github.com/lipofefeyt/openobsw
 */

#ifndef OBSW_SVF_PROTOCOL_H
#define OBSW_SVF_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Frame type bytes                                                    */
/* ------------------------------------------------------------------ */

#define SVF_FRAME_TC        0x01U  /**< TC uplink (SVF → OBSW)        */
#define SVF_FRAME_SENSOR    0x02U  /**< Sensor injection (SVF → OBSW) */
#define SVF_FRAME_ACTUATOR  0x03U  /**< Actuator frame (OBSW → SVF)   */
#define SVF_FRAME_TM        0x04U  /**< PUS TM packet (OBSW → SVF)    */
#define SVF_SYNC_BYTE       0xFFU  /**< End of tick marker             */

/* ------------------------------------------------------------------ */
/* Sensor frame (SVF → OBSW, type 0x02)                               */
/* All floats little-endian. Total size: 29 bytes.                    */
/* ------------------------------------------------------------------ */

#pragma pack(push, 1)
typedef struct {
    float    mag_x;       /**< Magnetometer X [T]                    */
    float    mag_y;       /**< Magnetometer Y [T]                    */
    float    mag_z;       /**< Magnetometer Z [T]                    */
    uint8_t  mag_valid;   /**< 1 = valid measurement                 */
    float    st_q_w;      /**< Star tracker quaternion W             */
    float    st_q_x;      /**< Star tracker quaternion X             */
    float    st_q_y;      /**< Star tracker quaternion Y             */
    float    st_q_z;      /**< Star tracker quaternion Z             */
    uint8_t  st_valid;    /**< 1 = valid quaternion                  */
    float    gyro_x;      /**< Gyroscope X [rad/s]                   */
    float    gyro_y;      /**< Gyroscope Y [rad/s]                   */
    float    gyro_z;      /**< Gyroscope Z [rad/s]                   */
    uint8_t  gyro_valid;  /**< 1 = valid measurement                 */
    float    sim_time;    /**< Current simulation time [s]           */
} svf_sensor_frame_t;
#pragma pack(pop)

#define SVF_SENSOR_FRAME_LEN ((uint16_t)sizeof(svf_sensor_frame_t))

/* ------------------------------------------------------------------ */
/* Actuator frame (OBSW → SVF, type 0x03)                            */
/* All floats little-endian. Total size: 29 bytes.                    */
/* ------------------------------------------------------------------ */

#pragma pack(push, 1)
typedef struct {
    float    mtq_dipole_x;  /**< MTQ dipole X [Am²] (b-dot mode)    */
    float    mtq_dipole_y;  /**< MTQ dipole Y [Am²]                  */
    float    mtq_dipole_z;  /**< MTQ dipole Z [Am²]                  */
    float    rw_torque_x;   /**< RW torque X [Nm] (ADCS mode)        */
    float    rw_torque_y;   /**< RW torque Y [Nm]                    */
    float    rw_torque_z;   /**< RW torque Z [Nm]                    */
    uint8_t  controller;    /**< 0 = b-dot, 1 = ADCS PD              */
    float    sim_time;      /**< Echo of received sim_time           */
} svf_actuator_frame_t;
#pragma pack(pop)

#define SVF_ACTUATOR_FRAME_LEN ((uint16_t)sizeof(svf_actuator_frame_t))

/* ------------------------------------------------------------------ */
/* I/O interface — implement for your platform                        */
/* ------------------------------------------------------------------ */

/**
 * Read exactly len bytes into buf.
 * Block until all bytes are available.
 * Return 0 on success, -1 on error/EOF.
 */
extern int svf_io_read(uint8_t *buf, uint16_t len);

/**
 * Write exactly len bytes from buf.
 * Return 0 on success, -1 on error.
 */
extern int svf_io_write(const uint8_t *buf, uint16_t len);

/**
 * Flush output buffer (call fflush or UART drain).
 */
extern void svf_io_flush(void);

/* ------------------------------------------------------------------ */
/* Protocol API                                                        */
/* ------------------------------------------------------------------ */

/**
 * Print startup banner to stderr/UART.
 * Call once at OBSW startup before the main loop.
 *
 * @param srdb_version  Your SRDB version string (e.g. "0.1.0")
 */
void svf_init(const char *srdb_version);

/**
 * Read one typed frame from SVF.
 *
 * @param buf      Output buffer
 * @param bufsize  Buffer capacity
 * @param out_len  Set to payload length on success
 *
 * @return Frame type byte (SVF_FRAME_TC or SVF_FRAME_SENSOR),
 *         or 0 on error/EOF.
 */
uint8_t svf_read_frame(uint8_t *buf, uint16_t bufsize, uint16_t *out_len);

/**
 * Write a TM packet to SVF (type 0x04 frame).
 *
 * @param pkt  PUS TM packet bytes
 * @param len  Packet length
 */
void svf_write_tm(const uint8_t *pkt, uint16_t len);

/**
 * Write an actuator frame to SVF (type 0x03 frame).
 *
 * @param act  Actuator frame to send
 */
void svf_write_actuator(const svf_actuator_frame_t *act);

/**
 * Write the end-of-tick sync byte (0xFF).
 * Call at the end of every tick, after draining TM and actuators.
 */
void svf_write_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_SVF_PROTOCOL_H */
