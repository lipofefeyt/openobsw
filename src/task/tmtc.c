/*
 * TMTC task — TC uplink and TM downlink over USART3 (wire protocol v3).
 *
 * This task has the highest priority (4) and owns the physical UART link.
 * It polls for incoming bytes, assembles type-framed TC packets, pushes them
 * onto a queue for the PUS task, then drains any pending TM back to ground.
 *
 * To avoid starving lower-priority tasks while the UART is idle, the task
 * yields for up to 1 ms using ulTaskNotifyTake(). The PUS task sends a
 * notification after each TC dispatch to trigger an immediate TM drain.
 *
 * Wire protocol v3 uplink frame:
 *   [0x01][uint16 BE len][TC frame bytes]
 *
 * Wire protocol v3 downlink frame:
 *   [0x04][uint16 BE len][TM packet bytes]
 *   [0xFF]                                  end-of-tick
 */

#include "obsw/task/tmtc.h"
#include "obsw/tm/store.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Wire protocol type bytes */
#define WIRE_TC 0x01U
#define WIRE_TM 0x04U

/* USART3 registers used directly by the TMTC task and its RX ISR */
#define USART3_BASE    0x40004800UL
#define USART3_CR1_REG (*(volatile uint32_t *)(USART3_BASE + 0x00U))
#define USART3_ISR_REG (*(volatile uint32_t *)(USART3_BASE + 0x1CU))
#define USART_RXNE     (1U << 5)
#define USART_RXNEIE   (1U << 5) /* same bit position in CR1 */

/* NVIC registers for USART3 (IRQ 39).
 * IPR9 holds priorities for IRQs 36-39; IRQ 39 = bits [31:24].
 * ISER1 enables IRQs 32-63; bit 7 = IRQ 39. */
#define NVIC_ISER1 (*(volatile uint32_t *)0xE000E104UL)
#define NVIC_IPR9  (*(volatile uint32_t *)0xE000E424UL)

#define TMTC_STACK_DEPTH 512U
#define TMTC_PRIORITY    4U   /* Highest — owns the physical UART link */

#define TC_QUEUE_DEPTH 4U

/* ── Static allocation ────────────────────────────────────────────── */

static StaticTask_t  s_tcb;
static StackType_t   s_stack[TMTC_STACK_DEPTH];
static TaskHandle_t  s_handle;

static StaticQueue_t s_queue_struct;
static uint8_t       s_queue_storage[TC_QUEUE_DEPTH * sizeof(obsw_tc_frame_item_t)];
static QueueHandle_t s_tc_queue;

static uint8_t s_tm_pkt[OBSW_TM_MAX_PACKET_LEN]; /* static — keeps 1 KB off task stack */

static obsw_io_ops_t   *s_io;
static obsw_tm_store_t *s_tm_store;
static obsw_tm_log_t    s_tm_log;

/* ── UART primitives ──────────────────────────────────────────────── */

static uint8_t uart_getc(void)
{
    uint8_t b;
    s_io->read(&b, 1, s_io->ctx);
    return b;
}

static void uart_write(const uint8_t *buf, uint16_t len)
{
    s_io->write(buf, len, s_io->ctx);
}

/* ── TM downlink ──────────────────────────────────────────────────── */

/* Emit "# TM(svc,subsvc)\r\n" after each binary frame so picocom users
 * see a human-readable label.  '#' (0x23) is not 0x04 or 0xFF, so wire-
 * protocol parsers skip it safely (decode_tm_packets skips non-0x04 bytes). */
static void tm_uart_label(uint8_t svc, uint8_t subsvc)
{
    uint8_t buf[16];
    uint8_t i = 0;
    buf[i++] = '#'; buf[i++] = ' ';
    buf[i++] = 'T'; buf[i++] = 'M'; buf[i++] = '(';
    if (svc    >= 10U) buf[i++] = (uint8_t)('0' + svc    / 10U);
    buf[i++] = (uint8_t)('0' + svc    % 10U);
    buf[i++] = ',';
    if (subsvc >= 10U) buf[i++] = (uint8_t)('0' + subsvc / 10U);
    buf[i++] = (uint8_t)('0' + subsvc % 10U);
    buf[i++] = ')'; buf[i++] = '\r'; buf[i++] = '\n';
    uart_write(buf, i);
}

