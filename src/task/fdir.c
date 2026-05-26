#include "obsw/task/fdir.h"
#include "FreeRTOS.h"
#include "task.h"

#define FDIR_STACK_DEPTH 256U
#define FDIR_PRIORITY    3U   /* Med-high — fault response preempts normal work */

static StaticTask_t fdir_tcb;
static StackType_t  fdir_stack[FDIR_STACK_DEPTH];

/* Stub — full implementation in issue #36 */
static void fdir_task(void *param)
{
    (void)param;
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void obsw_fdir_task_init(void)
{
    xTaskCreateStatic(fdir_task, "FDIR", FDIR_STACK_DEPTH,
                      NULL, FDIR_PRIORITY, fdir_stack, &fdir_tcb);
}
