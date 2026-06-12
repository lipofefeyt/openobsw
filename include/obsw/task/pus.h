#ifndef OBSW_TASK_PUS_H
#define OBSW_TASK_PUS_H

#include "obsw/tm/store.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/*
 * Initialise the PUS task.
 *
 * PUS owns: TC dispatcher, S1/S8/S17/S20 service contexts.
 * It reads TC frames from tc_queue, applies the FSM TC gate (via the Mode
 * Manager's shared FSM), dispatches allowed TCs, and notifies tmtc_handle
 * when new TM is ready to drain.
 *
 * Call after obsw_mode_task_init() and obsw_tmtc_task_init().
 */
void obsw_pus_task_init(obsw_tm_store_t *tm_store,
                        QueueHandle_t    tc_queue,
                        TaskHandle_t     tmtc_handle);

/*
 * Read a float32 S20 parameter value by ID.
 * Returns default_val if the ID is not in the table.
 * Safe to call from any task — 32-bit float read is atomic on single-core ARM.
 */
float obsw_pus_s20_get_float(uint16_t param_id, float default_val);

#endif /* OBSW_TASK_PUS_H */
