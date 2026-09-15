/*
 * FDIR task — fault detection, isolation, and recovery.
 *
 * Responsibilities:
 *   1. IWDG kick — prevents reset as long as FDIR task is alive (hardware proof
 *      that the scheduler is running). A deadlock or hard fault stops the kick
 *      and the chip resets after the 4s timeout.
 *
 *   2. Event telemetry — emits TM(5,1) BOOT_COMPLETE on first tick; monitors
 *      POWER and TEMPERATURE faults and emits TM(5,x) events. S5 HIGH events
 *      with matching trigger IDs call obsw_fsm_to_safe() on the Mode Manager's
 *      FSM directly via the s5_ctx.fsm pointer.
 *
 *   3. LCD status — updates the status bar each tick with current mode and
 *      watchdog kick count.
 *
 * The mode FSM is owned by the Mode Manager task; FDIR is a pure fault
 * responder and never initiates nominal-mode transitions.
 *
 * Sub-tick period: 200 ms (vTaskDelayUntil — deterministic).
 * Full-tick period: 1000 ms (every 5th sub-tick).
 *
 * The ST7735R power supervisor fires ~250 ms after DISPON if the supply
 * ripples from sustained CPU + AOCS I2C + panel scan current.  lcd_slpout_dispon_fast()
 * is called every 200 ms sub-tick (< 250 ms PS window) to restore panel
 * registers before the PS can reset them; GRAM is preserved so the display
 * appears continuously on.  Content updates (console rewrite, status bar,
 * TM log drain) happen only on the 1000 ms full-tick to avoid triggering the
 * PS with sustained SPI bursts.
 *
 * IWDG configuration (STM32H750, LSI ≈ 32 kHz):
 *   Prescaler /128 → counter clock = 250 Hz
 *   Reload 1000    → timeout = 1000/250 = 4.0 s
 *   Kick every 1s  → 4× safety margin
 */

#include "obsw/task/fdir.h"
#include "obsw/task/mode.h"
#include "obsw/task/tmtc.h"
#include "obsw/task/aocs.h"
#include "obsw/pus/s3.h"
#include "obsw/pus/s5.h"
#include "obsw/srdb_generated.h"
#include "FreeRTOS.h"
#include "task.h"

#ifndef OBSW_RENODE
#include "obsw/hal/stm32h7/lcd_console.h"
#include "obsw/hal/stm32h7/lcd.h"
#endif

/* ── IWDG registers (STM32H750, D3 domain) ───────────────────────── */

#define IWDG_BASE  0x58004800UL
#define IWDG_KR    (*(volatile uint32_t *)(IWDG_BASE + 0x00U))
#define IWDG_PR    (*(volatile uint32_t *)(IWDG_BASE + 0x04U))
#define IWDG_RLR   (*(volatile uint32_t *)(IWDG_BASE + 0x08U))
#define IWDG_SR    (*(volatile uint32_t *)(IWDG_BASE + 0x0CU))

#define IWDG_KEY_REFRESH 0xAAAAU
#define IWDG_KEY_UNLOCK  0x5555U
#define IWDG_KEY_START   0xCCCCU

#define IWDG_PR_DIV128   5U     /* /128 → 250 Hz with 32 kHz LSI */
#define IWDG_RELOAD_4S   1000U  /* 1000 × 4 ms = 4.0 s           */

/* ── User LED (PE3, active-low) — debug heartbeat ────────────────── */

#define GPIOE_MODER (*(volatile uint32_t *)0x58021000UL)
#define GPIOE_BSRR  (*(volatile uint32_t *)0x58021018UL)

static void led_init(void)
{
    /* GPIOE clock already enabled by spi.c.  Configure PE3 as push-pull output. */
    GPIOE_MODER = (GPIOE_MODER & ~(3U << 6)) | (1U << 6);
    GPIOE_BSRR  = (1U << (3 + 16));  /* LED off (active-low: pin high = off) */
}

static void led_toggle(void)
{
    /* Read ODR bit 3 and flip it via BSRR (no read-modify-write on ODR needed). */
    uint32_t odr = *(volatile uint32_t *)0x58021014UL;
    if (odr & (1U << 3))
        GPIOE_BSRR = (1U << (3 + 16));  /* was high → pull low (LED on) */
    else
        GPIOE_BSRR = (1U << 3);          /* was low  → push high (LED off) */
}

