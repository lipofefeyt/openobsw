/**
 * @file src/platform/stm32h7/main.c
 * @brief STM32H750VBT6 bare-metal entry point.
 *
 * Initialises the system clock, UART, watchdog (disabled),
 * and the OBSW PUS dispatcher. Runs the wire protocol v3
 * main loop — same protocol as the host sim and ZynqMP target.
 *
 * Clock configuration:
 *   HSI 64 MHz (reset default, no PLL) — HSE crystal not confirmed present
 *   BRR = 64000000 / 115200 = 556
 *
 * UART: USART3 on PD8/PD9, 115200 baud.
 */

 #include "obsw/hal/io.h"
 #include "obsw/pus/pus_tm.h"
 #include "obsw/pus/s1.h"
 #include "obsw/pus/s17.h"
 #include "obsw/pus/s20.h"
 #include "obsw/tc/dispatcher.h"
 #include "obsw/tm/store.h"
 #include "obsw/srdb_generated.h"
 #include "sensor_inject.h"

 #ifdef OBSW_FREERTOS
 #include "FreeRTOS.h"
 #include "task.h"
 #include "obsw/task/tmtc.h"
 #include "obsw/task/pus.h"
 #include "obsw/task/aocs.h"
 #include "obsw/task/fdir.h"
 #endif

 #include <stdint.h>
 #include <string.h>

/* ------------------------------------------------------------------ */
/* RCC register map (minimal — only what clock init needs)            */
/* ------------------------------------------------------------------ */

#define RCC_CR          (*(volatile uint32_t *)(0x58024400UL + 0x000))
#define RCC_CFGR        (*(volatile uint32_t *)(0x58024400UL + 0x010))
#define RCC_D1CFGR      (*(volatile uint32_t *)(0x58024400UL + 0x018))
#define RCC_D2CFGR      (*(volatile uint32_t *)(0x58024400UL + 0x01C))
#define RCC_D3CFGR      (*(volatile uint32_t *)(0x58024400UL + 0x020))
#define RCC_PLLCKSELR   (*(volatile uint32_t *)(0x58024400UL + 0x028))
#define RCC_PLLCFGR     (*(volatile uint32_t *)(0x58024400UL + 0x02C))
#define RCC_PLL1DIVR    (*(volatile uint32_t *)(0x58024400UL + 0x030))

/* Flash latency register */
#define FLASH_ACR       (*(volatile uint32_t *)(0x52002000UL + 0x000))

/* PWR register — needed to set VOS0 for 480 MHz */
#define PWR_CR3         (*(volatile uint32_t *)(0x58024800UL + 0x00C))
#define PWR_D3CR        (*(volatile uint32_t *)(0x58024800UL + 0x018))
#define SYSCFG_PWRCR    (*(volatile uint32_t *)(0x58000400UL + 0x004))

 #ifndef OBSW_RENODE
 /**
 * STM32H750 System Clock Configuration
 *
 * Running on HSI 64 MHz (reset default) — no HSE/PLL.
 * HSE crystal not confirmed present on this board.
 * VOS3 at 64 MHz → 0 flash wait states.
 * USART3 BRR = 64000000 / 115200 = 556
 */
