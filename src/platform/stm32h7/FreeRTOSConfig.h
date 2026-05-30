/* FreeRTOS configuration for STM32H750VBT6 (Cortex-M7, HSI 64 MHz).
 *
 * Static allocation only — configSUPPORT_DYNAMIC_ALLOCATION=0.
 * All tasks, queues, and semaphores must be created with their
 * *Static() variants. malloc() is never called.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ── Scheduler ────────────────────────────────────────────────────── */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configCPU_CLOCK_HZ                      64000000UL
#define configTICK_RATE_HZ                      1000UL
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                128U
#define configMAX_TASK_NAME_LEN                 12
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* ── Memory allocation — static only ─────────────────────────────── */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0

/* ── IPC primitives ───────────────────────────────────────────────── */
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               0

/* ── Software timers (used for S3 HK periodic reports) ───────────── */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               2
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256U

/* ── Stack overflow detection ─────────────────────────────────────── */
#define configCHECK_FOR_STACK_OVERFLOW          2

/* ── Hook functions ───────────────────────────────────────────────── */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* ── Unused features ──────────────────────────────────────────────── */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configUSE_CO_ROUTINES                   0
#define configUSE_APPLICATION_TASK_TAG          0

/* ── Cortex-M7 interrupt priorities ──────────────────────────────── */
/* STM32H7 NVIC has 4 priority bits (levels 0–15, 0 = highest).
 * ISRs at or below MAX_SYSCALL may call FreeRTOS FromISR() APIs. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
#define configKERNEL_INTERRUPT_PRIORITY  \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY     << 4)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << 4)

/* ── Map FreeRTOS port handlers to CMSIS weak symbols ────────────── */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* ── Assert ───────────────────────────────────────────────────────── */
void obsw_freertos_assert_fail(const char *file, int line);
#define configASSERT(x) \
    do { if (!(x)) obsw_freertos_assert_fail(__FILE__, __LINE__); } while (0)

/* ── INCLUDE_ API selection ───────────────────────────────────────── */
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 0
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle      0
#define INCLUDE_eTaskGetState               0
#define INCLUDE_xTaskResumeFromISR          1
#define INCLUDE_xTaskAbortDelay             0

#endif /* FREERTOS_CONFIG_H */