/* ── UART debug trace (direct register access, same as fault_puts) ─── *
 * Prints raw characters to USART3 without going through the TMTC layer.
 * Output appears between TM packets on the serial port; monitor with:
 *   screen /dev/ttyACM0 115200   (or the appropriate port)
 * Key: 'B' = blocking first-init, 'k' = fast keepalive, 'F' = full tick,
 *      '!' = AOCS disabled diagnostic build.                            */

#define DBG_USART3_TDR (*(volatile uint32_t *)(0x40004800UL + 0x28U))
#define DBG_USART3_ISR (*(volatile uint32_t *)(0x40004800UL + 0x1CU))
#define DBG_USART_TXE  (1U << 7)

static void dbg_putc(char c)
{
    while (!(DBG_USART3_ISR & DBG_USART_TXE));
    DBG_USART3_TDR = (uint8_t)c;
}

static void dbg_puts(const char *s)
{
    while (*s) dbg_putc(*s++);
}

/* ── Task parameters ──────────────────────────────────────────────── */

#define FDIR_STACK_DEPTH 512U
#define FDIR_PRIORITY    3U   /* Med-high — preempts AOCS, yields to TMTC */

/* ── Static allocation ────────────────────────────────────────────── */

static StaticTask_t  s_tcb;
static StackType_t   s_stack[FDIR_STACK_DEPTH];

static obsw_s5_ctx_t   s_s5;   /* FDIR-local S5 context for BOOT_COMPLETE */
static obsw_tm_log_t  *s_tm_log;

/* ── S3 Housekeeping ──────────────────────────────────────────────── */

/* SID 2 — FDIR health parameters */
static uint32_t s_safe_mode_count = 0;
static uint32_t s_kick_count_hk   = 0; /* copy updated before each S3 tick */
static uint32_t s_wdg_ticks_rem   = 4; /* constant: timeout / kick_interval */

static obsw_s3_param_t s_fdir_hk_params[] = {
    {.ptr = &s_safe_mode_count, .size = OBSW_S3_PARAM_U32},
    {.ptr = &s_kick_count_hk,   .size = OBSW_S3_PARAM_U32},
    {.ptr = &s_wdg_ticks_rem,   .size = OBSW_S3_PARAM_U32},
};

/* SID 7 — AOCS B-dot HK (floats reported as raw 4-byte LE IEEE 754) */
static obsw_s3_param_t s_aocs_bdot_hk_params[7]; /* filled in init from AOCS HK ptr */

static obsw_s3_set_t s_hk_sets[] = {
    {
        .set_id = SRDB_HK_FDIR_HK,
        .params = s_fdir_hk_params,
        .param_count = 3,
        .interval_ticks = 60,
        .countdown = 60,
        .enabled = false,
    },
    {
        .set_id = SRDB_HK_AOCS_BDOT_HK,
        .params = s_aocs_bdot_hk_params,
        .param_count = 7,
        .interval_ticks = 10,
        .countdown = 10,
        .enabled = false,
    },
};

static obsw_s3_ctx_t s_s3;

/* ── IWDG ─────────────────────────────────────────────────────────── */

static void iwdg_init(void)
{
    /* 0xCCCC must come first: it starts the IWDG and forces the LSI
     * oscillator ON.  PVU/RVU in IWDG_SR can only clear once LSI is
     * running, so writing 0x5555 before 0xCCCC leaves the while-loop
     * below deadlocked in software-watchdog mode. */
    IWDG_KR  = IWDG_KEY_START;   /* enable IWDG + force LSI ON */
    IWDG_KR  = IWDG_KEY_UNLOCK;
    IWDG_PR  = IWDG_PR_DIV128;
    IWDG_RLR = IWDG_RELOAD_4S;
    while (IWDG_SR & 0x3U)       /* wait for PVU and RVU to clear */
        ;
    IWDG_KR = IWDG_KEY_REFRESH;  /* reload to new 4 s value */
}

static void iwdg_kick(void)
{
    IWDG_KR = IWDG_KEY_REFRESH;
}

/* ── Task body ────────────────────────────────────────────────────── */