static void flush_tm(void)
{
    uint16_t plen = 0;
    while (obsw_tm_store_dequeue(s_tm_store, s_tm_pkt, sizeof(s_tm_pkt), &plen)
           == OBSW_TM_OK) {
        uint8_t hdr[3] = {WIRE_TM, (uint8_t)(plen >> 8), (uint8_t)(plen & 0xFFU)};
        uart_write(hdr, 3);
        uart_write(s_tm_pkt, plen);
        if (plen >= 9U) {
            uint8_t svc    = s_tm_pkt[7];
            uint8_t subsvc = s_tm_pkt[8];
            /* Log for LCD display (FDIR task reads) */
            s_tm_log.buf[s_tm_log.write_idx % OBSW_TM_LOG_DEPTH].svc    = svc;
            s_tm_log.buf[s_tm_log.write_idx % OBSW_TM_LOG_DEPTH].subsvc = subsvc;
            s_tm_log.write_idx++;
            /* ASCII label on UART for terminal monitors */
            tm_uart_label(svc, subsvc);
        }
    }
}

/* ── Task body ────────────────────────────────────────────────────── */

static void tmtc_task(void *param)
{
    (void)param;

    static const uint8_t EOT = 0xFF;

    for (;;) {
        /* Sleep when UART RX is empty — gives lower-priority tasks CPU time.
         * Re-arm the RXNE interrupt before sleeping so the first incoming
         * byte wakes us within microseconds (the ISR disables it again to
         * avoid per-byte re-entry).  PUS also notifies via xTaskNotify. */
        if (!(USART3_ISR_REG & USART_RXNE)) {
            USART3_CR1_REG |= USART_RXNEIE;
            ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1));
            flush_tm();
            continue;
        }

        uint8_t  type = uart_getc();
        uint8_t  hi   = uart_getc();
        uint8_t  lo   = uart_getc();
        uint16_t flen = (uint16_t)((uint16_t)(hi << 8) | lo);

        if (flen == 0 || flen > OBSW_TC_FRAME_MAX) {
            flush_tm();
            uart_write(&EOT, 1);
            taskYIELD();
            continue;
        }

        obsw_tc_frame_item_t item;
        item.len = flen;
        for (uint16_t i = 0; i < flen; i++)
            item.data[i] = uart_getc();

        if (type == WIRE_TC)
            xQueueSend(s_tc_queue, &item, 0); /* non-blocking; drop if PUS is backed up */

        flush_tm();
        uart_write(&EOT, 1);
        taskYIELD(); /* give FDIR and other lower-priority tasks a scheduling slot */
    }
}

/* ── USART3 RX interrupt ──────────────────────────────────────────── */

/* Fires on the FIRST byte of an incoming frame.  Disables itself to
 * prevent re-entry for every subsequent byte (uart_getc() polls RXNE
 * directly for the rest of the frame).  The task re-enables it when
 * it goes idle, closing the race: if RXNE is already set at that point
 * the interrupt fires immediately and the task loops without sleeping. */
void USART3_IRQHandler(void)
{
    USART3_CR1_REG &= ~USART_RXNEIE; /* disable — task re-enables before sleep */
    BaseType_t hp = pdFALSE;
    xTaskNotifyFromISR(s_handle, 0, eNoAction, &hp);
    portYIELD_FROM_ISR(hp);
}

/* ── Public API ───────────────────────────────────────────────────── */

QueueHandle_t  obsw_tmtc_get_tc_queue(void) { return s_tc_queue; }
TaskHandle_t   obsw_tmtc_get_handle(void)   { return s_handle;   }
obsw_tm_log_t *obsw_tmtc_get_tm_log(void)  { return &s_tm_log;  }

void obsw_tmtc_task_init(obsw_io_ops_t *io, obsw_tm_store_t *tm_store)
{
    s_io       = io;
    s_tm_store = tm_store;

    s_tc_queue = xQueueCreateStatic(TC_QUEUE_DEPTH,
                                    sizeof(obsw_tc_frame_item_t),
                                    s_queue_storage,
                                    &s_queue_struct);

    s_handle = xTaskCreateStatic(tmtc_task, "TMTC", TMTC_STACK_DEPTH,
                                 NULL, TMTC_PRIORITY, s_stack, &s_tcb);

    /* Configure USART3 IRQ (39) in NVIC.
     * Priority must be >= configMAX_SYSCALL_INTERRUPT_PRIORITY so that
     * xTaskNotifyFromISR() can be called safely from the handler.
     * IPR9 covers IRQs 36-39; USART3 priority sits in bits [31:24]. */
    NVIC_IPR9 = (NVIC_IPR9 & ~(0xFFU << 24)) |
                ((uint32_t)configMAX_SYSCALL_INTERRUPT_PRIORITY << 24);
    NVIC_ISER1 |= (1U << 7);  /* enable IRQ 39 */
    /* RXNEIE is NOT enabled here; tmtc_task arms it just before sleeping. */
}
