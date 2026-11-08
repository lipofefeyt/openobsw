#include "obsw/task/aocs.h"
#include "obsw/task/mode.h"
#include "obsw/task/pus.h"
#include "obsw/aocs/bdot.h"
#include "obsw/aocs/adcs.h"
#include "obsw/srdb_generated.h"
#include "obsw/ui/dashboard.h"
#include "FreeRTOS.h"
#include "task.h"

#ifndef OBSW_RENODE
#include "obsw/hal/stm32h7/hmc5883l.h"
#include "obsw/hal/stm32h7/qmc5883l.h"

/* Magnetometer read function for whichever chip answered at boot
 * (QMC5883L at 0x0D or HMC5883L at 0x1E); NULL if none. Both return Tesla. */
typedef bool (*mag_read_fn_t)(float b[3]);
static mag_read_fn_t s_mag_read = NULL;
#endif

#define AOCS_STACK_DEPTH 512U
#define AOCS_PRIORITY    2U   /* Lowest — periodic 10 Hz control loop */
#define AOCS_DT_S        0.1f /* 10 Hz fixed timestep */

static StaticTask_t      aocs_tcb;
static StackType_t       aocs_stack[AOCS_STACK_DEPTH];

static obsw_bdot_ctx_t   s_bdot;
static obsw_adcs_ctx_t   s_adcs;

/* HK state — written each tick, read by FDIR task for S3 TM(3,25) reports. */
static obsw_aocs_hk_t    s_hk;

static void aocs_task(void *param)
{
    (void)param;

    obsw_fsm_mode_t last_mode = obsw_fsm_mode(obsw_mode_get_fsm());
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));

        obsw_fsm_mode_t mode = obsw_fsm_mode(obsw_mode_get_fsm());

        /* Reset B-dot derivative accumulator on every SAFE entry from NOMINAL */
        if (mode == OBSW_FSM_SAFE && last_mode == OBSW_FSM_NOMINAL)
            obsw_bdot_reset(&s_bdot);
        last_mode = mode;

        /* STANDBY: AOCS off — actuators hold zero command */
        if (mode == OBSW_FSM_STANDBY) {
            s_hk.law = (uint8_t)OBSW_DASH_AOCS_OFF;
            continue;
        }

        /* Sync S20-tunable gains — reads are atomic (32-bit, single-core ARM) */
        s_bdot.config.gain    = obsw_pus_s20_get_float(SRDB_PARAM_BDOT_GAIN,    1.0e4f);
        s_bdot.config.hpf_tau = obsw_pus_s20_get_float(SRDB_PARAM_BDOT_HPF_TAU, 15.0f);
        s_adcs.config.kp      = obsw_pus_s20_get_float(SRDB_PARAM_ADCS_KP,      0.5f);
        s_adcs.config.kd      = obsw_pus_s20_get_float(SRDB_PARAM_ADCS_KD,      0.1f);

        /* ---- Sensor reads ---- */
        float b[3]             = {0.0f, 0.0f, 0.0f};
        obsw_quat_t q_meas     = {1.0f, 0.0f, 0.0f, 0.0f};
        float omega[3]         = {0.0f, 0.0f, 0.0f};
        bool  st_valid         = false;
        bool  gyro_valid       = false;
#ifndef OBSW_RENODE
        bool  mag_valid        = (s_mag_read != NULL) && s_mag_read(b);
#else
        bool  mag_valid        = false;
#endif

        /* Update shared HK state — each field is a single 32-bit write,
         * atomic on Cortex-M7.  mag_valid is written last so the reader
         * (FDIR S3 tick) always sees consistent data when valid=1. */
        s_hk.mag_x     = b[0];
        s_hk.mag_y     = b[1];
        s_hk.mag_z     = b[2];
        s_hk.mag_valid = (uint8_t)mag_valid;

        /* ---- Control law selection ---- */
        if (mode == OBSW_FSM_NOMINAL && st_valid && gyro_valid) {
            obsw_adcs_output_t out;
            obsw_adcs_step(&s_adcs, &q_meas, omega, &out);
            s_hk.law = (uint8_t)OBSW_DASH_AOCS_PD;
            /* TODO: write out.torque_cmd to RW driver */
        } else if (mode == OBSW_FSM_SAFE && mag_valid) {
            obsw_bdot_output_t out;
            obsw_bdot_step(&s_bdot, b, AOCS_DT_S, &out);
            s_hk.m_cmd_x = out.m_cmd[0];
            s_hk.m_cmd_y = out.m_cmd[1];
            s_hk.m_cmd_z = out.m_cmd[2];
            s_hk.dipole_valid = 1U;
            s_hk.law = (uint8_t)OBSW_DASH_AOCS_BDOT;
            /* TODO: write out.m_cmd to MTQ driver */
        } else {
            s_hk.law = (uint8_t)OBSW_DASH_AOCS_IDLE;
        }
        /* No valid sensors → m_cmd holds last command (safe by zero-init) */
    }
}

void obsw_aocs_task_init(void)
{
    obsw_bdot_config_t bdot_cfg = {.gain = 1.0e4f, .max_dipole = 10.0f, .hpf_tau = 15.0f};
    obsw_bdot_init(&s_bdot, &bdot_cfg);

    obsw_adcs_config_t adcs_cfg = {.kp = 0.5f, .kd = 0.1f, .max_torque = 0.01f};
    obsw_adcs_init(&s_adcs, &adcs_cfg);

#ifndef OBSW_RENODE
    /* Probed once at boot: a sensor plugged in later needs a reset. */
    if (obsw_qmc5883l_init())
        s_mag_read = obsw_qmc5883l_read;
    else if (obsw_hmc5883l_init())
        s_mag_read = obsw_hmc5883l_read;
#endif

    xTaskCreateStatic(aocs_task, "AOCS", AOCS_STACK_DEPTH,
                      NULL, AOCS_PRIORITY, aocs_stack, &aocs_tcb);
}

const obsw_aocs_hk_t *obsw_aocs_get_hk(void)
{
    return &s_hk;
}