static void system_clock_init(void)
{
    /* HSI 64 MHz is already active at reset. Just correct flash latency:
     * reset value of FLASH_ACR.LATENCY is 7 WS; VOS3 at 64 MHz needs 0. */
    FLASH_ACR = (FLASH_ACR & ~0xFU) | 0U;
}
 #endif /* OBSW_RENODE */

 /* ------------------------------------------------------------------ */
 /* External symbols from startup.S and HAL                             */
 /* ------------------------------------------------------------------ */
 
 extern obsw_io_ops_t obsw_uart_ops;
 extern void          obsw_uart_init(void);
 
 /* ------------------------------------------------------------------ */
 /* OBSW context                                                         */
 /* ------------------------------------------------------------------ */

 static obsw_tm_store_t tm_store;

 /* Superloop path owns the dispatcher and PUS contexts directly.
  * In the FreeRTOS path these live in src/task/pus.c instead. */
 #ifndef OBSW_FREERTOS
 static void noop_responder(uint8_t flag, const obsw_tc_t *tc, void *ctx)
 {
     (void)flag; (void)tc; (void)ctx;
 }

 static obsw_s1_ctx_t   s1_ctx  = {0};
 static obsw_s17_ctx_t  s17_ctx = {0};
 static obsw_s20_ctx_t  s20_ctx = {0};

 static obsw_s20_param_t s20_params[] = {
     {.param_id = SRDB_PARAM_OBC_UPTIME,           .value = {.u32 = 0}},
     {.param_id = SRDB_PARAM_SAFE_MODE_ENTRY_COUNT,.value = {.u32 = 0}},
     {.param_id = SRDB_PARAM_WATCHDOG_KICK_COUNT,  .value = {.u32 = 0}},
 };

 static obsw_tc_route_t routes[] = {
     {.apid = 0xFFFF, .service = 17, .subservice = 1,
      .handler = obsw_s17_ping,   .ctx = &s17_ctx},
     {.apid = 0xFFFF, .service = 20, .subservice = 1,
      .handler = obsw_s20_set,    .ctx = &s20_ctx},
     {.apid = 0xFFFF, .service = 20, .subservice = 3,
      .handler = obsw_s20_get,    .ctx = &s20_ctx},
 };
 #endif /* !OBSW_FREERTOS */
 
 /* ------------------------------------------------------------------ */
 /* UART I/O helpers                                                     */
 /* ------------------------------------------------------------------ */

 static void uart_write_buf(const uint8_t *buf, uint16_t len)
 {
     obsw_uart_ops.write(buf, len, NULL);
 }

