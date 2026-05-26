#ifndef OBSW_TASK_TMTC_H
#define OBSW_TASK_TMTC_H

#include "obsw/hal/io.h"
#include "obsw/tm/store.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* Maximum raw TC frame size accepted from the UART link. */
#define OBSW_TC_FRAME_MAX 512U

/* TC frame item placed on the inter-task queue by TMTC, consumed by PUS. */
typedef struct {
    uint8_t  data[OBSW_TC_FRAME_MAX];
    uint16_t len;
} obsw_tc_frame_item_t;

/* Initialise the TMTC task. Must be called before obsw_pus_task_init(). */
void          obsw_tmtc_task_init(obsw_io_ops_t *io, obsw_tm_store_t *tm_store);

/* Return the TC frame queue written by TMTC and read by PUS. */
QueueHandle_t obsw_tmtc_get_tc_queue(void);

/* Return the TMTC task handle so PUS can send task notifications. */
TaskHandle_t  obsw_tmtc_get_handle(void);

#endif /* OBSW_TASK_TMTC_H */
