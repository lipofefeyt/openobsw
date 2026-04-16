/**
 * @file src/platform/zynqmp/main.c
 * @brief ZynqMP PS bare-metal entry point.
 *
 * Runs the openobsw PUS stack on ZynqMP Cortex-A53 (APU).
 * Uses Cadence UART HAL for I/O (uart0 @ 0xFF000000).
 * Wire protocol v3: type-prefixed frames (same as obsw_sim).
 */

#include "obsw/hal/io.h"
#include "obsw/obsw.h"
#include "obsw/aocs/bdot.h"
#include "obsw/aocs/adcs.h"
#include "obsw/srdb_generated.h"
#include "obsw/fdir/fsm.h"

#include <stdint.h>
#include <string.h>

extern obsw_io_ops_t obsw_uart_ops;
extern void obsw_uart_init(void);

#define OBSW_FRAME_TC       0x01U
#define OBSW_FRAME_SENSOR   0x02U
#define OBSW_FRAME_ACTUATOR 0x03U
#define OBSW_FRAME_TM       0x04U
#define OBSW_SYNC_BYTE      0xFFU

/* ------------------------------------------------------------------ */
/* UART helpers                                                        */
/* ------------------------------------------------------------------ */

static void uart_putc(uint8_t c)
{
    obsw_uart_ops.write(&c, 1, obsw_uart_ops.ctx);
}

static void uart_getc_blocking(uint8_t *c)
{
    while (obsw_uart_ops.read(c, 1, obsw_uart_ops.ctx) == 0)
        ;
}

static void uart_write_u16_be(uint16_t v)
{
    uart_putc((uint8_t)(v >> 8));
    uart_putc((uint8_t)(v & 0xFF));
}

static void write_frame(uint8_t type, const uint8_t *data, uint16_t len)
{
    uart_putc(type);
    uart_write_u16_be(len);
    obsw_uart_ops.write(data, len, obsw_uart_ops.ctx);
}

static void write_sync(void)
{
    uart_putc(OBSW_SYNC_BYTE);
}

static uint8_t read_frame(uint8_t *buf, uint16_t bufsize, uint16_t *out_len)
{
    uint8_t type, hi, lo;
    uart_getc_blocking(&type);
    uart_getc_blocking(&hi);
    uart_getc_blocking(&lo);

    uint16_t len = (uint16_t)(((uint16_t)hi << 8) | lo);
    if (len > bufsize) return 0;

    for (uint16_t i = 0; i < len; i++)
        uart_getc_blocking(&buf[i]);

    *out_len = len;
    return type;
}

/* ------------------------------------------------------------------ */
/* No-op responder                                                     */
/* ------------------------------------------------------------------ */