static void uart_print_hex32(uint32_t val)
{
    static const char h[] = "0123456789ABCDEF";
    uint8_t buf[8];
    for (int i = 7; i >= 0; i--) {
        buf[i] = (uint8_t)h[val & 0xFU];
        val >>= 4;
    }
    uart_write_buf(buf, 8);
}

 /* Superloop-only helpers — in FreeRTOS mode the TMTC task handles I/O. */
 #ifndef OBSW_FREERTOS
 static uint8_t uart_getc(void)
 {
     uint8_t b;
     obsw_uart_ops.read(&b, 1, NULL);
     return b;
 }

 static void uart_putc(uint8_t b)
 {
     obsw_uart_ops.write(&b, 1, NULL);
 }

 static void write_tm_packet(const uint8_t *pkt, uint16_t len)
 {
     uint8_t hdr[3] = {0x04, (uint8_t)(len >> 8), (uint8_t)(len & 0xFF)};
     uart_write_buf(hdr, 3);
     uart_write_buf(pkt, len);
 }

 static void flush_tm_store(void)
 {
     uint8_t pkt[OBSW_TM_MAX_PACKET_LEN];
     uint16_t plen = 0;
     while (obsw_tm_store_dequeue(&tm_store, pkt, sizeof(pkt), &plen)
            == OBSW_TM_OK) {
         write_tm_packet(pkt, plen);
     }
 }
 #endif /* !OBSW_FREERTOS */
 
 /* ------------------------------------------------------------------ */
 /* Main                                                                 */
 /* ------------------------------------------------------------------ */
 
 int main(void)
 {
     /* Keep debug interface active during WFI (FreeRTOS idle task) so OpenOCD
      * can connect while the CPU is in sleep mode. Must be set before the
      * scheduler starts — do it as early as possible. */
 #define DBGMCU_CR (*(volatile uint32_t *)(0x5C001000UL + 0x004U))
     DBGMCU_CR |= (1U << 0)   /* DBGSLEEP_D1 */
               |  (1U << 1)   /* DBGSTOP_D1  */
               |  (1U << 2);  /* DBGSTBY_D1  */

     /* Init the clock — skipped under Renode (stub RCC would spin on ready bits) */
 #ifndef OBSW_RENODE
     system_clock_init();
 #endif

     /* Peripheral init */
     obsw_uart_init();
 
     /* OBSW init */
     obsw_tm_store_init(&tm_store);

 #ifndef OBSW_FREERTOS
     /* Superloop: initialise dispatcher and PUS contexts here.
      * FreeRTOS path: these are owned by the PUS task in src/task/pus.c. */
     s1_ctx.tm_store    = &tm_store;
     s1_ctx.apid        = SRDB_APID_DEFAULT;
     s1_ctx.msg_counter = 0;
     s1_ctx.timestamp   = 0;

     s17_ctx.tm_store    = &tm_store;
     s17_ctx.s1          = &s1_ctx;
     s17_ctx.apid        = SRDB_APID_DEFAULT;
     s17_ctx.msg_counter = 0;
     s17_ctx.timestamp   = 0;

     s20_ctx.tm_store    = &tm_store;
     s20_ctx.s1          = &s1_ctx;
     s20_ctx.apid        = SRDB_APID_DEFAULT;
     s20_ctx.table       = s20_params;
     s20_ctx.table_len   = sizeof(s20_params) / sizeof(s20_params[0]);

     obsw_tc_dispatcher_t dispatcher;
     obsw_tc_dispatcher_init(&dispatcher,
                             routes,
                             sizeof(routes) / sizeof(routes[0]),
                             noop_responder, NULL);
 #endif /* !OBSW_FREERTOS */

     /* Boot banner */
     const char *banner =
         "\r\n[OBSW] STM32H750 started (wire protocol v3).\r\n"
         "[OBSW] SRDB version: " SRDB_VERSION "\r\n";
     uart_write_buf((const uint8_t *)banner, (uint16_t)strlen(banner));

     /* Clock register dump — lets us verify SYSCLK source and APB prescalers.
      * SWS (bits[5:3] of RCC_CFGR): 3 = PLL1P active.
      * D2CFGR bits[6:4]=PPREx: 4=/2 → APB1=120 MHz, 5=/4 → APB1=60 MHz. */
     uart_write_buf((const uint8_t *)"[OBSW] CLK RCC_CFGR=", 20);
     uart_print_hex32(RCC_CFGR);
     uart_write_buf((const uint8_t *)" D2CFGR=", 8);
     uart_print_hex32(RCC_D2CFGR);
     uart_write_buf((const uint8_t *)"\r\n", 2);

 #ifdef OBSW_FREERTOS
     /* FreeRTOS path — create tasks then hand control to the scheduler.
      * Init order matters: TMTC (queue), then FDIR (FSM), then PUS (needs both). */
     obsw_tmtc_task_init(&obsw_uart_ops, &tm_store);
     obsw_fdir_task_init(&tm_store);
     obsw_pus_task_init(&tm_store,
                        obsw_tmtc_get_tc_queue(),
                        obsw_tmtc_get_handle(),
                        obsw_fdir_get_fsm());
     obsw_aocs_task_init(obsw_fdir_get_fsm());
     vTaskStartScheduler();
     /* Never reached */
     for (;;);
 #else
     /* Superloop path — wire protocol v3 (used for Renode smoke-tests) */
     uint32_t tick = 0;
     while (1) {
         uint8_t type = uart_getc();

         uint8_t hi = uart_getc();
         uint8_t lo = uart_getc();
         uint16_t frame_len = (uint16_t)((hi << 8) | lo);

         if (frame_len == 0 || frame_len > 512)
             continue;

         uint8_t frame[512];
         for (uint16_t i = 0; i < frame_len; i++)
             frame[i] = uart_getc();

         if (type == OBSW_FRAME_TC) {
             obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);
             flush_tm_store();
         } else if (type == OBSW_FRAME_SENSOR) {
             obsw_sensor_frame_t sensor;
             if (obsw_sim_parse_sensor(frame, frame_len, &sensor))
                 s20_params[0].value.u32 = (uint32_t)sensor.sim_time;
             tick++;
         }

         uart_putc(0xFF);
     }
 #endif
 }