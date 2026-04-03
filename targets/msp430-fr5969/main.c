/**
 * @file main.c
 * @brief openobsw MSP430FR5969 application entry point.
 *
 * Safe-mode OBC implementation. Runs the minimal PUS-C stack
 * suitable for a 2KB SRAM target:
 *
 *   S1  — TC verification
 *   S3  — Housekeeping (nominal + FDIR parameter sets)
 *   S5  — Event reporting (with FDIR safe-trigger coupling)
 *   S6  — Memory management (load / check / dump)
 *   S8  — Function management (recover to NOMINAL)
 *   S17 — Are-you-alive ping
 *   FSM — NOMINAL ↔ SAFE mode
 *   WD  — Software watchdog
 *
 * TC framing: length-prefixed space packets on UART (same protocol
 * as the host sim — the Renode sentinel peripheral and the
 * OBCEmulatorAdapter speak this same format).
 */

#include "obsw/obsw.h"
#include <msp430.h>
#include <msp430.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* UART I/O ops (defined in hal/msp430/uart.c)                        */
/* ------------------------------------------------------------------ */

extern obsw_io_ops_t obsw_uart_ops;
extern void obsw_uart_init(void);

/* ------------------------------------------------------------------ */
/* Application state — all statically allocated                        */
/* ------------------------------------------------------------------ */

static obsw_tm_store_t  tm_store;
static obsw_fsm_ctx_t   fsm;
static obsw_wd_ctx_t    watchdog;

/* Service contexts */
static obsw_s1_ctx_t    s1  = { 0 };
static obsw_s3_ctx_t    s3  = { 0 };
static obsw_s5_ctx_t    s5  = { 0 };
static obsw_s17_ctx_t   s17 = { 0 };
static obsw_s8_ctx_t    s8  = { 0 };

/* ------------------------------------------------------------------ */
/* Housekeeping parameters — live application variables               */
/* ------------------------------------------------------------------ */

static uint16_t param_uptime_s         = 0;
static uint16_t param_safe_entry_count = 0;
static uint32_t param_wd_kick_count    = 0;

static obsw_s3_param_t nominal_hk_params[] = {
    { .ptr = &param_uptime_s,         .size = OBSW_S3_PARAM_U16 },
};

static obsw_s3_param_t fdir_hk_params[] = {
    { .ptr = &param_safe_entry_count, .size = OBSW_S3_PARAM_U16 },
    { .ptr = &param_wd_kick_count,    .size = OBSW_S3_PARAM_U32 },
};

static obsw_s3_set_t hk_sets[] = {
    {
        .set_id         = SRDB_HK_NOMINAL_HK,
        .params         = nominal_hk_params,
        .param_count    = 1,
        .interval_ticks = 10,
        .countdown      = 10,
        .enabled        = true,
    },
    {
        .set_id         = SRDB_HK_FDIR_HK,
        .params         = fdir_hk_params,
        .param_count    = 2,
        .interval_ticks = 60,
        .countdown      = 60,
        .enabled        = true,
    },
};

/* ------------------------------------------------------------------ */
/* S8 function table                                                   */
/* ------------------------------------------------------------------ */

static int fn_recover_nominal(const uint8_t *args, uint8_t args_len,
                               void *ctx)
{
    (void)args; (void)args_len;
    obsw_fsm_to_nominal((obsw_fsm_ctx_t *)ctx);
    return 0;
}

static obsw_s8_entry_t s8_table[] = {
    { .function_id = OBSW_S8_FN_RECOVER_NOMINAL,
      .fn           = fn_recover_nominal,
      .ctx          = &fsm },
};

/* ------------------------------------------------------------------ */
/* FDIR hooks                                                          */
/* ------------------------------------------------------------------ */

static void on_enter_safe(void *ctx)
{
    (void)ctx;
    param_safe_entry_count++;
    /* Mission: switch to safe beacon frequency, disable payload power */
}