static void noop_responder(uint8_t flag, const obsw_tc_t *tc, void *ctx)
{
    (void)flag; (void)tc; (void)ctx;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    obsw_uart_init();

    const char *banner =
        "[OBSW] ZynqMP started (type-frame protocol v2).\r\n";
    obsw_uart_ops.write((const uint8_t *)banner,
                        (uint16_t)strlen(banner), obsw_uart_ops.ctx);

    const char *version = "[OBSW] SRDB version: " SRDB_VERSION "\r\n";
    obsw_uart_ops.write((const uint8_t *)version,
                        (uint16_t)strlen(version), obsw_uart_ops.ctx);

    /* TM store */
    obsw_tm_store_t tm_store;
    obsw_tm_store_init(&tm_store);

    /* FSM */
    obsw_fsm_ctx_t fsm_ctx = {0};
    obsw_fsm_config_t fsm_cfg = {
        .on_enter_safe     = NULL,
        .on_exit_safe      = NULL,
        .hook_ctx          = NULL,
        .safe_tc_whitelist = NULL,
        .whitelist_len     = 0,
    };
    obsw_fsm_init(&fsm_ctx, &fsm_cfg);

    /* AOCS */
    obsw_bdot_ctx_t bdot_ctx;
    obsw_bdot_config_t bdot_cfg = { .gain = 1.0e4f, .max_dipole = 10.0f };
    obsw_bdot_init(&bdot_ctx, &bdot_cfg);

    obsw_adcs_ctx_t adcs_ctx;
    obsw_adcs_config_t adcs_cfg = {
        .kp = 0.5f, .kd = 0.1f, .max_torque = 0.01f
    };
    obsw_adcs_init(&adcs_ctx, &adcs_cfg);

    /* PUS contexts */
    obsw_s1_ctx_t s1_ctx = {0};
    s1_ctx.tm_store = &tm_store;
    s1_ctx.apid     = SRDB_APID_DEFAULT;

    obsw_s17_ctx_t s17_ctx = {0};
    s17_ctx.tm_store = &tm_store;
    s17_ctx.s1       = &s1_ctx;
    s17_ctx.apid     = SRDB_APID_DEFAULT;

    /* Dispatcher */
    obsw_tc_route_t routes[] = {
        { .apid = 0xFFFF, .service = 17, .subservice = 1,
          .handler = obsw_s17_ping, .ctx = &s17_ctx },
    };
    obsw_tc_dispatcher_t dispatcher;
    obsw_tc_dispatcher_init(&dispatcher, routes, 1,
                            noop_responder, NULL);

    float last_sim_time = 0.0f;
    uint8_t frame[256];
    uint16_t frame_len;

    while (1) {
        uint8_t type = read_frame(frame, sizeof(frame), &frame_len);
        if (type == 0) continue;

        if (type == OBSW_FRAME_TC) {
            obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);

        } else if (type == OBSW_FRAME_SENSOR) {
            float mag_x, mag_y, mag_z;
            uint8_t mag_valid;
            float st_qw, st_qx, st_qy, st_qz;
            uint8_t st_valid;
            float gx, gy, gz;
            uint8_t gyro_valid;
            float sim_time;

            uint8_t *p = frame;
            memcpy(&mag_x,    p, 4); p += 4;
            memcpy(&mag_y,    p, 4); p += 4;
            memcpy(&mag_z,    p, 4); p += 4;
            mag_valid = *p++;
            memcpy(&st_qw,   p, 4); p += 4;
            memcpy(&st_qx,   p, 4); p += 4;
            memcpy(&st_qy,   p, 4); p += 4;
            memcpy(&st_qz,   p, 4); p += 4;
            st_valid = *p++;
            memcpy(&gx,      p, 4); p += 4;
            memcpy(&gy,      p, 4); p += 4;
            memcpy(&gz,      p, 4); p += 4;
            gyro_valid = *p++;
            memcpy(&sim_time, p, 4);

            float dt = sim_time - last_sim_time;
            if (dt <= 0.0f) dt = 0.1f;
            last_sim_time = sim_time;

            float act[8] = {0};
            act[7] = sim_time;

            bool nominal = !obsw_fsm_is_safe(&fsm_ctx);
            if (nominal && st_valid && gyro_valid) {
                obsw_quat_t q = { st_qw, st_qx, st_qy, st_qz };
                float omega[3] = { gx, gy, gz };
                obsw_adcs_output_t out;
                if (obsw_adcs_step(&adcs_ctx, &q, omega, &out)) {
                    act[3] = out.torque_cmd[0];
                    act[4] = out.torque_cmd[1];
                    act[5] = out.torque_cmd[2];
                    act[6] = 1.0f;
                }
            } else if (mag_valid) {
                float b[3] = { mag_x, mag_y, mag_z };
                obsw_bdot_output_t out;
                obsw_bdot_step(&bdot_ctx, b, dt, &out);
                act[0] = out.m_cmd[0];
                act[1] = out.m_cmd[1];
                act[2] = out.m_cmd[2];
                act[6] = 0.0f;
            }

            uint8_t act_buf[29];
            uint8_t *q = act_buf;
            for (int i = 0; i < 6; i++) {
                memcpy(q, &act[i], 4); q += 4;
            }
            *q++ = (uint8_t)act[6];
            memcpy(q, &act[7], 4);
            write_frame(OBSW_FRAME_ACTUATOR, act_buf, 29);
        }

        uint8_t pkt[128];
        uint16_t plen = 0;
        while (obsw_tm_store_dequeue(&tm_store, pkt,
                                     sizeof(pkt), &plen) == OBSW_TM_OK) {
            write_frame(OBSW_FRAME_TM, pkt, plen);
        }
        write_sync();
    }
}
