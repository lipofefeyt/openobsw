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

void test_hpf_disabled_matches_raw(void)
{
    /* hpf_tau = 0 → dbdt_filt must equal dbdt (no filtering) */
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = {.gain = 1e4f, .max_dipole = 100.0f, .hpf_tau = 0.0f};
    obsw_bdot_init(&ctx, &cfg);

    float b0[3] = {0.0f, 0.0f, 0.0f};
    float b1[3] = {1e-4f, 2e-4f, -1e-4f};
    obsw_bdot_output_t out;

    obsw_bdot_step(&ctx, b0, 0.1f, &out);
    obsw_bdot_step(&ctx, b1, 0.1f, &out);

    TEST_ASSERT_FLOAT_WITHIN(1e-9f, out.dbdt[0], out.dbdt_filt[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, out.dbdt[1], out.dbdt_filt[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, out.dbdt[2], out.dbdt_filt[2]);
}

void test_hpf_attenuates_dc_drift(void)
{
    /* A slowly-rising field (orbital drift) must be attenuated by the HPF.
     * After many steps the filtered dB/dt should be much smaller than the
     * raw dB/dt for a constant-rate input (DC → 0 in steady state). */
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = {.gain = 1.0f, .max_dipole = 1000.0f, .hpf_tau = 10.0f};
    obsw_bdot_init(&ctx, &cfg);

    float dt = 0.1f;
    float step = 1e-7f;  /* tiny constant dB/dt — simulates orbital drift */
    float b[3] = {0.0f, 0.0f, 0.0f};
    obsw_bdot_output_t out;

    /* Drive 500 steps (~50 s >> tau=10 s) with constant dB/dt */
    for (int i = 0; i < 500; i++) {
        b[0] += step * dt;
        obsw_bdot_step(&ctx, b, dt, &out);
    }

    /* HPF in steady state with constant input → output → 0 */
    TEST_ASSERT_TRUE(fabsf(out.dbdt_filt[0]) < fabsf(out.dbdt[0]) * 0.01f);
}

void test_hpf_passes_attitude_rate(void)
{
    /* A higher-frequency oscillation (attitude tumble rate) must pass through.
     * After HPF settles, the filtered amplitude should be close to the raw
     * amplitude for a sinusoidal input well above the corner frequency. */
    obsw_bdot_ctx_t ctx;
    obsw_bdot_config_t cfg = {.gain = 1.0f, .max_dipole = 1000.0f, .hpf_tau = 10.0f};
    obsw_bdot_init(&ctx, &cfg);

    float dt = 0.1f;
    float freq = 0.5f;     /* 0.5 Hz — well above f_c = 1/(2π*10) ≈ 0.016 Hz */
    float amp  = 1e-5f;    /* field amplitude */
    obsw_bdot_output_t out;

    /* Run enough cycles for HPF to settle */
    for (int i = 0; i < 200; i++) {
        float t  = i * dt;
        float b[3] = {amp * sinf(2.0f * 3.14159265f * freq * t), 0.0f, 0.0f};
        obsw_bdot_step(&ctx, b, dt, &out);
    }

    /* At 0.5 Hz with tau=10s: |H(jω)| = ωτ/√(1+(ωτ)²) ≈ 0.9995 — nearly flat pass */
    TEST_ASSERT_TRUE(fabsf(out.dbdt_filt[0]) > fabsf(out.dbdt[0]) * 0.90f);
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
    RUN_TEST(test_hpf_disabled_matches_raw);
    RUN_TEST(test_hpf_attenuates_dc_drift);
    RUN_TEST(test_hpf_passes_attitude_rate);
    return UNITY_END();
}