#include "obsw/aocs/bdot.h"
#include "unity.h"

#include <math.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static obsw_bdot_config_t default_cfg(void)
{
    return (obsw_bdot_config_t){.gain = 1.0f, .max_dipole = 10.0f};
}

void test_init_zeroes_state(void)
{
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = default_cfg();
    obsw_bdot_init(&ctx, &cfg);
    TEST_ASSERT_FALSE(ctx.initialised);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ctx.b_prev[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ctx.b_prev[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ctx.b_prev[2]);
}

void test_first_step_outputs_zero(void)
{
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = default_cfg();
    obsw_bdot_init(&ctx, &cfg);

    float b[3] = {1e-5f, 2e-5f, 3e-5f};
    obsw_bdot_output_t out;
    obsw_bdot_step(&ctx, b, 0.1f, &out);

    /* No derivative on first step */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.m_cmd[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.m_cmd[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.m_cmd[2]);
    TEST_ASSERT_TRUE(ctx.initialised);
}

void test_step_computes_dipole(void)
{
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = {.gain = 1e4f, .max_dipole = 100.0f};
    obsw_bdot_init(&ctx, &cfg);

    float b0[3] = {0.0f, 0.0f, 0.0f};
    float b1[3] = {1e-4f, 0.0f, 0.0f}; /* dB/dt = 1e-3 T/s */
    obsw_bdot_output_t out;

    obsw_bdot_step(&ctx, b0, 0.1f, &out); /* init */
    obsw_bdot_step(&ctx, b1, 0.1f, &out); /* compute */

    /* m_cmd = -k * dB/dt = -1e4 * 1e-3 = -10 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, out.m_cmd[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out.m_cmd[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out.m_cmd[2]);
}

void test_saturation(void)
{
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = {.gain = 1e6f, .max_dipole = 5.0f};
    obsw_bdot_init(&ctx, &cfg);

    float b0[3] = {0.0f, 0.0f, 0.0f};
    float b1[3] = {1.0f, 1.0f, 1.0f}; /* huge dB/dt */
    obsw_bdot_output_t out;

    obsw_bdot_step(&ctx, b0, 0.1f, &out);
    obsw_bdot_step(&ctx, b1, 0.1f, &out);

    /* All axes saturated at max_dipole */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.0f, out.m_cmd[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.0f, out.m_cmd[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.0f, out.m_cmd[2]);
}

void test_reset_clears_state(void)
{
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = default_cfg();
    obsw_bdot_init(&ctx, &cfg);

    float b[3] = {1e-5f, 0.0f, 0.0f};
    obsw_bdot_output_t out;
    obsw_bdot_step(&ctx, b, 0.1f, &out);
    TEST_ASSERT_TRUE(ctx.initialised);

    obsw_bdot_reset(&ctx);
    TEST_ASSERT_FALSE(ctx.initialised);

    /* After reset, first step outputs zero again */
    obsw_bdot_step(&ctx, b, 0.1f, &out);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.m_cmd[0]);
}

void test_opposing_field_reversal(void)
{
    /* B-dot should oppose field change direction */
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = {.gain = 1.0f, .max_dipole = 100.0f};
    obsw_bdot_init(&ctx, &cfg);

    float b0[3] = {0.0f, 0.0f, 1e-4f};
    float b1[3] = {0.0f, 0.0f, -1e-4f}; /* field decreasing */
    obsw_bdot_output_t out;

    obsw_bdot_step(&ctx, b0, 1.0f, &out);
    obsw_bdot_step(&ctx, b1, 1.0f, &out);

    /* dB/dt negative → m_cmd positive (opposing) */
    TEST_ASSERT_TRUE(out.m_cmd[2] > 0.0f);
}

void test_runtime_gain_update(void)
{
    /* Verify that updating config.gain mid-run changes the output magnitude */
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = {.gain = 1.0f, .max_dipole = 1000.0f};
    obsw_bdot_init(&ctx, &cfg);

    float b0[3] = {0.0f, 0.0f, 0.0f};
    float b1[3] = {1e-4f, 0.0f, 0.0f}; /* dB/dt = 1e-3 T/s over dt=0.1 */
    obsw_bdot_output_t out;

    obsw_bdot_step(&ctx, b0, 0.1f, &out); /* init */
    obsw_bdot_step(&ctx, b1, 0.1f, &out);
    float m_gain1 = out.m_cmd[0]; /* m = -1 * 1e-3 = -1e-3 */

    /* S20 TC(20,1) runtime update — double the gain */
    ctx.config.gain = 2.0f;
    obsw_bdot_reset(&ctx);
    obsw_bdot_step(&ctx, b0, 0.1f, &out);
    obsw_bdot_step(&ctx, b1, 0.1f, &out);
    float m_gain2 = out.m_cmd[0]; /* m = -2 * 1e-3 = -2e-3 */

    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.0f * m_gain1, m_gain2);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_zeroes_state);
    RUN_TEST(test_first_step_outputs_zero);
    RUN_TEST(test_step_computes_dipole);
    RUN_TEST(test_saturation);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_opposing_field_reversal);
    RUN_TEST(test_runtime_gain_update);
    return UNITY_END();
}