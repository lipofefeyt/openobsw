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

/* USART3 ISR register — RXNE bit checked to yield when UART is idle.
 * Polling avoids spinning at maximum priority when no ground traffic arrives. */
#define USART3_ISR_REG (*(volatile uint32_t *)(0x40004800UL + 0x1CU))
#define USART_RXNE     (1U << 5)

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

static void flush_tm(void)
{
    uint16_t plen = 0;
    while (obsw_tm_store_dequeue(s_tm_store, s_tm_pkt, sizeof(s_tm_pkt), &plen)
           == OBSW_TM_OK) {
        uint8_t hdr[3] = {WIRE_TM, (uint8_t)(plen >> 8), (uint8_t)(plen & 0xFFU)};
        uart_write(hdr, 3);
        uart_write(s_tm_pkt, plen);
    }
}

/* ── Task body ────────────────────────────────────────────────────── */

static void tmtc_task(void *param)
{
    (void)param;

    static const uint8_t EOT = 0xFF;

    for (;;) {
        /* Yield when UART RX is empty — gives lower-priority tasks CPU time.
         * PUS notifies this task when TM is ready so drain latency stays low. */
        if (!(USART3_ISR_REG & USART_RXNE)) {
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
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

QueueHandle_t obsw_tmtc_get_tc_queue(void) { return s_tc_queue; }
TaskHandle_t  obsw_tmtc_get_handle(void)   { return s_handle;   }

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
}
