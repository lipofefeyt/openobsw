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
#include "obsw/pus/s20.h"

#ifdef OBSW_ENABLE_ORBITFABRIC_CONTRACT
#include "orbitfabric_contract_adapter.h"
#endif

#include <math.h>
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
static obsw_s8_ctx_t  s8_ctx  = {0};
static obsw_s17_ctx_t s17_ctx = {0};
static obsw_s20_ctx_t  s20_ctx = {0};
static obsw_fsm_ctx_t fsm_ctx = {0};
static obsw_wd_ctx_t  wd_ctx  = {0};

static uint16_t param_uptime_s         = 0;
static uint16_t param_safe_entry_count = 0;
static uint32_t param_wd_kick_count    = 0;

/* DHS OBC HK — reported in TM(3,25) set_id=3, consumed by opensvf */
static uint8_t  param_obc_mode        = 0;   /* 0=SAFE, 1=NOMINAL            */
static uint32_t param_obc_obt         = 0;   /* on-board time [s]            */
static uint8_t  param_obc_wd_status   = 0;   /* 0=nominal, 1=expired         */
static uint8_t  param_obc_mem_pct     = 0;   /* mass-memory fill % (stub)    */
static uint8_t  param_obc_health      = 0;   /* 0=nominal (stub)             */
static uint16_t param_obc_reset_count = 0;   /* reset counter (stub = 0)     */
static uint8_t  param_obc_cpu_load    = 0;   /* CPU utilisation % (stub)     */

static obsw_s3_param_t nominal_hk_params[] = {
    {.ptr = &param_uptime_s, .size = OBSW_S3_PARAM_U16},
};
static obsw_s3_param_t fdir_hk_params[] = {
    {.ptr = &param_safe_entry_count, .size = OBSW_S3_PARAM_U16},
    {.ptr = &param_wd_kick_count,    .size = OBSW_S3_PARAM_U32},
};
static obsw_s3_param_t dhs_obc_hk_params[] = {
    {.ptr = &param_obc_mode,        .size = OBSW_S3_PARAM_U8},
    {.ptr = &param_obc_obt,         .size = OBSW_S3_PARAM_U32},
    {.ptr = &param_obc_wd_status,   .size = OBSW_S3_PARAM_U8},
    {.ptr = &param_obc_mem_pct,     .size = OBSW_S3_PARAM_U8},
    {.ptr = &param_obc_health,      .size = OBSW_S3_PARAM_U8},
    {.ptr = &param_obc_reset_count, .size = OBSW_S3_PARAM_U16},
    {.ptr = &param_obc_cpu_load,    .size = OBSW_S3_PARAM_U8},
};
/* AOCS HK (set_id=4): gains as big-endian IEEE 754 floats for YAMCS */
static uint8_t hk_bdot_gain_be[4] = {0};
static uint8_t hk_adcs_kp_be[4]   = {0};
static uint8_t hk_adcs_kd_be[4]   = {0};
static obsw_s3_param_t aocs_hk_params[] = {
    {.ptr = hk_bdot_gain_be, .size = OBSW_S3_PARAM_U32},
    {.ptr = hk_adcs_kp_be,   .size = OBSW_S3_PARAM_U32},
    {.ptr = hk_adcs_kd_be,   .size = OBSW_S3_PARAM_U32},
};

/* AOCS state HK (set_id=6): live attitude state as big-endian IEEE 754 */
static uint8_t  hk_aocs_err_be[4]   = {0};  /* aocs_angle_err_rad */
static uint8_t  hk_aocs_omega_be[4] = {0};  /* aocs_omega_mag     */
static uint8_t  hk_aocs_ctrl        = 0;    /* aocs_controller: 0=off,1=bdot,2=PD */
static obsw_s3_param_t aocs_state_hk_params[] = {
    {.ptr = hk_aocs_err_be,   .size = OBSW_S3_PARAM_U32},
    {.ptr = hk_aocs_omega_be, .size = OBSW_S3_PARAM_U32},
    {.ptr = &hk_aocs_ctrl,    .size = OBSW_S3_PARAM_U8},
};

