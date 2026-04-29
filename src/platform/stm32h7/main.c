/**
 * @file src/platform/stm32h7/main.c
 * @brief STM32H750VBT6 bare-metal entry point.
 *
 * Initialises the system clock, UART, watchdog (disabled),
 * and the OBSW PUS dispatcher. Runs the wire protocol v3
 * main loop — same protocol as the host sim and ZynqMP target.
 *
 * Clock configuration:
 *   HSI (64MHz) → PLL1 → SYSCLK 480MHz, APB1 120MHz
 *   (Full PLL config coming in v0.8 — currently runs on HSI 64MHz
 *    with APB1 = 32MHz, BRR adjusted accordingly)
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
 
 #include <stdint.h>
 #include <string.h>
 #include <stdio.h>
 
 /* ------------------------------------------------------------------ */
 /* External symbols from startup.S and HAL                             */
 /* ------------------------------------------------------------------ */
 
 extern obsw_io_ops_t obsw_uart_ops;
 extern void          obsw_uart_init(void);
 
 /* ------------------------------------------------------------------ */
 /* OBSW context                                                         */
 /* ------------------------------------------------------------------ */
 
 static obsw_tm_store_t tm_store;
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
 
 /* ------------------------------------------------------------------ */
 /* UART I/O helpers                                                     */
 /* ------------------------------------------------------------------ */
 
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
 
 static void uart_write_buf(const uint8_t *buf, uint16_t len)
 {
     obsw_uart_ops.write(buf, len, NULL);
 }
 
 /* ------------------------------------------------------------------ */
 /* Wire protocol v3 helpers                                             */
 /* ------------------------------------------------------------------ */
 
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
 
 /* ------------------------------------------------------------------ */
 /* Main                                                                 */
 /* ------------------------------------------------------------------ */
 
 int main(void)
 {
     /* Peripheral init */
     obsw_uart_init();
 
     /* OBSW init */
     obsw_tm_store_init(&tm_store);
 
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
                             NULL, NULL);
 
     /* Boot banner — visible on UART terminal */
     const char *banner =
         "\r\n[OBSW] STM32H750 started (type-frame protocol v2).\r\n"
         "[OBSW] SRDB version: " SRDB_VERSION "\r\n";
     uart_write_buf((const uint8_t *)banner, (uint16_t)strlen(banner));
 
     /* Main loop — wire protocol v3 */
     uint32_t tick = 0;
     while (1) {
         uint8_t type = uart_getc();
 
         /* Read length */
         uint8_t hi = uart_getc();
         uint8_t lo = uart_getc();
         uint16_t frame_len = (uint16_t)((hi << 8) | lo);
 
         if (frame_len == 0 || frame_len > 512)
             continue;
 
         /* Read frame body */
         uint8_t frame[512];
         for (uint16_t i = 0; i < frame_len; i++)
             frame[i] = uart_getc();
 
         if (type == OBSW_FRAME_TC) {
             obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);
             flush_tm_store();
         } else if (type == OBSW_FRAME_SENSOR) {
             /* Sensor injection — update S20 uptime param */
             obsw_sensor_frame_t sensor;
             if (obsw_sim_parse_sensor(frame, frame_len, &sensor)) {
                 s20_params[0].value.u32 = (uint32_t)sensor.sim_time;
             }
             tick++;
         }
 
         /* Sync byte after every frame */
         uart_putc(0xFF);
     }
 }