/* ------------------------------------------------------------------ */
/* SAFE-mode TC whitelist                                              */
/* ------------------------------------------------------------------ */

static const obsw_fsm_tc_entry_t safe_whitelist[] = {
    { SRDB_TC_ARE_YOU_ALIVE_SVC,      SRDB_TC_ARE_YOU_ALIVE_SUBSVC },
    { SRDB_TC_S8_PERFORM_FUNCTION_SVC, SRDB_TC_S8_PERFORM_FUNCTION_SUBSVC },
};

/* ------------------------------------------------------------------ */
/* TC routing table                                                    */
/* ------------------------------------------------------------------ */

static obsw_tc_route_t routes[] = {
    { .apid = 0xFFFF,
      .service = SRDB_TC_ARE_YOU_ALIVE_SVC,
      .subservice = SRDB_TC_ARE_YOU_ALIVE_SUBSVC,
      .handler = obsw_s17_ping, .ctx = &s17 },

    { .apid = 0xFFFF,
      .service = SRDB_TC_S8_PERFORM_FUNCTION_SVC,
      .subservice = SRDB_TC_S8_PERFORM_FUNCTION_SUBSVC,
      .handler = obsw_s8_perform, .ctx = &s8 },

    { .apid = 0xFFFF,
      .service = SRDB_TC_ENABLE_HK_REPORT_SVC,
      .subservice = SRDB_TC_ENABLE_HK_REPORT_SUBSVC,
      .handler = obsw_s3_enable, .ctx = &s3 },

    { .apid = 0xFFFF,
      .service = SRDB_TC_DISABLE_HK_REPORT_SVC,
      .subservice = SRDB_TC_DISABLE_HK_REPORT_SUBSVC,
      .handler = obsw_s3_disable, .ctx = &s3 },
};

/* ------------------------------------------------------------------ */
/* TM responder — writes TM packets to UART                           */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* No-op responder — TM is drained explicitly after each dispatch     */
/* ------------------------------------------------------------------ */

static void noop_responder(uint8_t f, const obsw_tc_t *t, void *c)
{
    (void)f; (void)t; (void)c;
}

/* ------------------------------------------------------------------ */
/* Watchdog expiry handler                                             */
/* ------------------------------------------------------------------ */