static void fdir_task(void *param)
{
    (void)param;

    bool boot_sent = false;
    bool lcd_first = true;   /* separate from boot_sent — set after first SLPOUT */
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t kick_count = 0;
    obsw_fsm_mode_t last_mode = obsw_fsm_mode(obsw_mode_get_fsm());
    uint8_t sub_tick = 0;   /* counts 200 ms steps; full tick every 5 */

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(200));

        /* 1. Kick IWDG every 200 ms sub-tick (4 s timeout → 20× safety margin). */
        iwdg_kick();

        /* 2. LCD keepalive — re-issue SLPOUT+DISPON every 200 ms.
         *    The ST7735R power supervisor resets panel registers to SLPIN when
         *    the supply ripples under load (CPU + AOCS I2C + panel scan).  The
         *    PS fires ~250 ms after DISPON, so calling lcd_slpout_dispon_fast()
         *    every 200 ms (< 250 ms) beats it.  GRAM is preserved, so the
         *    display content is visible immediately after each DISPON.
         *
         *    lcd_first uses the BLOCKING (DWT spin) variant so that the AOCS
         *    task cannot preempt FDIR during the 120 ms SLPOUT wait — AOCS I2C
         *    reads are the primary supply transient suspected of re-triggering
         *    the PS.  After the first init lcd_first = false, so subsequent
         *    keepalives use the yielding fast path (10 ms). */
#ifndef OBSW_RENODE
        if (lcd_first) {
            lcd_slpout_dispon();   /* blocking DWT spin — AOCS cannot preempt */
            lcd_first = false;
            dbg_putc('B');         /* Blocking first-init fired */
        } else {
            lcd_slpout_dispon_fast();
            dbg_putc('k');         /* keepalive fired */
        }
#endif

        sub_tick++;
        if (sub_tick < 5) continue;   /* non-content sub-ticks done */
        sub_tick = 0;

        /* ── Full 1000 ms tick below ─────────────────────────────── */

        dbg_puts("F\r\n");   /* full tick marker — appears every 1 s on serial monitor */

        /* 3. Heartbeat LED (PE3, 1 Hz) and HK kick counter. */
        kick_count++;
        s_kick_count_hk = kick_count;
        led_toggle();

        /* 4. LCD content update — rewrite header + drain TM log. */
#ifndef OBSW_RENODE
        lcd_console_init();
        lcd_console_set_colours(LCD_BLACK, LCD_GREEN);
        lcd_console_puts("openobsw v" SRDB_VERSION "\n");
        lcd_console_puts("STM32H750 HSI 64MHz\n");
        lcd_console_puts("FDIR alive\n");
#endif

        /* 5. Boot event — emit TM(5,1) BOOT_COMPLETE once. */
        if (!boot_sent) {
            obsw_s5_report(&s_s5, OBSW_S5_INFO,
                           SRDB_EVENT_BOOT_COMPLETE, NULL, 0);
            boot_sent = true;
        }

#ifndef OBSW_RENODE
        /* 6. Drain TM log and print each new packet to LCD console. */
        while (s_tm_log->read_idx != s_tm_log->write_idx) {
            obsw_tm_log_entry_t e =
                s_tm_log->buf[s_tm_log->read_idx % OBSW_TM_LOG_DEPTH];
            s_tm_log->read_idx++;
            char ln[12];
            uint8_t k = 0;
            ln[k++] = 'T'; ln[k++] = 'M'; ln[k++] = '(';
            if (e.svc    >= 10U) ln[k++] = (char)('0' + e.svc    / 10U);
            ln[k++] = (char)('0' + e.svc    % 10U);
            ln[k++] = ',';
            if (e.subsvc >= 10U) ln[k++] = (char)('0' + e.subsvc / 10U);
            ln[k++] = (char)('0' + e.subsvc % 10U);
            ln[k++] = ')'; ln[k++] = '\n'; ln[k] = '\0';
            lcd_console_puts(ln);
        }
#endif

        /* 7. Read current mode; track SAFE entries for HK. */
        obsw_fsm_mode_t cur_mode = obsw_fsm_mode(obsw_mode_get_fsm());
        if (cur_mode == OBSW_FSM_SAFE && last_mode != OBSW_FSM_SAFE)
            s_safe_mode_count++;
        last_mode = cur_mode;

        /* 8. Tick S3 HK — one second per call matches interval_ticks units. */
        obsw_s3_tick(&s_s3);

