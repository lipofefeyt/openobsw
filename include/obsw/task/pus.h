#ifndef OBSW_TASK_PUS_H
#define OBSW_TASK_PUS_H

#include "obsw/fdir/fsm.h"
#include "obsw/tm/store.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/*
 * Initialise the PUS task.
 *
 * PUS owns: TC dispatcher, S1/S17/S20/S8 service contexts.
 * It reads TC frames from tc_queue, applies the FSM TC gate, dispatches
 * allowed TCs, and notifies tmtc_handle when new TM is ready to drain.
 *
 * fsm must point to the FSM owned by the FDIR task (obsw_fdir_get_fsm()).
 * Call after obsw_fdir_task_init() and obsw_tmtc_task_init().
 */
void obsw_pus_task_init(obsw_tm_store_t *tm_store,
                        QueueHandle_t    tc_queue,
                        TaskHandle_t     tmtc_handle,
                        obsw_fsm_ctx_t  *fsm);

#endif /* OBSW_TASK_PUS_H */
