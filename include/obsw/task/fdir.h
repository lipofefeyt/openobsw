#ifndef OBSW_TASK_FDIR_H
#define OBSW_TASK_FDIR_H

#include "obsw/fdir/fsm.h"
#include "obsw/tm/store.h"

/*
 * Initialise the FDIR task.
 *
 * FDIR owns the mode FSM and S5 event context. It:
 *   - Initialises and periodically kicks the STM32H7 IWDG (4s timeout, 1s kick)
 *   - Emits TM(5,1) BOOT_COMPLETE on its first tick
 *   - Detects NOMINAL→SAFE and SAFE→NOMINAL transitions and emits TM(5,x) events
 *
 * Call before obsw_pus_task_init() so the FSM pointer is valid when PUS wires
 * up its S8 recover function and TC gate.
 */
void obsw_fdir_task_init(obsw_tm_store_t *tm_store);

/* Return the shared FSM context. Valid after obsw_fdir_task_init(). */
obsw_fsm_ctx_t *obsw_fdir_get_fsm(void);

#endif /* OBSW_TASK_FDIR_H */
