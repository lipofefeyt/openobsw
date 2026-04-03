/**
 * @file main.c
 * @brief Host simulation harness for openobsw.
 *
 * Binary pipe protocol (stdin/stdout) for OpenSVF OBCEmulatorAdapter:
 *
 *   stdin:  [uint16 BE length][TC frame bytes] ...
 *   stdout: [uint16 BE length][TM packet bytes] ...  (per TM packet)
 *           [0xFF]                                    (sync byte per tick)
 *
 * The sync byte signals end of one control cycle to the OBCEmulatorAdapter,
 * allowing OpenSVF SimulationMaster to advance simulation time (lockstep).
 *
 * All human-readable diagnostics go to stderr so stdout stays binary-clean.
 *
 * Usage:
 *   ./build/sim/obsw_sim           (driven by OBCEmulatorAdapter via pipes)
 *   python3 sim/send_ping.py       (manual test)
 */
#include "obsw/obsw.h"
#include "obsw/srdb_generated.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Binary stdout helpers                                               */
/* ------------------------------------------------------------------ */

static void write_tm_packet(const uint8_t *pkt, uint16_t len)
{
    uint8_t hdr[2] = {(uint8_t)(len >> 8), (uint8_t)(len & 0xFFU)};
    fwrite(hdr, 1, 2, stdout);
    fwrite(pkt, 1, len, stdout);
    fflush(stdout);
}

