/* FreeRTOS static allocation hooks and fault handlers for STM32H750. */

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stdint.h>

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

/* ── Fault UART helpers (direct register access — no driver dependency) ── */

#define USART3_TDR (*(volatile uint32_t *)(0x40004800UL + 0x28U))
#define USART3_ISR (*(volatile uint32_t *)(0x40004800UL + 0x1CU))
#define USART_TXE  (1U << 7)

static void fault_putc(char c)
{
    while (!(USART3_ISR & USART_TXE));
    USART3_TDR = (uint8_t)c;
}

static void fault_puts(const char *s)
{
    while (*s) fault_putc(*s++);
}

static void fault_hex32(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        fault_putc(h[(v >> (i * 4)) & 0xFU]);
    }
}

static void fault_u32(uint32_t v)
{
    char buf[10];
    int i = 9;
    buf[i] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    else { while (v) { buf[--i] = (char)('0' + v % 10U); v /= 10U; } }
    fault_puts(buf + i);
}

/* ── HardFault handler ────────────────────────────────────────────── */

/* Cortex-M fault status registers */
#define SCB_HFSR (*(volatile uint32_t *)0xE000ED2CUL)
#define SCB_CFSR (*(volatile uint32_t *)0xE000ED28UL)
#define SCB_MMAR (*(volatile uint32_t *)0xE000ED34UL)
#define SCB_BFAR (*(volatile uint32_t *)0xE000ED38UL)

/* Called from the naked trampoline below.
 * frame[] = { r0,r1,r2,r3,r12,lr,pc,xpsr } pushed by hardware on fault entry. */
void hard_fault_handler_c(uint32_t *frame)
{
    fault_puts("\r\n[HARDFAULT]");
    fault_puts(" PC=");   fault_hex32(frame[6]);
    fault_puts(" LR=");   fault_hex32(frame[5]);
    fault_puts(" PSR=");  fault_hex32(frame[7]);
    fault_puts("\r\n");
    fault_puts(" HFSR="); fault_hex32(SCB_HFSR);
    fault_puts(" CFSR="); fault_hex32(SCB_CFSR);
    fault_puts(" MMAR="); fault_hex32(SCB_MMAR);
    fault_puts(" BFAR="); fault_hex32(SCB_BFAR);
    fault_puts("\r\n");
    for (;;);  /* IWDG will reset after 4 s if FDIR stops kicking */
}

/* Trampoline: determine which stack was active, pass it to C handler. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b     hard_fault_handler_c \n"
    );
}

/* ── Stack overflow hook ──────────────────────────────────────────── */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    fault_puts("\r\n[STACK OVERFLOW] task=");
    fault_puts(pcTaskName);
    fault_puts("\r\n");
    for (;;);
}

/* ── Assert handler ───────────────────────────────────────────────── */

void obsw_freertos_assert_fail(const char *file, int line)
{
    (void)file;
    fault_puts("\r\n[ASSERT FAIL] line=");
    fault_u32((uint32_t)line);
    fault_puts(" file=");
    fault_puts(file);
    fault_puts("\r\n");
    for (;;);
}
