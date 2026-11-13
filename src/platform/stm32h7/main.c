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
 #include "obsw/task/mode.h"
 #include "obsw/task/pus.h"
 #include "obsw/task/aocs.h"
 #include "obsw/task/fdir.h"
 #endif

 #include "obsw/ui/dashboard.h"
 #include "obsw/version.h"

 #ifndef OBSW_RENODE
 #include "obsw/hal/stm32h7/spi.h"
 #include "obsw/hal/stm32h7/lcd.h"
 #include "obsw/hal/stm32h7/i2c.h"
 #endif

 #include <stdint.h>
 #include <string.h>

/* Flash latency register — used by system_clock_init() */
#define FLASH_ACR       (*(volatile uint32_t *)(0x52002000UL + 0x000))

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

 #ifndef OBSW_RENODE
 /* RCC_RSR as read at boot, before RMVF clears it. Read over SWD
  * (e.g. OpenOCD "mdw &s_boot_rcc_rsr") to see every reset flag that was set. */
 static volatile uint32_t s_boot_rcc_rsr;
 #endif

 /* Superloop path owns the dispatcher and PUS contexts directly.
  * In the FreeRTOS path these live in src/task/pus.c instead. */
 #ifndef OBSW_FREERTOS
 static obsw_s1_ctx_t   s1_ctx  = {0};
 static obsw_s17_ctx_t  s17_ctx = {0};
 static obsw_s20_ctx_t  s20_ctx = {0};

 static obsw_s20_param_t s20_params[] = {
     {.param_id = SRDB_PARAM_OBC_UPTIME, .value = {.u32 = 0},
      .type = SRDB_PARAM_OBC_UPTIME_S20_TYPE},
     {.param_id = SRDB_PARAM_SAFE_MODE_ENTRY_COUNT, .value = {.u32 = 0},
      .type = SRDB_PARAM_SAFE_MODE_ENTRY_COUNT_S20_TYPE},
     {.param_id = SRDB_PARAM_WATCHDOG_KICK_COUNT, .value = {.u32 = 0},
      .type = SRDB_PARAM_WATCHDOG_KICK_COUNT_S20_TYPE},
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
                             obsw_s1_dispatch_responder, &s1_ctx);
 #endif /* !OBSW_FREERTOS */

     /* DBGMCU: keep SWD alive during WFI — placed after uart_init so a fault
      * here is visible on UART rather than causing a silent early hang. */
 #define DBGMCU_CR (*(volatile uint32_t *)(0x5C001000UL + 0x004U))
     DBGMCU_CR |= (1U << 0) | (1U << 1) | (1U << 2);

     /* Boot banner — UART first, always, before any LCD init that might hang */
     const char *banner =
         "\r\n[OBSW] STM32H750 started (wire protocol v3).\r\n"
         "[OBSW] openobsw v" OBSW_VERSION_STRING "\r\n"
         "[OBSW] SRDB version: " SRDB_VERSION "\r\n";
     uart_write_buf((const uint8_t *)banner, (uint16_t)strlen(banner));

 #ifndef OBSW_RENODE
     /* Report the reset source from RCC_RSR (RCC base 0x58024400, RSR 0x0D0).
      * Bit positions from RM0433 / CMSIS RCC_RSR_*_Pos, confirmed on the bench
      * (NRST button → 0x00460000 = PINRSTF | CPURSTF):
      *   21 BORRSTF, 22 PINRSTF, 23 PORRSTF, 24 SFTRSTF, 26 IWDG1RSTF, 28 WWDG1RSTF
      * Flags accumulate — every system reset also sets PINRSTF and a power-on
      * also sets BORRSTF — so decode the most specific source first. */
 #define RCC_RSR     (*(volatile uint32_t *)(0x58024400UL + 0x0D0U))
 #define RSR_RMVF    (1U << 16)
 #define RSR_BOR     (1U << 21)
 #define RSR_PIN     (1U << 22)
 #define RSR_POR     (1U << 23)
 #define RSR_SFT     (1U << 24)
 #define RSR_IWDG1   (1U << 26)
 #define RSR_WWDG1   (1U << 28)
     {
         uint32_t rsr = RCC_RSR;
         s_boot_rcc_rsr = rsr;   /* raw flags, kept for SWD inspection */
         RCC_RSR = rsr | RSR_RMVF;     /* clear all reset flags for next boot */
         obsw_dash_reset_t cause;
         const char *rsrc;
         if (rsr & RSR_IWDG1)    { cause = OBSW_DASH_RESET_IWDG; rsrc = "[OBSW] Reset: IWDG\r\n"; }
         else if (rsr & RSR_WWDG1) { cause = OBSW_DASH_RESET_WWDG; rsrc = "[OBSW] Reset: WWDG\r\n"; }
         else if (rsr & RSR_SFT) { cause = OBSW_DASH_RESET_SW;   rsrc = "[OBSW] Reset: Software\r\n"; }
         else if (rsr & RSR_POR) { cause = OBSW_DASH_RESET_POR;  rsrc = "[OBSW] Reset: POR (power-on)\r\n"; }
         else if (rsr & RSR_BOR) { cause = OBSW_DASH_RESET_BOR;  rsrc = "[OBSW] Reset: BOR (supply)\r\n"; }
         else if (rsr & RSR_PIN) { cause = OBSW_DASH_RESET_PIN;  rsrc = "[OBSW] Reset: NRST pin\r\n"; }
         else                    { cause = OBSW_DASH_RESET_UNKNOWN; rsrc = "[OBSW] Reset: unknown\r\n"; }
 #ifdef OBSW_FREERTOS
         obsw_fdir_set_reset_cause(cause);
 #else
         (void)cause;
 #endif
         uart_write_buf((const uint8_t *)rsrc, (uint16_t)__builtin_strlen(rsrc));
     }
 #endif

 /* LCD cold init before the scheduler: SWRESET + config (~0.6 s blocking),
  * clear the previous run's picture out of GRAM, then backlight on. FDIR
  * draws the dashboard from its first full tick. */
 #ifndef OBSW_RENODE
     obsw_i2c1_init();
     obsw_spi4_init();
     lcd_init();
     lcd_clear(LCD_BLACK);
     lcd_backlight_on();
 #endif

 #ifdef OBSW_FREERTOS
     /* FreeRTOS path — create tasks then hand control to the scheduler.
      * Init order: TMTC (queue) → MODE (FSM owner) → FDIR (uses FSM ptr) →
      *             PUS (uses FSM gate + mode requests) → AOCS (reads mode). */
     obsw_tmtc_task_init(&obsw_uart_ops, &tm_store);
     obsw_mode_task_init(&tm_store);
     obsw_fdir_task_init(&tm_store);
     obsw_pus_task_init(&tm_store,
                        obsw_tmtc_get_tc_queue(),
                        obsw_tmtc_get_handle());
     obsw_aocs_task_init();
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