static void on_watchdog_expiry(void *ctx)
{
    obsw_s5_report((obsw_s5_ctx_t *)ctx,
                    OBSW_S5_HIGH,
                    SRDB_EVENT_WATCHDOG_EXPIRY,
                    NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Blocking read helper — spins until exactly len bytes received      */
/* ------------------------------------------------------------------ */

static void uart_read_exact(uint8_t *buf, uint16_t len)
{
    uint16_t received = 0;
    while (received < len) {
        int n = obsw_uart_ops.read(buf + received,
                                    len - received,
                                    obsw_uart_ops.ctx);
        if (n > 0) received += (uint16_t)n;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    /* Stop the hardware watchdog immediately — it starts on power-up
     * with a ~32ms timeout and will reset the chip mid-UART-receive
     * if not disabled. Our software watchdog (obsw_wd_ctx_t) handles
     * FDIR; the hardware WDT is not needed on this target. */
    WDTCTL = WDTPW | WDTHOLD;

    obsw_uart_init();

    /* TM store */
    obsw_tm_store_init(&tm_store);

    /* FSM */
    obsw_fsm_config_t fsm_cfg = {
        .on_enter_safe     = on_enter_safe,
        .on_exit_safe      = NULL,
        .hook_ctx          = NULL,
        .safe_tc_whitelist = safe_whitelist,
        .whitelist_len     = sizeof(safe_whitelist) / sizeof(safe_whitelist[0]),
    };
    obsw_fsm_init(&fsm, &fsm_cfg);

    /* S1 */
    s1.tm_store    = &tm_store;
    s1.apid        = SRDB_APID_DEFAULT;
    s1.msg_counter = 0;
    s1.timestamp   = 0;

    /* S3 */
    s3.tm_store    = &tm_store;
    s3.s1          = &s1;
    s3.apid        = SRDB_APID_DEFAULT;
    s3.sets        = hk_sets;
    s3.set_count   = sizeof(hk_sets) / sizeof(hk_sets[0]);

    /* S5 — wire safe-trigger event IDs from SRDB */
    s5.tm_store           = &tm_store;
    s5.apid               = SRDB_APID_DEFAULT;
    s5.fsm                = (struct obsw_fsm_ctx *)&fsm;
    s5.safe_trigger_count = SRDB_SAFE_TRIGGER_COUNT;
    {
        uint16_t ids[] = SRDB_SAFE_TRIGGER_IDS;
        for (uint8_t i = 0; i < SRDB_SAFE_TRIGGER_COUNT; i++)
            s5.safe_trigger_ids[i] = ids[i];
    }

    /* S17 */
    s17.tm_store = &tm_store;
    s17.s1       = &s1;
    s17.apid     = SRDB_APID_DEFAULT;

    /* S8 */
    s8.tm_store  = &tm_store;
    s8.s1        = &s1;
    s8.apid      = SRDB_APID_DEFAULT;
    s8.table     = s8_table;
    s8.table_len = sizeof(s8_table) / sizeof(s8_table[0]);

    /* Watchdog — 100 ticks timeout */
    obsw_wd_init(&watchdog, 100, on_watchdog_expiry, &s5);

    /* Dispatcher */
    obsw_tc_dispatcher_t dispatcher;
    obsw_tc_dispatcher_init(&dispatcher,
                             routes,
                             sizeof(routes) / sizeof(routes[0]),
                             noop_responder, NULL);

    /* Boot event */
    obsw_s5_report(&s5, OBSW_S5_INFO, SRDB_EVENT_BOOT_COMPLETE, NULL, 0);

    /* ---- Main control loop ---------------------------------------- */
    uint8_t  frame[OBSW_TC_FRAME_MAX_LEN];
    uint32_t tick = 0;

    while (1) {
        /* 1. Kick watchdog */
        obsw_wd_kick(&watchdog);
        param_wd_kick_count++;

        /* 2. Read TC frame — blocking on each byte */
        uint8_t hdr[2];
        uart_read_exact(hdr, 2);
        uint16_t frame_len = (uint16_t)((hdr[0] << 8) | hdr[1]);
        if (frame_len > 0 && frame_len <= sizeof(frame)) {
            uart_read_exact(frame, frame_len);

            /* Filter by FSM mode before dispatching */
            obsw_sp_packet_t pkt;
            if (obsw_sp_parse(frame, frame_len, &pkt) == OBSW_SP_OK) {
                if (obsw_fsm_tc_allowed(&fsm,
                        pkt.payload[1], pkt.payload[2])) {
                    obsw_tc_dispatcher_feed(&dispatcher,
                                             frame, frame_len);
                }
            }

            /* Drain TM store — transmit all enqueued packets */
            {
                uint8_t  tm_pkt[OBSW_TM_MAX_PACKET_LEN];
                uint16_t tm_len = 0;
                while (obsw_tm_store_dequeue(&tm_store,
                           tm_pkt, sizeof(tm_pkt),
                           &tm_len) == OBSW_TM_OK) {
                    uint8_t lhdr[2] = {
                        (uint8_t)(tm_len >> 8),
                        (uint8_t)(tm_len & 0xFFU)
                    };
                    obsw_uart_ops.write(lhdr, 2,
                        obsw_uart_ops.ctx);
                    obsw_uart_ops.write(tm_pkt, tm_len,
                        obsw_uart_ops.ctx);
                }
            }
        }

        /* 3. Periodic tick */
        tick++;
        if (tick >= 1000U) {   /* ~1 Hz at typical loop rate */
            tick = 0;
            param_uptime_s++;
            obsw_s3_tick(&s3);
            obsw_wd_tick(&watchdog);
        }
    }
}