/*
 * FDIR task — fault detection, isolation, and recovery.
 *
 * Responsibilities:
 *   1. IWDG kick — prevents reset as long as FDIR task is alive (hardware proof
 *      that the scheduler is running). A deadlock or hard fault stops the kick
 *      and the chip resets after the 4s timeout.
 *
 *   2. Mode FSM — owns the NOMINAL↔SAFE state machine. Transitions are driven
 *      by S5 HIGH events (automatic via s5.c) or by S8 TC(8,1) recover command.
 *
 *   3. Event telemetry — emits TM(5,1) BOOT_COMPLETE on first tick; emits
 *      TM(5,x) SAFE_MODE_ENTRY / SAFE_MODE_EXIT when mode changes.
 *
 * Period: 1000 ms (vTaskDelayUntil — deterministic).
 *
 * IWDG configuration (STM32H750, LSI ≈ 32 kHz):
 *   Prescaler /128 → counter clock = 250 Hz
 *   Reload 1000    → timeout = 1000/250 = 4.0 s
 *   Kick every 1s  → 4× safety margin
 */

#include "obsw/task/fdir.h"
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

/* ── TC whitelist for SAFE mode ───────────────────────────────────── */

static const obsw_fsm_tc_entry_t s_safe_whitelist[] = {
    {.service = 8,  .subservice = 1},  /* S8(8,1): recover to NOMINAL  */
    {.service = 17, .subservice = 1},  /* S17(17,1): ping               */
    {.service = 20, .subservice = 3},  /* S20(20,3): get parameter      */
};

/* ── FSM hooks ────────────────────────────────────────────────────── */

/* Mode transition events are reported from the task loop (not from these
 * hooks) so they always run in FDIR task context and can call s5_report. */
static void on_enter_safe(void *ctx) { (void)ctx; }
static void on_exit_safe(void *ctx)  { (void)ctx; }

/* ── Static allocation ────────────────────────────────────────────── */

static StaticTask_t  s_tcb;
static StackType_t   s_stack[FDIR_STACK_DEPTH];

static obsw_fsm_ctx_t s_fsm;
static obsw_s5_ctx_t  s_s5;

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
    obsw_fsm_mode_t last_mode = OBSW_FSM_NOMINAL;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t kick_count = 0;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

        /* 1. Kick IWDG — must happen every tick; if we don't run, chip resets. */
        iwdg_kick();
        kick_count++;

        /* 2. Boot event on first tick. */
        if (!boot_sent) {
            obsw_s5_report(&s_s5, OBSW_S5_INFO,
                           SRDB_EVENT_BOOT_COMPLETE, NULL, 0);
            boot_sent = true;
        }

        /* 3. Detect mode transitions and emit events.
         *    Worst-case detection lag = 1 s (one tick period). */
        obsw_fsm_mode_t cur_mode = obsw_fsm_mode(&s_fsm);
        if (cur_mode != last_mode) {
            if (cur_mode == OBSW_FSM_SAFE) {
                obsw_s5_report(&s_s5, OBSW_S5_HIGH,
                               SRDB_EVENT_SAFE_MODE_ENTRY, NULL, 0);
            } else {
                obsw_s5_report(&s_s5, OBSW_S5_INFO,
                               SRDB_EVENT_SAFE_MODE_EXIT, NULL, 0);
            }
            last_mode = cur_mode;
        }

#ifndef OBSW_RENODE
        /* 4. Update LCD status bar: "FDIR:NOMINAL  WDG:00000042" (26 chars) */
        {
            static const char h[] = "0123456789";
            char s[27];
            /* mode field — 7 chars, space-padded */
            if (cur_mode == OBSW_FSM_NOMINAL) {
                __builtin_memcpy(s,     "FDIR:NOMINAL  WDG:", 18);
            } else {
                __builtin_memcpy(s,     "FDIR:SAFE     WDG:", 18);
            }
            /* 8-digit decimal kick counter */
            uint32_t k = kick_count;
            for (int8_t i = 7; i >= 0; i--) {
                s[18 + i] = h[k % 10U]; k /= 10U;
            }
            s[26] = '\0';
            uint16_t fg = (cur_mode == OBSW_FSM_NOMINAL) ? LCD_BLACK : LCD_WHITE;
            uint16_t bg = (cur_mode == OBSW_FSM_NOMINAL) ? LCD_GREEN  : LCD_RED;
            lcd_console_set_status(s, fg, bg);
        }
#endif
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

obsw_fsm_ctx_t *obsw_fdir_get_fsm(void)
{
    return &s_fsm;
}

void obsw_fdir_task_init(obsw_tm_store_t *tm_store)
{
    /* FSM */
    obsw_fsm_config_t fsm_cfg = {
        .on_enter_safe      = on_enter_safe,
        .on_exit_safe       = on_exit_safe,
        .hook_ctx           = NULL,
        .safe_tc_whitelist  = s_safe_whitelist,
        .whitelist_len      = (uint8_t)(sizeof(s_safe_whitelist) /
                                        sizeof(s_safe_whitelist[0])),
    };
    obsw_fsm_init(&s_fsm, &fsm_cfg);

    /* S5 — HIGH events whose IDs appear in safe_trigger_ids auto-call fsm_to_safe */
    s_s5.tm_store            = tm_store;
    s_s5.apid                = SRDB_APID_DEFAULT;
    s_s5.msg_counter         = 0;
    s_s5.timestamp           = 0;
    s_s5.fsm                 = &s_fsm;
    s_s5.safe_trigger_ids[0] = SRDB_EVENT_TEMPERATURE_HARD_LIMIT;
    s_s5.safe_trigger_ids[1] = SRDB_EVENT_POWER_FAULT_3V3;
    s_s5.safe_trigger_ids[2] = SRDB_EVENT_POWER_FAULT_5V;
    s_s5.safe_trigger_count  = 3;

    /* IWDG — arm it now; first kick occurs on the task's first tick (≤ 1 s). */
    iwdg_init();

    xTaskCreateStatic(fdir_task, "FDIR", FDIR_STACK_DEPTH,
                      NULL, FDIR_PRIORITY, s_stack, &s_tcb);
}
