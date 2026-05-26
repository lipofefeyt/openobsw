#include "obsw/task/aocs.h"
#include "obsw/aocs/bdot.h"
#include "obsw/aocs/adcs.h"
#include "obsw/fdir/fsm.h"
#include "FreeRTOS.h"
#include "task.h"

#define AOCS_STACK_DEPTH 512U
#define AOCS_PRIORITY    2U   /* Lowest — periodic 10 Hz control loop */
#define AOCS_DT_S        0.1f /* 10 Hz fixed timestep */

static StaticTask_t      aocs_tcb;
static StackType_t       aocs_stack[AOCS_STACK_DEPTH];

static obsw_fsm_ctx_t   *s_fsm;
static obsw_bdot_ctx_t   s_bdot;
static obsw_adcs_ctx_t   s_adcs;

static void aocs_task(void *param)
{
    (void)param;

    obsw_fsm_mode_t last_mode = obsw_fsm_mode(s_fsm);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));

        obsw_fsm_mode_t mode = obsw_fsm_mode(s_fsm);

        /* Reset B-dot derivative accumulator on every SAFE entry */
        if (mode == OBSW_FSM_SAFE && last_mode == OBSW_FSM_NOMINAL)
            obsw_bdot_reset(&s_bdot);
        last_mode = mode;

        /* ---- Sensor read stubs (TODO: wire real I2C/SPI drivers) ---- */
        float b[3]             = {0.0f, 0.0f, 0.0f};
        bool  mag_valid        = false;
        obsw_quat_t q_meas     = {1.0f, 0.0f, 0.0f, 0.0f};
        float omega[3]         = {0.0f, 0.0f, 0.0f};
        bool  st_valid         = false;
        bool  gyro_valid       = false;

        /* ---- Control law selection ---- */
        if (mode == OBSW_FSM_NOMINAL && st_valid && gyro_valid) {
            obsw_adcs_output_t out;
            obsw_adcs_step(&s_adcs, &q_meas, omega, &out);
            /* TODO: write out.torque_cmd to RW driver */
        } else if (mag_valid) {
            obsw_bdot_output_t out;
            obsw_bdot_step(&s_bdot, b, AOCS_DT_S, &out);
            /* TODO: write out.m_cmd to MTQ driver */
        }
        /* No valid sensors → actuators hold last command (safe by zero-init) */
    }
}

void obsw_aocs_task_init(obsw_fsm_ctx_t *fsm)
{
    s_fsm = fsm;

    obsw_bdot_config_t bdot_cfg = {.gain = 1.0e4f, .max_dipole = 10.0f};
    obsw_bdot_init(&s_bdot, &bdot_cfg);

    obsw_adcs_config_t adcs_cfg = {.kp = 0.5f, .kd = 0.1f, .max_torque = 0.01f};
    obsw_adcs_init(&s_adcs, &adcs_cfg);

    xTaskCreateStatic(aocs_task, "AOCS", AOCS_STACK_DEPTH,
                      NULL, AOCS_PRIORITY, aocs_stack, &aocs_tcb);
}