static obsw_s3_set_t hk_sets[] = {
    {.set_id = SRDB_HK_NOMINAL_HK, .params = nominal_hk_params,
     .param_count = 1, .interval_ticks = 10, .countdown = 10, .enabled = true},
    {.set_id = SRDB_HK_FDIR_HK, .params = fdir_hk_params,
     .param_count = 2, .interval_ticks = 60, .countdown = 60, .enabled = true},
    {.set_id = SRDB_HK_DHS_OBC_HK, .params = dhs_obc_hk_params,
     .param_count = 7, .interval_ticks = 10, .countdown = 10, .enabled = true},
    {.set_id = SRDB_HK_AOCS_HK, .params = aocs_hk_params,
     .param_count = 3, .interval_ticks = 5,  .countdown = 5,  .enabled = true},
    {.set_id = SRDB_HK_AOCS_STATE_HK, .params = aocs_state_hk_params,
     .param_count = 3, .interval_ticks = 5,  .countdown = 5,  .enabled = true},
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

static int fn_request_safe(const uint8_t *args, uint8_t args_len, void *ctx)
{
    (void)args; (void)args_len;
    obsw_fsm_to_safe((obsw_fsm_ctx_t *)ctx);
    fprintf(stderr, "[OBSW] Ground-commanded to SAFE\n");
    return 0;
}

static int fn_request_standby(const uint8_t *args, uint8_t args_len, void *ctx)
{
    (void)args; (void)args_len;
    obsw_fsm_to_standby((obsw_fsm_ctx_t *)ctx);
    fprintf(stderr, "[OBSW] Ground-commanded to STANDBY\n");
    return 0;
}

#ifdef OBSW_ENABLE_ORBITFABRIC_CONTRACT
#define OBSW_OF_S8_FN_REPORT_VOLTAGE_OUT_OF_BOUNDS 0x5001U

static int fn_of_report_voltage_out_of_bounds(const uint8_t *args,
                                              uint8_t args_len,
                                              void *ctx)
{
    (void)args;

    if (args_len != 0U)
        return -1;

    int rc = obsw_s5_report((obsw_s5_ctx_t *)ctx,
                            OBSW_S5_MEDIUM,
                            OF_EVENT_VOLTAGE_OUT_OF_BOUNDS,
                            NULL,
                            0U);
    if (rc == OBSW_PUS_TM_OK) {
        fprintf(stderr,
                "[OBSW] OrbitFabric: OF_EVENT_VOLTAGE_OUT_OF_BOUNDS -> TM(5,3)\n");
        return 0;
    }

    fprintf(stderr,
            "[OBSW] OrbitFabric: OF_EVENT_VOLTAGE_OUT_OF_BOUNDS report failed: %d\n",
            rc);
    return -1;
}
#endif

static obsw_s8_entry_t s8_table[] = {
    {.function_id = OBSW_S8_FN_RECOVER_NOMINAL, .fn = fn_recover_nominal, .ctx = &fsm_ctx},
    {.function_id = OBSW_S8_FN_REQUEST_SAFE,    .fn = fn_request_safe,    .ctx = &fsm_ctx},
    {.function_id = OBSW_S8_FN_REQUEST_STANDBY, .fn = fn_request_standby, .ctx = &fsm_ctx},
#ifdef OBSW_ENABLE_ORBITFABRIC_CONTRACT
    {.function_id = OBSW_OF_S8_FN_REPORT_VOLTAGE_OUT_OF_BOUNDS,
     .fn = fn_of_report_voltage_out_of_bounds, .ctx = &s5_ctx},
#endif
};

/* ------------------------------------------------------------------------ */
/* Static parameter table for S20. IDs match SRDB parameters.yaml.          */
/* Values are initialised to zero — the ground can set them via TC(20,1).   */
/* ------------------------------------------------------------------------ */

static obsw_s20_param_t s20_params[] = {
    {.param_id = SRDB_PARAM_OBC_TEMPERATURE,         .value = {.u32 = 0}},
    {.param_id = SRDB_PARAM_OBC_VOLTAGE_3V3,         .value = {.u32 = 3300}},
    {.param_id = SRDB_PARAM_OBC_VOLTAGE_5V,          .value = {.u32 = 5000}},
    {.param_id = SRDB_PARAM_OBC_UPTIME,              .value = {.u32 = 0}},
    {.param_id = SRDB_PARAM_SAFE_MODE_ENTRY_COUNT,   .value = {.u32 = 0}},
    {.param_id = SRDB_PARAM_WATCHDOG_KICK_COUNT,     .value = {.u32 = 0}},
    {.param_id = SRDB_PARAM_WATCHDOG_TICKS_REMAINING,.value = {.u32 = 30}},
    {.param_id = SRDB_PARAM_BDOT_GAIN,               .value = {.f32 = 1.0e4f}},
    {.param_id = SRDB_PARAM_ADCS_KP,                 .value = {.f32 = 0.5f}},
    {.param_id = SRDB_PARAM_ADCS_KD,                 .value = {.f32 = 0.1f}},
    /* Orbit and dynamics configuration (#68) — readable via TC(20,3) */
    {.param_id = SRDB_PARAM_ORBIT_ALTITUDE_KM,        .value = {.f32 = 550.0f}},
    {.param_id = SRDB_PARAM_ORBIT_INCLINATION_DEG,    .value = {.f32 = 97.4f}},
    {.param_id = SRDB_PARAM_SC_INERTIA_XX,            .value = {.f32 = 2.0e-3f}},
    {.param_id = SRDB_PARAM_SC_INERTIA_YY,            .value = {.f32 = 2.5e-3f}},
    {.param_id = SRDB_PARAM_SC_INERTIA_ZZ,            .value = {.f32 = 1.5e-3f}},
    {.param_id = SRDB_PARAM_MTQ_MAX_DIPOLE,           .value = {.f32 = 10.0f}},
    {.param_id = SRDB_PARAM_SC_OMEGA_X0,              .value = {.f32 = 0.5f}},
    {.param_id = SRDB_PARAM_SC_OMEGA_Y0,              .value = {.f32 = 0.5f}},
    {.param_id = SRDB_PARAM_SC_OMEGA_Z0,              .value = {.f32 = 0.5f}},
};

static float s20_get_f32(uint16_t param_id, float default_val)
{
    for (size_t i = 0; i < sizeof(s20_params) / sizeof(s20_params[0]); i++) {
        if (s20_params[i].param_id == param_id)
            return s20_params[i].value.f32;
    }
    return default_val;
}

/* ------------------------------------------------------------------ */
/* Nadir-pointing target                                               */
/* ------------------------------------------------------------------ */

/* Compute body-to-ECI quaternion for nadir-pointing circular orbit.
 * Frame: body +z = zenith (anti-nadir), body -z = nadir (Earth-facing),
 *        body +x = along-track (velocity), body +y = orbit normal.
 * Uses Shepperd's method to convert the rotation matrix to quaternion. */
static void compute_nadir_quat(float t_s,
                                float alt_km, float inc_deg,
                                obsw_quat_t *q_out)
{
    static const float MU_EARTH  = 3.986004418e14f;  /* m³/s² */
    static const float R_EARTH   = 6.371e6f;          /* m     */
    static const float DEG2RAD   = 3.14159265f / 180.0f;

    float r_orbit   = R_EARTH + alt_km * 1.0e3f;
    float omega_orb = sqrtf(MU_EARTH / (r_orbit * r_orbit * r_orbit));
    float nu        = omega_orb * t_s;
    float inc       = inc_deg * DEG2RAD;

    /* Unit radial r̂ and unit velocity v̂ for circular orbit in ECI */
    float r[3] = { cosf(nu),
                   sinf(nu) * cosf(inc),
                   sinf(nu) * sinf(inc) };
    float v[3] = { -sinf(nu),
                    cosf(nu) * cosf(inc),
                    cosf(nu) * sinf(inc) };

    /* Orbit-normal axis: h = r̂ × v̂  (body +y direction in ECI) */
    float h[3] = { r[1]*v[2] - r[2]*v[1],
                   r[2]*v[0] - r[0]*v[2],
                   r[0]*v[1] - r[1]*v[0] };
    float hn = sqrtf(h[0]*h[0] + h[1]*h[1] + h[2]*h[2]);
    if (hn > 1e-10f) { h[0] /= hn; h[1] /= hn; h[2] /= hn; }

    /* Rotation matrix R (body→ECI): columns are body axes expressed in ECI
     * col 0 = body +x = v̂,  col 1 = body +y = h,  col 2 = body +z = r̂ */
    float R[3][3] = {
        { v[0], h[0], r[0] },
        { v[1], h[1], r[1] },
        { v[2], h[2], r[2] },
    };

    /* Shepperd's method: R → unit quaternion [w, x, y, z] */
    float tr = R[0][0] + R[1][1] + R[2][2];
    float s;
    if (tr > 0.0f) {
        s = 0.5f / sqrtf(tr + 1.0f);
        q_out->w = 0.25f / s;
        q_out->x = (R[2][1] - R[1][2]) * s;
        q_out->y = (R[0][2] - R[2][0]) * s;
        q_out->z = (R[1][0] - R[0][1]) * s;
    } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
        s = 2.0f * sqrtf(1.0f + R[0][0] - R[1][1] - R[2][2]);
        q_out->w = (R[2][1] - R[1][2]) / s;
        q_out->x = 0.25f * s;
        q_out->y = (R[0][1] + R[1][0]) / s;
        q_out->z = (R[0][2] + R[2][0]) / s;
    } else if (R[1][1] > R[2][2]) {
        s = 2.0f * sqrtf(1.0f + R[1][1] - R[0][0] - R[2][2]);
        q_out->w = (R[0][2] - R[2][0]) / s;
        q_out->x = (R[0][1] + R[1][0]) / s;
        q_out->y = 0.25f * s;
        q_out->z = (R[2][1] + R[1][2]) / s;
    } else {
        s = 2.0f * sqrtf(1.0f + R[2][2] - R[0][0] - R[1][1]);
        q_out->w = (R[1][0] - R[0][1]) / s;
        q_out->x = (R[0][2] + R[2][0]) / s;
        q_out->y = (R[2][1] + R[1][2]) / s;
        q_out->z = 0.25f * s;
    }
    obsw_quat_normalise(q_out);
}

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
    {.apid = 0xFFFF, .service = 20, .subservice = 1,
     .handler = obsw_s20_set, .ctx = &s20_ctx},
    {.apid = 0xFFFF, .service = 20, .subservice = 3,
     .handler = obsw_s20_get, .ctx = &s20_ctx},
};

