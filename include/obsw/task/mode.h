#ifndef OBSW_TASK_MODE_H
#define OBSW_TASK_MODE_H

#include "obsw/fdir/fsm.h"
#include "obsw/tm/store.h"

/*
 * Mode Manager task — nominal mode lifecycle.
 *
 * Owns the satellite mode FSM and drives the STANDBY → SAFE → NOMINAL
 * sequence.  FDIR calls obsw_mode_request_safe() for fault-triggered
 * fallback; S8 handlers call the request functions for ground-commanded
 * transitions.
 *
 * Responsibilities:
 *   - Boots the FSM in STANDBY
 *   - Auto-transitions STANDBY → SAFE after STANDBY_SAFE_TIMEOUT_S seconds
 *   - Processes ground-commanded transitions via obsw_mode_request_*()
 *   - Emits TM(5,x) mode-change events on every transition
 *
 * Initialise before obsw_fdir_task_init() and obsw_pus_task_init().
 */
void obsw_mode_task_init(obsw_tm_store_t *tm_store);

/* Return the shared FSM context.  Valid after obsw_mode_task_init(). */
obsw_fsm_ctx_t *obsw_mode_get_fsm(void);

/*
 * Request a mode transition.  Safe to call from any FreeRTOS task context.
 * The transition is applied on the Mode Manager's next tick (≤ 1 s).
 * Redundant requests (already in the target mode) are silently ignored.
 */
void obsw_mode_request_safe(void);      /* any state → SAFE (fault fallback) */
void obsw_mode_request_nominal(void);   /* SAFE → NOMINAL (ground command)   */
void obsw_mode_request_standby(void);   /* any state → STANDBY (hibernation) */

#endif /* OBSW_TASK_MODE_H */