#ifndef OBSW_RENODE
        /* 9. Update LCD status bar: "FDIR:NOMINAL  WDG:00000042" (26 chars) */
        {
            static const char h[] = "0123456789";
            char s[27];
            if (cur_mode == OBSW_FSM_NOMINAL) {
                __builtin_memcpy(s, "FDIR:NOMINAL  WDG:", 18);
            } else if (cur_mode == OBSW_FSM_SAFE) {
                __builtin_memcpy(s, "FDIR:SAFE     WDG:", 18);
            } else {
                __builtin_memcpy(s, "FDIR:STANDBY  WDG:", 18);
            }
            uint32_t k = kick_count;
            for (int8_t i = 7; i >= 0; i--) {
                s[18 + i] = h[k % 10U]; k /= 10U;
            }
            s[26] = '\0';
            uint16_t fg = (cur_mode == OBSW_FSM_NOMINAL) ? LCD_BLACK :
                          (cur_mode == OBSW_FSM_SAFE)    ? LCD_WHITE : LCD_BLACK;
            uint16_t bg = (cur_mode == OBSW_FSM_NOMINAL) ? LCD_GREEN :
                          (cur_mode == OBSW_FSM_SAFE)    ? LCD_RED   : LCD_YELLOW;
            lcd_console_set_status(s, fg, bg);
        }
#endif
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

void obsw_fdir_task_init(obsw_tm_store_t *tm_store)
{
    /* S5 — HIGH events with matching IDs call obsw_fsm_to_safe() on Mode Manager's FSM */
    s_s5.tm_store            = tm_store;
    s_s5.apid                = SRDB_APID_DEFAULT;
    s_s5.msg_counter         = 0;
    s_s5.timestamp           = 0;
    s_s5.fsm                 = obsw_mode_get_fsm();
    s_s5.safe_trigger_ids[0] = SRDB_EVENT_TEMPERATURE_HARD_LIMIT;
    s_s5.safe_trigger_ids[1] = SRDB_EVENT_POWER_FAULT_3V3;
    s_s5.safe_trigger_ids[2] = SRDB_EVENT_POWER_FAULT_5V;
    s_s5.safe_trigger_count  = 3;

    /* S3 — wire AOCS B-dot HK params to live AOCS HK state struct.
     * obsw_aocs_task_init() must be called before obsw_fdir_task_init() so
     * the AOCS HK pointer is valid. */
    const obsw_aocs_hk_t *hk = obsw_aocs_get_hk();
    s_aocs_bdot_hk_params[0] = (obsw_s3_param_t){.ptr = (const void *)&hk->mag_x,    .size = OBSW_S3_PARAM_U32};
    s_aocs_bdot_hk_params[1] = (obsw_s3_param_t){.ptr = (const void *)&hk->mag_y,    .size = OBSW_S3_PARAM_U32};
    s_aocs_bdot_hk_params[2] = (obsw_s3_param_t){.ptr = (const void *)&hk->mag_z,    .size = OBSW_S3_PARAM_U32};
    s_aocs_bdot_hk_params[3] = (obsw_s3_param_t){.ptr = (const void *)&hk->m_cmd_x,  .size = OBSW_S3_PARAM_U32};
    s_aocs_bdot_hk_params[4] = (obsw_s3_param_t){.ptr = (const void *)&hk->m_cmd_y,  .size = OBSW_S3_PARAM_U32};
    s_aocs_bdot_hk_params[5] = (obsw_s3_param_t){.ptr = (const void *)&hk->m_cmd_z,  .size = OBSW_S3_PARAM_U32};
    s_aocs_bdot_hk_params[6] = (obsw_s3_param_t){.ptr = (const void *)&hk->mag_valid, .size = OBSW_S3_PARAM_U8};

    s_s3.tm_store  = tm_store;
    s_s3.s1        = NULL;  /* S3 tick sends TM(3,25) directly; no S1 wrapping */
    s_s3.apid      = SRDB_APID_DEFAULT;
    s_s3.msg_counter = 0;
    s_s3.timestamp   = 0;
    s_s3.sets      = s_hk_sets;
    s_s3.set_count = sizeof(s_hk_sets) / sizeof(s_hk_sets[0]);

    /* IWDG — arm it now; first kick occurs on the task's first tick (≤ 1 s). */
    iwdg_init();
    led_init();

    s_tm_log = obsw_tmtc_get_tm_log();

    xTaskCreateStatic(fdir_task, "FDIR", FDIR_STACK_DEPTH,
                      NULL, FDIR_PRIORITY, s_stack, &s_tcb);
}

obsw_s3_ctx_t *obsw_fdir_get_s3_ctx(void)
{
    return &s_s3;
}
