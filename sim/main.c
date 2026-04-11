/**
 * @file sim/main.c  
 * @brief Host simulation harness — type-frame protocol.
 *
 * Type 0x01: TC uplink   [0x01][uint16 BE len][TC bytes]
 * Type 0x02: Sensor data [0x02][uint16 BE len][obsw_sensor_frame_t]
 */

#include "obsw/obsw.h"
#include "obsw/aocs/bdot.h"
#include "obsw/aocs/adcs.h"
#include "obsw/srdb_generated.h"
#include "sensor_inject.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* stdout helpers                                                       */
/* ------------------------------------------------------------------ */

static void write_tm_packet(const uint8_t *pkt, uint16_t len)
{
    uint8_t hdr[3] = {
        OBSW_FRAME_TM,
        (uint8_t)(len >> 8),
        (uint8_t)(len & 0xFFU)
    };
    fwrite(hdr, 1, 3, stdout);
    fwrite(pkt, 1, len, stdout);
    fflush(stdout);
}

static void write_sync_byte(void)
{
    uint8_t sync = 0xFFU;
    fwrite(&sync, 1, 1, stdout);
    fflush(stdout);
}


static void write_actuator_frame(const obsw_actuator_frame_t *act)
{
    uint16_t len = OBSW_ACTUATOR_FRAME_LEN;
    uint8_t  hdr[3] = {
        OBSW_FRAME_ACTUATOR,
        (uint8_t)(len >> 8),
        (uint8_t)(len & 0xFFU)
    };
    fwrite(hdr, 1, 3, stdout);
    fwrite(act, 1, len, stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* TM drain                                                            */
/* ------------------------------------------------------------------ */

static obsw_tm_store_t tm_store;

static void drain_tm(void)
{
    uint8_t pkt[OBSW_TM_MAX_PACKET_LEN];
    uint16_t len = 0;
    while (obsw_tm_store_dequeue(&tm_store, pkt, sizeof(pkt), &len) == OBSW_TM_OK)
        write_tm_packet(pkt, len);
}

static void sim_responder(uint8_t f, const obsw_tc_t *t, void *c)
{
    (void)f; (void)t; (void)c;
}

/* ------------------------------------------------------------------ */
/* Service contexts                                                    */
/* ------------------------------------------------------------------ */

static obsw_s1_ctx_t  s1_ctx  = {0};
static obsw_s3_ctx_t  s3_ctx  = {0};
static obsw_s5_ctx_t  s5_ctx  = {0};
static obsw_s17_ctx_t s17_ctx = {0};
static obsw_s8_ctx_t  s8_ctx  = {0};
static obsw_fsm_ctx_t fsm_ctx = {0};
static obsw_wd_ctx_t  wd_ctx  = {0};

static uint16_t param_uptime_s         = 0;
static uint16_t param_safe_entry_count = 0;
static uint32_t param_wd_kick_count    = 0;

static obsw_s3_param_t nominal_hk_params[] = {
    {.ptr = &param_uptime_s, .size = OBSW_S3_PARAM_U16},
};
static obsw_s3_param_t fdir_hk_params[] = {
    {.ptr = &param_safe_entry_count, .size = OBSW_S3_PARAM_U16},
    {.ptr = &param_wd_kick_count,    .size = OBSW_S3_PARAM_U32},
};
static obsw_s3_set_t hk_sets[] = {
    {.set_id = SRDB_HK_NOMINAL_HK, .params = nominal_hk_params,
     .param_count = 1, .interval_ticks = 10, .countdown = 10, .enabled = true},
    {.set_id = SRDB_HK_FDIR_HK, .params = fdir_hk_params,
     .param_count = 2, .interval_ticks = 60, .countdown = 60, .enabled = true},
};

/* ------------------------------------------------------------------ */
/* S8 function table                                                   */
/* ------------------------------------------------------------------ */

static int fn_recover_nominal(const uint8_t *args, uint8_t args_len, void *ctx)
{
    (void)args; (void)args_len;
    obsw_fsm_to_nominal((obsw_fsm_ctx_t *)ctx);
    fprintf(stderr, "[OBSW] Recovered to NOMINAL\n");
    return 0;
}

static obsw_s8_entry_t s8_table[] = {
    {.function_id = OBSW_S8_FN_RECOVER_NOMINAL,
     .fn = fn_recover_nominal, .ctx = &fsm_ctx},
};

/* ------------------------------------------------------------------ */
/* FDIR                                                                */
/* ------------------------------------------------------------------ */

static void on_enter_safe(void *ctx) {
    (void)ctx;
    param_safe_entry_count++;
    fprintf(stderr, "[OBSW] Entered SAFE mode\n");
}

static void on_watchdog_expiry(void *ctx) {
    obsw_s5_report((obsw_s5_ctx_t *)ctx,
                   OBSW_S5_HIGH, SRDB_EVENT_WATCHDOG_EXPIRY, NULL, 0);
}

static const obsw_fsm_tc_entry_t safe_whitelist[] = {
    {SRDB_TC_ARE_YOU_ALIVE_SVC,      SRDB_TC_ARE_YOU_ALIVE_SUBSVC},
    {SRDB_TC_S8_PERFORM_FUNCTION_SVC, SRDB_TC_S8_PERFORM_FUNCTION_SUBSVC},
};

static obsw_tc_route_t routes[] = {
    {.apid = 0xFFFF, .service = 17, .subservice = 1,
     .handler = obsw_s17_ping, .ctx = &s17_ctx},
    {.apid = 0xFFFF, .service = 8,  .subservice = 1,
     .handler = obsw_s8_perform, .ctx = &s8_ctx},
    {.apid = 0xFFFF, .service = 3,  .subservice = 5,
     .handler = obsw_s3_enable, .ctx = &s3_ctx},
    {.apid = 0xFFFF, .service = 3,  .subservice = 6,
     .handler = obsw_s3_disable, .ctx = &s3_ctx},
};

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    obsw_tm_store_init(&tm_store);

    /* FSM */
    obsw_fsm_config_t fsm_cfg = {
        .on_enter_safe     = on_enter_safe,
        .on_exit_safe      = NULL,
        .hook_ctx          = NULL,
        .safe_tc_whitelist = safe_whitelist,
        .whitelist_len     = sizeof(safe_whitelist) / sizeof(safe_whitelist[0]),
    };
    obsw_fsm_init(&fsm_ctx, &fsm_cfg);

    /* PUS service contexts */
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
    s8_ctx.table   = s8_table;
    s8_ctx.table_len = sizeof(s8_table) / sizeof(s8_table[0]);

    /* Watchdog */
obsw_wd_init(&wd_ctx, 30, on_watchdog_expiry, &s5_ctx);

    /* Dispatcher */
    obsw_tc_dispatcher_t dispatcher;
    obsw_tc_dispatcher_init(&dispatcher,
                            routes, sizeof(routes) / sizeof(routes[0]),
                            sim_responder, NULL);

    /* B-dot AOCS */
    obsw_bdot_ctx_t bdot_ctx;
    obsw_bdot_config_t bdot_cfg = {
        .gain       = 1.0e4f,
        .max_dipole = 10.0f,
    };
    obsw_bdot_init(&bdot_ctx, &bdot_cfg);

    obsw_adcs_ctx_t adcs_ctx;
    obsw_adcs_config_t adcs_cfg = {
        .kp         = 0.5f,
        .kd         = 0.1f,
        .max_torque = 0.01f,
    };
    obsw_adcs_init(&adcs_ctx, &adcs_cfg);

    float last_sim_time = 0.0f;

    fprintf(stderr, "[OBSW] Host sim started (type-frame protocol v2).\n");

    uint8_t frame[1024];
    while (1)
    {
        uint16_t frame_len = 0;
        uint8_t  type      = obsw_sim_read_frame(frame, sizeof(frame), &frame_len);

        if (type == 0)
            break;

        if (type == OBSW_FRAME_TC) {
            int rc = obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);
            if (rc == OBSW_TC_ERR_NO_ROUTE)
                fprintf(stderr, "[OBSW] No route for TC\n");
            else if (rc != OBSW_TC_OK)
                fprintf(stderr, "[OBSW] Dispatcher error: %d\n", rc);

        } else if (type == OBSW_FRAME_SENSOR) {
            obsw_sensor_frame_t sensor;
            if (obsw_sim_parse_sensor(frame, frame_len, &sensor)) {
                float dt = sensor.sim_time - last_sim_time;
                if (dt <= 0.0f) dt = 0.1f;
                last_sim_time = sensor.sim_time;

                if (sensor.mag_valid) {
                    float b[3] = {sensor.mag_x, sensor.mag_y, sensor.mag_z};
                    obsw_bdot_output_t out;
                    obsw_bdot_step(&bdot_ctx, b, dt, &out);
                    fprintf(stderr,
                        "[OBSW] bdot m=[%.3e, %.3e, %.3e] Am2\n",
                        out.m_cmd[0], out.m_cmd[1], out.m_cmd[2]);
                }
                param_uptime_s = (uint16_t)sensor.sim_time;
                obsw_wd_kick(&wd_ctx);
                obsw_s3_tick(&s3_ctx);
            }
        }

        drain_tm();
        write_sync_byte();
    }

    fprintf(stderr, "[OBSW] Sim terminated.\n");
    return 0;
}