static void write_sync_byte(void)
{
    uint8_t sync = 0xFFU;
    fwrite(&sync, 1, 1, stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* TM drain — flush all queued TM packets to stdout                   */
/* ------------------------------------------------------------------ */

static obsw_tm_store_t tm_store;

static void drain_tm(void)
{
    uint8_t pkt[OBSW_TM_MAX_PACKET_LEN];
    uint16_t len = 0;
    while (obsw_tm_store_dequeue(&tm_store, pkt, sizeof(pkt), &len) == OBSW_TM_OK) {
        write_tm_packet(pkt, len);
    }
}

/* ------------------------------------------------------------------ */
/* No-op responder — TM is drained explicitly after each dispatch     */
/* ------------------------------------------------------------------ */

static void noop_responder(uint8_t f, const obsw_tc_t *t, void *c)
{
    (void)f;
    (void)t;
    (void)c;
}

/* ------------------------------------------------------------------ */
/* Service contexts                                                    */
/* ------------------------------------------------------------------ */

static obsw_s1_ctx_t s1_ctx   = {0};
static obsw_s3_ctx_t s3_ctx   = {0};
static obsw_s5_ctx_t s5_ctx   = {0};
static obsw_s17_ctx_t s17_ctx = {0};
static obsw_s8_ctx_t s8_ctx   = {0};
static obsw_fsm_ctx_t fsm_ctx = {0};
static obsw_wd_ctx_t wd_ctx   = {0};

/* ------------------------------------------------------------------ */
/* Housekeeping parameters                                             */
/* ------------------------------------------------------------------ */

static uint16_t param_uptime_s         = 0;
static uint16_t param_safe_entry_count = 0;
static uint32_t param_wd_kick_count    = 0;

static obsw_s3_param_t nominal_hk_params[] = {
    {.ptr = &param_uptime_s, .size = OBSW_S3_PARAM_U16},
};
static obsw_s3_param_t fdir_hk_params[] = {
    {.ptr = &param_safe_entry_count, .size = OBSW_S3_PARAM_U16},
    {.ptr = &param_wd_kick_count, .size = OBSW_S3_PARAM_U32},
};
static obsw_s3_set_t hk_sets[] = {
    {.set_id         = SRDB_HK_NOMINAL_HK,
     .params         = nominal_hk_params,
     .param_count    = 1,
     .interval_ticks = 10,
     .countdown      = 10,
     .enabled        = true},
    {.set_id         = SRDB_HK_FDIR_HK,
     .params         = fdir_hk_params,
     .param_count    = 2,
     .interval_ticks = 60,
     .countdown      = 60,
     .enabled        = true},
};

/* ------------------------------------------------------------------ */
/* S8 function table                                                   */
/* ------------------------------------------------------------------ */

static int fn_recover_nominal(const uint8_t *args, uint8_t args_len, void *ctx)
{
    (void)args;
    (void)args_len;
    obsw_fsm_to_nominal((obsw_fsm_ctx_t *)ctx);
    fprintf(stderr, "[OBSW] Recovered to NOMINAL\n");
    return 0;
}

static obsw_s8_entry_t s8_table[] = {
    {.function_id = OBSW_S8_FN_RECOVER_NOMINAL, .fn = fn_recover_nominal, .ctx = &fsm_ctx},
};

/* ------------------------------------------------------------------ */
/* FDIR                                                                */
/* ------------------------------------------------------------------ */

static void on_enter_safe(void *ctx)
{
    (void)ctx;
    param_safe_entry_count++;
    fprintf(stderr, "[OBSW] Entered SAFE mode\n");
}

static void on_watchdog_expiry(void *ctx)
{
    obsw_s5_report((obsw_s5_ctx_t *)ctx, OBSW_S5_HIGH, SRDB_EVENT_WATCHDOG_EXPIRY, NULL, 0);
}

static const obsw_fsm_tc_entry_t safe_whitelist[] = {
    {SRDB_TC_ARE_YOU_ALIVE_SVC, SRDB_TC_ARE_YOU_ALIVE_SUBSVC},
    {SRDB_TC_S8_PERFORM_FUNCTION_SVC, SRDB_TC_S8_PERFORM_FUNCTION_SUBSVC},
};

/* ------------------------------------------------------------------ */
/* Routing table                                                       */
/* ------------------------------------------------------------------ */

static obsw_tc_route_t routes[] = {
    {.apid = 0xFFFF, .service = 17, .subservice = 1, .handler = obsw_s17_ping, .ctx = &s17_ctx},
    {.apid = 0xFFFF, .service = 8, .subservice = 1, .handler = obsw_s8_perform, .ctx = &s8_ctx},
    {.apid = 0xFFFF, .service = 3, .subservice = 5, .handler = obsw_s3_enable, .ctx = &s3_ctx},
    {.apid = 0xFFFF, .service = 3, .subservice = 6, .handler = obsw_s3_disable, .ctx = &s3_ctx},
};

/* ------------------------------------------------------------------ */
/* Main loop                                                           */
/* ------------------------------------------------------------------ */

int main(void)
{
    obsw_tm_store_init(&tm_store);

    obsw_fsm_config_t fsm_cfg = {
        .on_enter_safe     = on_enter_safe,
        .on_exit_safe      = NULL,
        .hook_ctx          = NULL,
        .safe_tc_whitelist = safe_whitelist,
        .whitelist_len     = sizeof(safe_whitelist) / sizeof(safe_whitelist[0]),
    };
    obsw_fsm_init(&fsm_ctx, &fsm_cfg);

    s1_ctx.tm_store = &tm_store;
    s1_ctx.apid     = SRDB_APID_DEFAULT;

    s3_ctx.tm_store  = &tm_store;
    s3_ctx.s1        = &s1_ctx;
    s3_ctx.apid      = SRDB_APID_DEFAULT;
    s3_ctx.sets      = hk_sets;
    s3_ctx.set_count = sizeof(hk_sets) / sizeof(hk_sets[0]);

    s5_ctx.tm_store           = &tm_store;
    s5_ctx.apid               = SRDB_APID_DEFAULT;
    s5_ctx.fsm                = (struct obsw_fsm_ctx *)&fsm_ctx;
    s5_ctx.safe_trigger_count = SRDB_SAFE_TRIGGER_COUNT;
    {
        uint16_t ids[] = SRDB_SAFE_TRIGGER_IDS;
        for (uint8_t i = 0; i < SRDB_SAFE_TRIGGER_COUNT; i++)
            s5_ctx.safe_trigger_ids[i] = ids[i];
    }

    s17_ctx.tm_store = &tm_store;
    s17_ctx.s1       = &s1_ctx;
    s17_ctx.apid     = SRDB_APID_DEFAULT;

    s8_ctx.tm_store  = &tm_store;
    s8_ctx.s1        = &s1_ctx;
    s8_ctx.apid      = SRDB_APID_DEFAULT;
    s8_ctx.table     = s8_table;
    s8_ctx.table_len = 1;

    obsw_wd_init(&wd_ctx, 100, on_watchdog_expiry, &s5_ctx);

    obsw_tc_dispatcher_t dispatcher;
    obsw_tc_dispatcher_init(
        &dispatcher, routes, sizeof(routes) / sizeof(routes[0]), noop_responder, NULL);

    /* Boot event */
    obsw_s5_report(&s5_ctx, OBSW_S5_INFO, SRDB_EVENT_BOOT_COMPLETE, NULL, 0);
    drain_tm();

    fprintf(stderr, "[OBSW] Host sim started (SCID=0x%03X)\n", SRDB_SCID);

    uint8_t frame[OBSW_TM_MAX_PACKET_LEN];
    uint32_t tick = 0;

    while (1) {
        /* Read TC frame (length-prefixed, non-blocking via select) */
        uint8_t len_buf[2];
        if (fread(len_buf, 1, 2, stdin) != 2)
            break;

        uint16_t frame_len = (uint16_t)((len_buf[0] << 8) | len_buf[1]);
        if (frame_len == 0 || frame_len > sizeof(frame))
            break;
        if (fread(frame, 1, frame_len, stdin) != frame_len)
            break;

        /* FSM gate */
        obsw_sp_packet_t pkt;
        if (obsw_sp_parse(frame, frame_len, &pkt) == OBSW_SP_OK) {
            if (obsw_fsm_tc_allowed(&fsm_ctx, pkt.payload[1], pkt.payload[2])) {
                obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);
            }
        }

        drain_tm();
        write_sync_byte(); /* 0xFF — one control cycle complete */

        /* Periodic housekeeping every 10 TCs */
        tick++;
        if (tick % 10U == 0U) {
            param_uptime_s++;
            obsw_s3_tick(&s3_ctx);
            obsw_wd_kick(&wd_ctx);
            obsw_wd_tick(&wd_ctx);
            param_wd_kick_count++;
            drain_tm();
        }
    }

    fprintf(stderr, "[OBSW] Sim terminated.\n");
    return 0;
}