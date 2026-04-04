#include "obsw/aocs/adcs.h"
#include "unity.h"
#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void setUp(void)
{
}
void tearDown(void)
{
}

static obsw_adcs_config_t default_cfg(void)
{
    return (obsw_adcs_config_t){.kp = 1.0f, .kd = 0.1f, .max_torque = 1.0f};
}

static obsw_quat_t identity(void)
{
    return (obsw_quat_t){1.0f, 0.0f, 0.0f, 0.0f};
}

/* ------------------------------------------------------------------ */
/* Quaternion math tests                                               */
/* ------------------------------------------------------------------ */

void test_quat_normalise_identity(void)
{
    obsw_quat_t q = {2.0f, 0.0f, 0.0f, 0.0f};
    TEST_ASSERT_TRUE(obsw_quat_normalise(&q));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, q.w);
}

void test_quat_normalise_zero_fails(void)
{
    obsw_quat_t q = {0.0f, 0.0f, 0.0f, 0.0f};
    TEST_ASSERT_FALSE(obsw_quat_normalise(&q));
}

void test_quat_conjugate(void)
{
    obsw_quat_t q = {1.0f, 2.0f, 3.0f, 4.0f};
    obsw_quat_t c = obsw_quat_conjugate(&q);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.w);
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, c.x);
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, c.y);
    TEST_ASSERT_EQUAL_FLOAT(-4.0f, c.z);
}

void test_quat_multiply_by_conjugate_gives_identity(void)
{
    /* q ⊗ q* = identity */
    obsw_quat_t q = {0.0f, 1.0f, 0.0f, 0.0f};
    obsw_quat_normalise(&q);
    obsw_quat_t qc = obsw_quat_conjugate(&q);
    obsw_quat_t r  = obsw_quat_multiply(&q, &qc);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, fabsf(r.w));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, r.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, r.z);
}

/* ------------------------------------------------------------------ */
/* Controller tests                                                    */
/* ------------------------------------------------------------------ */

void test_zero_error_zero_torque(void)
{
    obsw_adcs_ctx_t ctx;
    obsw_adcs_config_t cfg = default_cfg();
    obsw_adcs_init(&ctx, &cfg);

    /* Measured = target = identity → zero error */
    obsw_quat_t q  = identity();
    float omega[3] = {0.0f, 0.0f, 0.0f};
    obsw_adcs_output_t out;

    TEST_ASSERT_TRUE(obsw_adcs_step(&ctx, &q, omega, &out));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out.torque_cmd[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out.torque_cmd[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out.torque_cmd[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out.angle_err_rad);
}

void test_rate_damping(void)
{
    /* Zero attitude error but non-zero rate → derivative term fires */
    obsw_adcs_ctx_t ctx;
    obsw_adcs_config_t cfg = {.kp = 0.0f, .kd = 1.0f, .max_torque = 10.0f};
    obsw_adcs_init(&ctx, &cfg);

    obsw_quat_t q  = identity();
    float omega[3] = {0.1f, 0.0f, 0.0f};
    obsw_adcs_output_t out;

    obsw_adcs_step(&ctx, &q, omega, &out);

    /* τ = -Kd * ω → -1.0 * 0.1 = -0.1 on x axis */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.1f, out.torque_cmd[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out.torque_cmd[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out.torque_cmd[2]);
}

void test_attitude_error_drives_torque(void)
{
    /* 90° rotation about Z → vector part = (0,0,sin45°) */
    obsw_adcs_ctx_t ctx;
    obsw_adcs_config_t cfg = {.kp = 1.0f, .kd = 0.0f, .max_torque = 10.0f};
    obsw_adcs_init(&ctx, &cfg);

    /* Measured: 90° rotation about Z */
    float half         = (float)(M_PI / 4.0);
    obsw_quat_t q_meas = {cosf(half), 0.0f, 0.0f, sinf(half)};
    float omega[3]     = {0.0f, 0.0f, 0.0f};
    obsw_adcs_output_t out;

    obsw_adcs_step(&ctx, &q_meas, omega, &out);

    /* Error should drive torque on Z axis */
    TEST_ASSERT_TRUE(fabsf(out.torque_cmd[2]) > 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, out.torque_cmd[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, out.torque_cmd[1]);
    /* Angle error ≈ 90° = π/2 */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, (float)(M_PI / 2.0), out.angle_err_rad);
}

void test_torque_saturation(void)
{
    obsw_adcs_ctx_t ctx;
    obsw_adcs_config_t cfg = {.kp = 100.0f, .kd = 0.0f, .max_torque = 0.01f};
    obsw_adcs_init(&ctx, &cfg);

    /* Large error */
    float half         = (float)(M_PI / 4.0);
    obsw_quat_t q_meas = {cosf(half), sinf(half), 0.0f, 0.0f};
    float omega[3]     = {0.0f, 0.0f, 0.0f};
    obsw_adcs_output_t out;

    obsw_adcs_step(&ctx, &q_meas, omega, &out);

    /* All torques saturated */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(fabsf(out.torque_cmd[i]) <= 0.01f + 1e-6f);
    }
}

void test_set_target(void)
{
    obsw_adcs_ctx_t ctx;
    obsw_adcs_config_t cfg = default_cfg();
    obsw_adcs_init(&ctx, &cfg);

    /* Set target to 90° around X */
    float half         = (float)(M_PI / 4.0);
    obsw_quat_t target = {cosf(half), sinf(half), 0.0f, 0.0f};
    obsw_adcs_set_target(&ctx, &target);

    /* If measured = target → zero torque */
    float omega[3] = {0.0f, 0.0f, 0.0f};
    obsw_adcs_output_t out;
    obsw_adcs_step(&ctx, &target, omega, &out);

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out.torque_cmd[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out.torque_cmd[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out.torque_cmd[2]);
}

void test_null_inputs_return_false(void)
{
    obsw_adcs_ctx_t ctx;
    obsw_adcs_config_t cfg = default_cfg();
    obsw_adcs_init(&ctx, &cfg);

    obsw_quat_t q  = identity();
    float omega[3] = {0.0f, 0.0f, 0.0f};
    obsw_adcs_output_t out;

    TEST_ASSERT_FALSE(obsw_adcs_step(NULL, &q, omega, &out));
    TEST_ASSERT_FALSE(obsw_adcs_step(&ctx, NULL, omega, &out));
    TEST_ASSERT_FALSE(obsw_adcs_step(&ctx, &q, NULL, &out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_quat_normalise_identity);
    RUN_TEST(test_quat_normalise_zero_fails);
    RUN_TEST(test_quat_conjugate);
    RUN_TEST(test_quat_multiply_by_conjugate_gives_identity);
    RUN_TEST(test_zero_error_zero_torque);
    RUN_TEST(test_rate_damping);
    RUN_TEST(test_attitude_error_drives_torque);
    RUN_TEST(test_torque_saturation);
    RUN_TEST(test_set_target);
    RUN_TEST(test_null_inputs_return_false);
    return UNITY_END();
}