#ifndef OBSW_TASK_FDIR_H
#define OBSW_TASK_FDIR_H

#include "obsw/tm/store.h"
#include "obsw/pus/s3.h"

/*
 * Initialise the FDIR task.
 *
 * FDIR owns the S3 housekeeping context and ticks it every second.
 * PUS task registers TC(3,5)/(3,6) routes via obsw_fdir_get_s3_ctx(),
 * which must be called after obsw_fdir_task_init().
 *
 * FDIR is a fault-only responder. It:
 *   - Initialises and periodically kicks the STM32H7 IWDG (4s timeout, 1s kick)
 *   - Emits TM(5,1) BOOT_COMPLETE on its first tick
 *   - Reports POWER and TEMPERATURE faults via S5; S5 HIGH events with
 *     matching trigger IDs call obsw_fsm_to_safe() on the Mode Manager's FSM
 *   - Displays current mode and watchdog status on the LCD
 *   - Ticks S3 HK sets: SID 2 (FDIR health), SID 7 (AOCS B-dot sensor/actuator)
 *
 * The mode FSM is owned by the Mode Manager task (obsw_mode_get_fsm()).
 * Call after obsw_mode_task_init().
 */
void          obsw_fdir_task_init(obsw_tm_store_t *tm_store);
obsw_s3_ctx_t *obsw_fdir_get_s3_ctx(void);

#endif /* OBSW_TASK_FDIR_H */
