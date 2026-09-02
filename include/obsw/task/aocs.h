#ifndef OBSW_TASK_AOCS_H
#define OBSW_TASK_AOCS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Live B-dot sensor and actuator state — written by the AOCS task each tick,
 * read by the FDIR task for S3 HK reporting.  Fields are individually
 * volatile; on single-core FreeRTOS each 32-bit write is atomic, so the
 * reader sees at-most-one-tick-stale data without any mutex.
 */
typedef struct
{
    volatile float   mag_x;      /**< Magnetometer X [T]       */
    volatile float   mag_y;      /**< Magnetometer Y [T]       */
    volatile float   mag_z;      /**< Magnetometer Z [T]       */
    volatile float   m_cmd_x;    /**< MTQ dipole command X [Am²] */
    volatile float   m_cmd_y;    /**< MTQ dipole command Y [Am²] */
    volatile float   m_cmd_z;    /**< MTQ dipole command Z [Am²] */
    volatile uint8_t mag_valid;  /**< 1 if QMC5883L read succeeded */
} obsw_aocs_hk_t;

void                       obsw_aocs_task_init(void);
const obsw_aocs_hk_t      *obsw_aocs_get_hk(void);

#endif /* OBSW_TASK_AOCS_H */
