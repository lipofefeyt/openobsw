#include "obsw/task/aocs.h"
#include "FreeRTOS.h"
#include "task.h"

#define AOCS_STACK_DEPTH 512U
#define AOCS_PRIORITY    2U   /* Medium — periodic 10 Hz control loop */

static StaticTask_t aocs_tcb;
static StackType_t  aocs_stack[AOCS_STACK_DEPTH];

/* Stub — full implementation in issues #35 and #40 */
static void aocs_task(void *param)
{
    (void)param;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;)
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100)); /* 10 Hz */
}

void obsw_aocs_task_init(void)
{
    xTaskCreateStatic(aocs_task, "AOCS", AOCS_STACK_DEPTH,
                      NULL, AOCS_PRIORITY, aocs_stack, &aocs_tcb);
}
