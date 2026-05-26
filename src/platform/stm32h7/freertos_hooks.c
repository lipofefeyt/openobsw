/* FreeRTOS static allocation hooks and fault handlers for STM32H750. */

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

/* ── Idle task static memory ──────────────────────────────────────── */

static StaticTask_t idle_task_tcb;
static StackType_t  idle_task_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t  **ppxIdleTaskStackBuffer,
                                   uint32_t      *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

/* ── Timer daemon task static memory ─────────────────────────────── */

static StaticTask_t timer_task_tcb;
static StackType_t  timer_task_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t  **ppxTimerTaskStackBuffer,
                                    uint32_t      *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer   = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

/* ── Stack overflow hook ──────────────────────────────────────────── */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    while (1); /* Spin — debugger catches PC here */
}

/* ── Assert handler ───────────────────────────────────────────────── */

void obsw_freertos_assert_fail(const char *file, int line)
{
    (void)file; (void)line;
    __asm volatile("bkpt #0");
    while (1);
}