#ifdef OBSW_ENABLE_ORBITFABRIC_CONTRACT
static void configure_orbitfabric_contract_routes(void)
{
    obsw_of_tc_route_t of_route = {0};

    if (obsw_of_tc_route_for_command(OF_CMD_PING, &of_route) != 0) {
        fprintf(stderr, "[OBSW] OrbitFabric: OF_CMD_PING not mapped\n");
        return;
    }

    size_t n = sizeof(routes) / sizeof(routes[0]);
    for (size_t i = 0; i < n; i++) {
        if (routes[i].service == 17U && routes[i].subservice == 1U) {
            routes[i].apid       = of_route.apid;
            routes[i].service    = of_route.service;
            routes[i].subservice = of_route.subservice;
            fprintf(stderr, "[OBSW] OrbitFabric: OF_CMD_PING -> TC(%u,%u)\n",
                    (unsigned)of_route.service, (unsigned)of_route.subservice);
            return;
        }
    }

    fprintf(stderr, "[OBSW] OrbitFabric: TC(17,1) route not found in table\n");
}
#else
static void configure_orbitfabric_contract_routes(void) {}
#endif

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
    obsw_fsm_to_safe(&fsm_ctx);   /* sim skips STANDBY auto-timeout; start in SAFE */

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

    s20_ctx.tm_store    = &tm_store;
    s20_ctx.s1          = &s1_ctx;
    s20_ctx.apid        = SRDB_APID_DEFAULT;
    s20_ctx.table       = s20_params;
    s20_ctx.table_len   = sizeof(s20_params) / sizeof(s20_params[0]);

    /* Watchdog */
    obsw_wd_init(&wd_ctx, 30, on_watchdog_expiry, &s5_ctx);

    /* Dispatcher */
    configure_orbitfabric_contract_routes();
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
        .max_torque = 0.2f,   /* 0.2 Nm matches Kp=0.5 linear regime up to ~47° error */
    };
    obsw_adcs_init(&adcs_ctx, &adcs_cfg);

    float last_sim_time = 0.0f;

    fprintf(stderr, "[OBSW] Host sim started (type-frame protocol v2).\n");
    fprintf(stderr, "[OBSW] SRDB version: %s\n", SRDB_VERSION);

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

            /* Drain TM generated by this TC */
            {
                uint8_t pkt[OBSW_TM_MAX_PACKET_LEN];
                uint16_t plen = 0;
                while (obsw_tm_store_dequeue(&tm_store, pkt, sizeof(pkt), &plen) == OBSW_TM_OK)
                    write_tm_packet(pkt, plen);
            }

        } else if (type == OBSW_FRAME_SENSOR) {
            obsw_sensor_frame_t sensor;
            if (obsw_sim_parse_sensor(frame, frame_len, &sensor)) {
                float dt = sensor.sim_time - last_sim_time;
                if (dt <= 0.0f) dt = 0.1f;
                last_sim_time = sensor.sim_time;

                obsw_actuator_frame_t act;
                memset(&act, 0, sizeof(act));
                act.sim_time   = sensor.sim_time;
                act.controller = 0;

                /* Three-state mode gate: STANDBY=off, SAFE=bdot, NOMINAL=PD ADCS */
                obsw_fsm_mode_t cur_mode = obsw_fsm_mode(&fsm_ctx);

                /* Sync S20-tunable AOCS gains before each control step */
                adcs_ctx.config.kp          = s20_get_f32(SRDB_PARAM_ADCS_KP,        0.5f);
                adcs_ctx.config.kd          = s20_get_f32(SRDB_PARAM_ADCS_KD,        0.1f);
                bdot_ctx.config.gain        = s20_get_f32(SRDB_PARAM_BDOT_GAIN,      1.0e4f);
                bdot_ctx.config.max_dipole  = s20_get_f32(SRDB_PARAM_MTQ_MAX_DIPOLE, 10.0f);

                if (cur_mode == OBSW_FSM_NOMINAL && sensor.st_valid && sensor.gyro_valid) {
                    /* Update nadir target from orbital parameters (runtime-tunable via S20) */
                    {
                        float alt_km  = s20_get_f32(SRDB_PARAM_ORBIT_ALTITUDE_KM,     550.0f);
                        float inc_deg = s20_get_f32(SRDB_PARAM_ORBIT_INCLINATION_DEG,  97.4f);
                        obsw_quat_t q_nadir;
                        compute_nadir_quat(sensor.sim_time, alt_km, inc_deg, &q_nadir);
                        obsw_adcs_set_target(&adcs_ctx, &q_nadir);
                    }

                    obsw_quat_t q_meas = {
                        sensor.st_q_w, sensor.st_q_x,
                        sensor.st_q_y, sensor.st_q_z
                    };
                    float omega[3] = {
                        sensor.gyro_x, sensor.gyro_y, sensor.gyro_z
                    };
                    obsw_adcs_output_t adcs_out;
                    if (obsw_adcs_step(&adcs_ctx, &q_meas, omega, &adcs_out)) {
                        act.rw_torque_x = adcs_out.torque_cmd[0];
                        act.rw_torque_y = adcs_out.torque_cmd[1];
                        act.rw_torque_z = adcs_out.torque_cmd[2];
                        act.controller  = 1;
                        fprintf(stderr,
                            "[OBSW] adcs err=%.1f° tau=[%.3e,%.3e,%.3e] Nm\n",
                            adcs_out.angle_err_rad * (180.0f / 3.14159265f),
                            act.rw_torque_x, act.rw_torque_y, act.rw_torque_z);

                        /* Update AOCS state HK (set_id=6) */
                        {
                            float om = sqrtf(omega[0]*omega[0] + omega[1]*omega[1] + omega[2]*omega[2]);
                            uint32_t err_u32, om_u32;
                            memcpy(&err_u32, &adcs_out.angle_err_rad, 4);
                            memcpy(&om_u32,  &om, 4);
                            for (int _i = 0; _i < 4; _i++) {
                                hk_aocs_err_be[_i]   = (uint8_t)((err_u32 >> (24 - 8*_i)) & 0xFFU);
                                hk_aocs_omega_be[_i] = (uint8_t)((om_u32  >> (24 - 8*_i)) & 0xFFU);
                            }
                            hk_aocs_ctrl = 2U; /* PD */
                        }
                    }
                } else if (cur_mode == OBSW_FSM_SAFE && sensor.mag_valid) {
                    float b[3] = {sensor.mag_x, sensor.mag_y, sensor.mag_z};
                    obsw_bdot_output_t bdot_out;
                    obsw_bdot_step(&bdot_ctx, b, dt, &bdot_out);
                    act.mtq_dipole_x = bdot_out.m_cmd[0];
                    act.mtq_dipole_y = bdot_out.m_cmd[1];
                    act.mtq_dipole_z = bdot_out.m_cmd[2];
                    act.controller   = 0;
                    hk_aocs_ctrl     = 1U; /* B-dot */
                    fprintf(stderr,
                        "[OBSW] bdot m=[%.3e,%.3e,%.3e] Am2\n",
                        act.mtq_dipole_x, act.mtq_dipole_y, act.mtq_dipole_z);
                } else {
                    hk_aocs_ctrl = 0U; /* STANDBY or sensors invalid */
                }

                /* Drain any TM generated during sensor tick */
                {
                    uint8_t pkt[OBSW_TM_MAX_PACKET_LEN];
                    uint16_t plen = 0;
                    while (obsw_tm_store_dequeue(&tm_store, pkt, sizeof(pkt), &plen) == OBSW_TM_OK)
                        write_tm_packet(pkt, plen);
                }
                write_actuator_frame(&act);

                param_uptime_s = (uint16_t)sensor.sim_time;

                s20_params[3].value.u32 = (uint32_t)sensor.sim_time;         /* obc_uptime */
                s20_params[4].value.u32 = (uint32_t)param_safe_entry_count;  /* safe_mode_entry_count */
                s20_params[5].value.u32 = param_wd_kick_count;               /* watchdog_kick_count */

                /* Sync AOCS HK gains as big-endian IEEE 754 for TM(3,25) set_id=4 */
                {
                    uint32_t params[3];
                    memcpy(&params[0], &(float){s20_get_f32(SRDB_PARAM_BDOT_GAIN, 1.0e4f)}, 4);
                    memcpy(&params[1], &(float){s20_get_f32(SRDB_PARAM_ADCS_KP,   0.5f)},   4);
                    memcpy(&params[2], &(float){s20_get_f32(SRDB_PARAM_ADCS_KD,   0.1f)},   4);
                    for (int _i = 0; _i < 4; _i++) {
                        hk_bdot_gain_be[_i] = (uint8_t)((params[0] >> (24 - 8*_i)) & 0xFFU);
                        hk_adcs_kp_be[_i]   = (uint8_t)((params[1] >> (24 - 8*_i)) & 0xFFU);
                        hk_adcs_kd_be[_i]   = (uint8_t)((params[2] >> (24 - 8*_i)) & 0xFFU);
                    }
                }

                /* DHS OBC HK — live state for TM(3,25) set_id=3 */
                param_obc_mode = (cur_mode == OBSW_FSM_STANDBY) ? 0U :
                                 (cur_mode == OBSW_FSM_SAFE)    ? 1U : 2U;
                param_obc_obt       = (uint32_t)sensor.sim_time;
                param_obc_wd_status = 0U;   /* nominal — watchdog kicked each tick */

                obsw_wd_kick(&wd_ctx);
                obsw_s3_tick(&s3_ctx);
            }
        }
        write_sync_byte();
    }

    fprintf(stderr, "[OBSW] Sim terminated.\n");
    return 0;
}