#ifndef OBSW_TASK_PUS_H
#define OBSW_TASK_PUS_H

#include "obsw/tm/store.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/*
 * Initialise the PUS task.
 *
 * PUS owns the TC dispatcher and all PUS service contexts (S1/S17/S20).
 * It reads TC frames from tc_queue (produced by the TMTC task), dispatches
 * them, and notifies tmtc_handle when new TM is ready to drain.
 *
 * Call after obsw_tmtc_task_init() so the queue and handle are valid.
 */
void obsw_pus_task_init(obsw_tm_store_t *tm_store,
                        QueueHandle_t    tc_queue,
                        TaskHandle_t     tmtc_handle);

#endif /* OBSW_TASK_PUS_H */
