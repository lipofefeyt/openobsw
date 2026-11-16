/*
 * Mode Manager task — satellite mode lifecycle (STANDBY / SAFE / NOMINAL).
 *
 * This task owns the mode FSM.  It is the single authority for nominal
 * mode transitions; FDIR calls obsw_mode_request_safe() for fault-triggered
 * fallback and S8 handlers call obsw_mode_request_nominal/standby() for
 * ground-commanded transitions.
 *
 * Period: 1 000 ms (vTaskDelayUntil).
 * Priority: 2 — below FDIR/PUS, above AOCS.
 *
 * Auto-transition (LEOP autonomy, OBSW-SRS-187..189): STANDBY → SAFE after
 * the S20 parameter standby_safe_timeout seconds (default 300, build option
 * OBSW_STANDBY_SAFE_TIMEOUT_S) without ground contact, so detumbling starts
 * even if the first pass is missed. Any well-formed TC restarts the
 * countdown; the value is re-read every second so TC(20,1) applies at once.
 */

#include "obsw/task/mode.h"
#include "obsw/fdir/standby_timer.h"
#include "obsw/pus/s5.h"
#include "obsw/task/pus.h"
#include "obsw/srdb_generated.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#define MODE_STACK_DEPTH  256U
#define MODE_PRIORITY       2U

/* ── TC whitelist for non-NOMINAL modes ──────────────────────────────── */

/* telecommands.yaml safe_allowed: TC(17,1), (8,1), (9,128), (20,3) */
static const obsw_fsm_tc_entry_t s_whitelist[] = SRDB_SAFE_TC_WHITELIST;

/* ── Static allocation ───────────────────────────────────────────────── */

static StaticTask_t  s_tcb;
static StackType_t   s_stack[MODE_STACK_DEPTH];

static obsw_fsm_ctx_t s_fsm;
static obsw_s5_ctx_t  s_s5;

/* ── Transition request flags (set from any task, consumed by mode task) */

static volatile bool s_req_safe     = false;
static volatile bool s_req_nominal  = false;
static volatile bool s_req_standby  = false;
static volatile bool s_ground_contact = false;  /* set by PUS, consumed here */

/* ── FSM hooks ───────────────────────────────────────────────────────── */

/* Both run inside an FSM transition, in whichever task commanded it (Mode
 * Manager, or FDIR for an S12 safe trigger) and with a critical section held:
 * keep them short and non-blocking. */
static void on_enter_safe(void *ctx) { (void)ctx; }
static void on_exit_safe(void *ctx)  { (void)ctx; }

/* ── Task body ───────────────────────────────────────────────────────── */

static void mode_task(void *param)
{
    (void)param;
    obsw_standby_timer_t standby_timer;
    obsw_standby_timer_reset(&standby_timer);
    obsw_fsm_mode_t last = obsw_fsm_mode(&s_fsm);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

        /* The FSM has a second writer: an S5 context wired to it commands
         * SAFE from the FDIR task (S12 monitoring, obsw_pus_s12_check(), which
         * holds a critical section). FDIR has the higher priority, so without
         * a lock here it could run in the middle of one of the transitions
         * below. Every transition in this task is therefore made inside a
         * critical section too, with the mode read inside it. */
        uint32_t timeout_s = obsw_pus_s20_get_u32(SRDB_PARAM_STANDBY_SAFE_TIMEOUT,
                                                  OBSW_STANDBY_SAFE_TIMEOUT_S);

        taskENTER_CRITICAL();

        /* ── Process pending requests ──────────────────────────────── */

        obsw_fsm_mode_t cur = obsw_fsm_mode(&s_fsm);
        if (s_req_safe) {
            s_req_safe = false;
            obsw_fsm_to_safe(&s_fsm);
        } else if (s_req_standby) {
            s_req_standby = false;
            obsw_fsm_to_standby(&s_fsm);
        } else if (s_req_nominal) {
            s_req_nominal = false;
            if (cur == OBSW_FSM_SAFE)
                obsw_fsm_to_nominal(&s_fsm);
        }

        /* ── Auto-transition: STANDBY → SAFE after timeout ─────────── */

        cur = obsw_fsm_mode(&s_fsm);
        bool contact = s_ground_contact;
        s_ground_contact = false;
        if (obsw_standby_timer_tick(&standby_timer, cur, timeout_s, contact))
            obsw_fsm_to_safe(&s_fsm);

        /* ── Detect transitions and emit TM(5,x) events ─────────────── */

        cur = obsw_fsm_mode(&s_fsm);

        taskEXIT_CRITICAL();
        if (cur != last) {
            switch (cur) {
            case OBSW_FSM_SAFE:
                obsw_s5_report(&s_s5, OBSW_S5_HIGH,
                               SRDB_EVENT_SAFE_MODE_ENTRY, NULL, 0);
                break;
            case OBSW_FSM_NOMINAL:
                obsw_s5_report(&s_s5, OBSW_S5_INFO,
                               SRDB_EVENT_NOMINAL_ENTRY, NULL, 0);
                break;
            case OBSW_FSM_STANDBY:
                obsw_s5_report(&s_s5, OBSW_S5_INFO,
                               SRDB_EVENT_STANDBY_ENTRY, NULL, 0);
                break;
            default:
                break;
            }
            last = cur;
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void obsw_mode_task_init(obsw_tm_store_t *tm_store)
{
    obsw_fsm_config_t cfg = {
        .on_enter_safe     = on_enter_safe,
        .on_exit_safe      = on_exit_safe,
        .hook_ctx          = NULL,
        .safe_tc_whitelist = s_whitelist,
        .whitelist_len     = SRDB_SAFE_TC_COUNT,
    };
    obsw_fsm_init(&s_fsm, &cfg);   /* starts in STANDBY */

    s_s5.tm_store          = tm_store;
    s_s5.apid              = SRDB_APID_DEFAULT;
    s_s5.msg_counter       = 0;
    s_s5.timestamp         = 0;
    s_s5.fsm               = NULL; /* Mode Manager emits events; no re-trigger */
    s_s5.safe_trigger_count = 0;

    xTaskCreateStatic(mode_task, "MODE", MODE_STACK_DEPTH,
                      NULL, MODE_PRIORITY, s_stack, &s_tcb);
}

obsw_fsm_ctx_t *obsw_mode_get_fsm(void)
{
    return &s_fsm;
}

void obsw_mode_request_safe(void)
{
    s_req_safe    = true;
    s_req_nominal = false;   /* clear lower-priority pending requests */
}

void obsw_mode_request_nominal(void)
{
    s_req_nominal = true;
}

void obsw_mode_note_ground_contact(void)
{
    s_ground_contact = true;
}

void obsw_mode_request_standby(void)
{
    s_req_standby = true;
    s_req_nominal = false;
}
