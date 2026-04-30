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

 /**
 * STM32H750 System Clock Configuration
 *
 * Target: SYSCLK = 480 MHz, APB1 = 120 MHz
 * Source: HSE 25 MHz (WeAct board crystal) → PLL1
 *
 * PLL1 configuration:
 *   HSE = 25 MHz
 *   DIVM1 = 5   → PLL1 input = 5 MHz (ref clock)
 *   DIVN1 = 192 → VCO = 960 MHz
 *   DIVP1 = 2   → SYSCLK = 480 MHz
 *   DIVQ1 = 4   → PLL1Q = 240 MHz (USB, SPI etc.)
 *   DIVR1 = 2   → PLL1R = 480 MHz
 *
 * APB prescalers:
 *   PPRE1 (APB1) = /4 → 120 MHz  ← USART3 BRR = 120000000/115200 = 1041
 *   PPRE2 (APB2) = /2 → 240 MHz
 *
 * Add this block at the top of main(), before obsw_uart_init():
 */
static void system_clock_init(void)
{
    /* 1. Boost VOS to VOS0 (required for 480 MHz) */
    /* Enable SYSCFG clock */
    *(volatile uint32_t *)(0x58024400UL + 0x0E4) |= (1U << 1); /* APB4ENR SYSCFGEN */
    /* Set VOS0 via SYSCFG_PWRCR ODEN bit */
    SYSCFG_PWRCR |= (1U << 0);
    /* Wait for VOS ready */
    while (!(PWR_D3CR & (1U << 13)))
        ;

    /* 2. Enable HSE */
    RCC_CR |= (1U << 16);          /* HSEON */
    while (!(RCC_CR & (1U << 17))) /* Wait HSERDY */
        ;

    /* 3. Flash latency for 480 MHz (4 wait states + 2 extra = 6 total, VOS0) */
    FLASH_ACR = (FLASH_ACR & ~0xFU) | 6U;
    FLASH_ACR |= (1U << 8);   /* ARTEN  */
    FLASH_ACR |= (1U << 9);   /* ARTRST — not needed but harmless */

    /* 4. Configure PLL1: HSE/5 * 192 / 2 = 480 MHz */
    /* Select HSE as PLL source, DIVM1=5 */
    RCC_PLLCKSELR = (2U << 0)    /* PLLSRC = HSE */
                  | (5U << 4);   /* DIVM1 = 5 */

    /* DIVN1=192-1=191, DIVP1=2-1=1, DIVQ1=4-1=3, DIVR1=2-1=1 */
    RCC_PLL1DIVR = ((191U) << 0)  /* DIVN1 */
                 | ((1U)   << 9)  /* DIVP1 */
                 | ((3U)   << 16) /* DIVQ1 */
                 | ((1U)   << 24);/* DIVR1 */

    /* Enable PLL1 fractional and wide-range VCO */
    RCC_PLLCFGR = (1U << 0)   /* DIVPEN — enable P output */
                | (1U << 1)   /* DIVQEN — enable Q output */
                | (1U << 2)   /* DIVREN — enable R output */
                | (3U << 2)   /* PLL1RGE: input 4-8 MHz range */
                | (1U << 4);  /* PLL1VCOSEL: wide VCO 192-836 MHz */

    /* 5. Enable PLL1 */
    RCC_CR |= (1U << 24);          /* PLL1ON */
    while (!(RCC_CR & (1U << 25))) /* Wait PLL1RDY */
        ;

    /* 6. Set bus prescalers before switching clock */
    /* D1CFGR: HPRE=/2 (AHB=240MHz), D1PPRE=/2 (APB3=120MHz) */
    RCC_D1CFGR = (8U << 0)   /* HPRE = /2 */
               | (4U << 4);  /* D1PPRE = /2 */

    /* D2CFGR: D2PPRE1=/4 (APB1=120MHz), D2PPRE2=/2 (APB2=240MHz) */
    RCC_D2CFGR = (5U << 4)   /* D2PPRE1 = /4 → APB1 = 120 MHz */
               | (4U << 8);  /* D2PPRE2 = /2 → APB2 = 240 MHz */

    /* D3CFGR: D3PPRE=/2 (APB4=120MHz) */
    RCC_D3CFGR = (4U << 4);

    /* 7. Switch system clock to PLL1P */
    RCC_CFGR = (RCC_CFGR & ~0x7U) | 3U; /* SW = PLL1 */
    while (((RCC_CFGR >> 3) & 0x7U) != 3U) /* Wait SWS = PLL1 */
        ;
}
 
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
     /* Init the clock */
     //system_clock_init();

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