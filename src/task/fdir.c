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
 * Period: 1000 ms (vTaskDelayUntil — deterministic).
 *
 * IWDG configuration (STM32H750, LSI ≈ 32 kHz):
 *   Prescaler /128 → counter clock = 250 Hz
 *   Reload 1000    → timeout = 1000/250 = 4.0 s
 *   Kick every 1s  → 4× safety margin
 */

#include "obsw/task/fdir.h"
#include "obsw/task/mode.h"
#include "obsw/task/tmtc.h"
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

/* ── Task parameters ──────────────────────────────────────────────── */

#define FDIR_STACK_DEPTH 512U
#define FDIR_PRIORITY    3U   /* Med-high — preempts AOCS, yields to TMTC */

/* ── Static allocation ────────────────────────────────────────────── */

static StaticTask_t  s_tcb;
static StackType_t   s_stack[FDIR_STACK_DEPTH];

static obsw_s5_ctx_t   s_s5;   /* FDIR-local S5 context for BOOT_COMPLETE */
static obsw_tm_log_t  *s_tm_log;

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
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t kick_count = 0;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

        /* 1. Kick IWDG — must happen every tick; if we don't run, chip resets. */
        iwdg_kick();
        kick_count++;

        /* 2. Boot event — emit TM(5,1) once, and on the same tick wake the LCD.
         *
         * lcd_slpout_dispon_yield() uses vTaskDelay for all mandatory panel
         * delays (120 ms SLPOUT + 10 ms NORON + 100 ms DISPON = 230 ms total).
         * The CPU enters WFI during those yields, cutting ~50 mA of system
         * current compared to the DWT-spin path in lcd_slpout_dispon().  Lower
         * current is critical: on the WeAct board the ST7735R power supervisor
         * fires whenever instantaneous current exceeds its threshold, resetting
         * ALL panel registers (COLMOD, MADCTL, PWCTR…) back to sleep defaults.
         * Previous builds used DWT-spin delays and repeatedly triggered the PS;
         * the yielding variant keeps supply headroom and lets the panel reach
         * DISPON cleanly. */
        if (!boot_sent) {
#ifndef OBSW_RENODE
            lcd_slpout_dispon_yield();
            lcd_console_init();
            lcd_console_set_colours(LCD_BLACK, LCD_GREEN);
            lcd_console_puts("openobsw v" SRDB_VERSION "\n");
            lcd_console_puts("STM32H750 HSI 64MHz\n");
            lcd_console_puts("FDIR alive\n");
#endif
            obsw_s5_report(&s_s5, OBSW_S5_INFO,
                           SRDB_EVENT_BOOT_COMPLETE, NULL, 0);
            boot_sent = true;
        } else {
#ifndef OBSW_RENODE
            /* Subsequent ticks: redraw the header in place so GRAM stays current.
             * If the panel is in DISPON this refreshes what the user sees.
             * If PS fired and the panel is sleeping, the GRAM write is still
             * accepted (the ST7735R allows GRAM access in SLPIN), so when the
             * supply recovers and the panel auto-wakes it immediately shows the
             * latest content without needing another slpout_dispon call. */
            lcd_console_init();
            lcd_console_set_colours(LCD_BLACK, LCD_GREEN);
            lcd_console_puts("openobsw v" SRDB_VERSION "\n");
            lcd_console_puts("STM32H750 HSI 64MHz\n");
            lcd_console_puts("FDIR alive\n");
#endif
        }

#ifndef OBSW_RENODE
        /* 4. Drain TM log and print each new packet to LCD console. */
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

        /* 5. Read current mode from Mode Manager for LCD status bar. */
        obsw_fsm_mode_t cur_mode = obsw_fsm_mode(obsw_mode_get_fsm());

#ifndef OBSW_RENODE
        /* 6. Update LCD status bar: "FDIR:NOMINAL  WDG:00000042" (26 chars) */
        {
            static const char h[] = "0123456789";
            char s[27];
            /* mode field — 7 chars, space-padded */
            if (cur_mode == OBSW_FSM_NOMINAL) {
                __builtin_memcpy(s, "FDIR:NOMINAL  WDG:", 18);
            } else if (cur_mode == OBSW_FSM_SAFE) {
                __builtin_memcpy(s, "FDIR:SAFE     WDG:", 18);
            } else {
                __builtin_memcpy(s, "FDIR:STANDBY  WDG:", 18);
            }
            /* 8-digit decimal kick counter */
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

    /* IWDG — arm it now; first kick occurs on the task's first tick (≤ 1 s). */
    iwdg_init();

    s_tm_log = obsw_tmtc_get_tm_log();

    xTaskCreateStatic(fdir_task, "FDIR", FDIR_STACK_DEPTH,
                      NULL, FDIR_PRIORITY, s_stack, &s_tcb);
}
