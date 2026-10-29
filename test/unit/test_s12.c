#include "obsw/ccsds/space_packet.h"
#include "obsw/fdir/fsm.h"
#include "obsw/pus/s12.h"
#include "obsw/tm/store.h"
#include "unity.h"

#include <math.h>
#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define P_TEMP 0x0001U /* uint32 parameter */
#define P_GAIN 0x20A0U /* float parameter  */
#define EV_LOW 0x0101U
#define EV_HIGH 0x0102U

static obsw_tm_store_t store;
static obsw_s1_ctx_t s1;
static obsw_s5_ctx_t s5;
static obsw_fsm_ctx_t fsm;
static obsw_s12_ctx_t s12;
static obsw_s12_def_t defs[2];
static obsw_s20_param_t params[] = {
    {.param_id = P_TEMP, .value = {.u32 = 20}},
    {.param_id = P_GAIN, .value = {.f32 = 1.0e4f}, .type = OBSW_S20_F32},
};

static void setup_ctx(void)
{
    obsw_tm_store_init(&store);
    memset(&s1, 0, sizeof(s1));
    s1.tm_store = &store;
    s1.apid     = 0x010;
    memset(&s5, 0, sizeof(s5));
    s5.tm_store = &store;
    s5.apid     = 0x010;
    params[0].value.u32 = 20;
    params[1].value.f32 = 1.0e4f;
    obsw_s12_init(&s12, &s1, &s5, params, 2, defs, 2);
}

static uint8_t ud[32];

static obsw_tc_t make_tc(uint8_t sub, uint16_t len)
{
    obsw_tc_t tc = {.apid          = 0x010,
                    .service       = 12,
                    .subservice    = sub,
                    .seq_count     = 9,
                    .user_data     = ud,
                    .user_data_len = len};
    return tc;
}

static void put_f32(uint8_t *p, float f)
{
    uint32_t r;
    memcpy(&r, &f, 4);
    p[0] = (uint8_t)(r >> 24);
    p[1] = (uint8_t)(r >> 16);
    p[2] = (uint8_t)(r >> 8);
    p[3] = (uint8_t)r;
}

/* TC(12,3) for param with limits, events EV_LOW/EV_HIGH and severity */
static obsw_tc_t make_add(uint16_t pid, float low, float high, uint8_t sev)
{
    ud[0] = (uint8_t)(pid >> 8);
    ud[1] = (uint8_t)pid;
    put_f32(ud + 2, low);
    put_f32(ud + 6, high);
    ud[10] = (uint8_t)(EV_LOW >> 8);
    ud[11] = (uint8_t)EV_LOW;
    ud[12] = (uint8_t)(EV_HIGH >> 8);
    ud[13] = (uint8_t)EV_HIGH;
    ud[14] = sev;
    return make_tc(3, OBSW_S12_ADD_LEN);
}

static obsw_sp_packet_t dequeue(uint8_t *buf, size_t n)
{
    uint16_t len = 0;
    obsw_tm_store_dequeue(&store, buf, (uint16_t)n, &len);
    obsw_sp_packet_t pkt = {0};
    obsw_sp_parse(buf, len, &pkt);
    return pkt;
}

/* Expect exactly one TM(1,2) with this code and nothing else */
static void expect_rejected(uint16_t code)
{
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
    uint8_t buf[64];
    obsw_sp_packet_t p = dequeue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(1, p.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(2, p.payload[2]);
    TEST_ASSERT_EQUAL_HEX16(code, (uint16_t)((p.payload[15] << 8) | p.payload[16]));
}

/* Drop the S1 reports of a successful TC */
static void add_ok(uint16_t pid, float low, float high, uint8_t sev)
{
    obsw_tc_t tc = make_add(pid, low, high, sev);
    TEST_ASSERT_EQUAL_INT(0, obsw_s12_add(&tc, NULL, &s12));
    obsw_tm_store_init(&store);
}

/* Next TM must be TM(5,sev) for event_id carrying value as BE float */
static void expect_event(uint8_t sev, uint16_t event_id, float value)
{
    uint8_t buf[64];
    obsw_sp_packet_t p = dequeue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(5, p.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(sev, p.payload[2]);
    TEST_ASSERT_EQUAL_HEX16(event_id, (uint16_t)((p.payload[11] << 8) | p.payload[12]));
    uint32_t r = ((uint32_t)p.payload[13] << 24) | ((uint32_t)p.payload[14] << 16) |
                 ((uint32_t)p.payload[15] << 8) | p.payload[16];
    float f;
    memcpy(&f, &r, 4);
    TEST_ASSERT_EQUAL_FLOAT(value, f);
}

/* ---- Definitions ---------------------------------------------------- */

/* @verifies OBSW-SRS-290, OBSW-SRS-082 */
void test_add_stores_definition_and_reports_tm11_tm17(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(P_TEMP, 0.0f, 50.0f, 2);
    TEST_ASSERT_EQUAL_INT(0, obsw_s12_add(&tc, NULL, &s12));
    TEST_ASSERT_EQUAL_UINT8(1, s12.count);
    TEST_ASSERT_EQUAL_HEX16(P_TEMP, defs[0].param_id);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, defs[0].high);
    TEST_ASSERT_EQUAL_HEX16(EV_HIGH, defs[0].event_high);
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_UINT8(1, dequeue(buf, sizeof(buf)).payload[2]);
    TEST_ASSERT_EQUAL_UINT8(7, dequeue(buf, sizeof(buf)).payload[2]);
}

/* @verifies OBSW-SRS-290 */
void test_add_same_parameter_replaces(void)
{
    setup_ctx();
    add_ok(P_TEMP, 0.0f, 50.0f, 2);
    add_ok(P_TEMP, 0.0f, 10.0f, 3);
    TEST_ASSERT_EQUAL_UINT8(1, s12.count);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, defs[0].high);
    TEST_ASSERT_EQUAL_INT(OBSW_S5_MEDIUM, defs[0].severity);
}

/* @verifies OBSW-SRS-291 */
void test_add_unknown_parameter_rejected(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(0x7777, 0.0f, 1.0f, 2);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s12_add(&tc, NULL, &s12));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
    TEST_ASSERT_EQUAL_UINT8(0, s12.count);
}

/* @verifies OBSW-SRS-291 */
void test_add_bad_severity_rejected(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(P_TEMP, 0.0f, 1.0f, 5);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s12_add(&tc, NULL, &s12));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
    tc = make_add(P_TEMP, 0.0f, 1.0f, 0);
    obsw_s12_add(&tc, NULL, &s12);
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
    TEST_ASSERT_EQUAL_UINT8(0, s12.count);
}

/* @verifies OBSW-SRS-291 */
void test_add_low_above_high_rejected(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(P_TEMP, 10.0f, 5.0f, 2);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s12_add(&tc, NULL, &s12));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-291, OBSW-SRS-083 */
void test_add_truncated_rejected(void)
{
    setup_ctx();
    obsw_tc_t tc = make_add(P_TEMP, 0.0f, 1.0f, 2);
    tc.user_data_len = OBSW_S12_ADD_LEN - 1U;
    TEST_ASSERT_NOT_EQUAL(0, obsw_s12_add(&tc, NULL, &s12));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-291 */
void test_add_when_full_rejected(void)
{
    setup_ctx();
    add_ok(P_TEMP, 0.0f, 1.0f, 2);
    add_ok(P_GAIN, 0.0f, 1.0f, 2);
    s12.capacity = 2;
    obsw_s20_param_t extra[3] = {params[0], params[1], {.param_id = 0x0003}};
    s12.params      = extra;
    s12.param_count = 3;
    obsw_tc_t tc    = make_add(0x0003, 0.0f, 1.0f, 2);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s12_add(&tc, NULL, &s12));
    expect_rejected(OBSW_S1_FAIL_EXEC_ERROR);
    TEST_ASSERT_EQUAL_UINT8(2, s12.count);
}

/* @verifies OBSW-SRS-294 */
void test_delete_one_keeps_others(void)
{
    setup_ctx();
    add_ok(P_TEMP, 0.0f, 1.0f, 2);
    add_ok(P_GAIN, 0.0f, 2.0e4f, 2);
    ud[0]        = (uint8_t)(P_TEMP >> 8);
    ud[1]        = (uint8_t)P_TEMP;
    obsw_tc_t tc = make_tc(4, 2);
    TEST_ASSERT_EQUAL_INT(0, obsw_s12_delete(&tc, NULL, &s12));
    TEST_ASSERT_EQUAL_UINT8(1, s12.count);
    TEST_ASSERT_EQUAL_HEX16(P_GAIN, defs[0].param_id);
}

/* @verifies OBSW-SRS-294 */
void test_delete_unknown_rejected(void)
{
    setup_ctx();
    ud[0]        = 0x12;
    ud[1]        = 0x34;
    obsw_tc_t tc = make_tc(4, 2);
    TEST_ASSERT_NOT_EQUAL(0, obsw_s12_delete(&tc, NULL, &s12));
    expect_rejected(OBSW_S1_FAIL_ILLEGAL_APP_DATA);
}

/* @verifies OBSW-SRS-294 */
void test_delete_all(void)
{
    setup_ctx();
    add_ok(P_TEMP, 0.0f, 1.0f, 2);
    add_ok(P_GAIN, 0.0f, 1.0f, 2);
    obsw_tc_t tc = make_tc(5, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s12_delete_all(&tc, NULL, &s12));
    TEST_ASSERT_EQUAL_UINT8(0, s12.count);
}

/* ---- Checking -------------------------------------------------------- */

/* @verifies OBSW-SRS-292 */
void test_high_crossing_raises_event_once_until_recovery(void)
{
    setup_ctx();
    add_ok(P_TEMP, 0.0f, 50.0f, 2);

    obsw_s12_check(&s12);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store)); /* 20 in range */

    params[0].value.u32 = 60;
    obsw_s12_check(&s12);
    expect_event(2, EV_HIGH, 60.0f);
    obsw_s12_check(&s12);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store)); /* latched */

    params[0].value.u32 = 40;
    obsw_s12_check(&s12);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store));
    params[0].value.u32 = 70;
    obsw_s12_check(&s12);
    expect_event(2, EV_HIGH, 70.0f); /* crossed again */
}

/* @verifies OBSW-SRS-292, OBSW-SRS-172 */
void test_low_crossing_on_float_parameter(void)
{
    setup_ctx();
    add_ok(P_GAIN, 5.0e3f, NAN, 3);
    params[1].value.f32 = 4.5e3f;
    obsw_s12_check(&s12);
    expect_event(3, EV_LOW, 4.5e3f);
}

/* @verifies OBSW-SRS-292 */
void test_nan_limits_never_trip(void)
{
    setup_ctx();
    add_ok(P_TEMP, NAN, NAN, 2);
    params[0].value.u32 = 0xFFFFFFFFU;
    obsw_s12_check(&s12);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store));
}

/* @verifies OBSW-SRS-293 */
void test_disable_stops_checks_and_enable_rearms(void)
{
    setup_ctx();
    add_ok(P_TEMP, 0.0f, 50.0f, 2);
    params[0].value.u32 = 60;
    obsw_s12_check(&s12);
    obsw_tm_store_init(&store);

    obsw_tc_t off = make_tc(2, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s12_disable(&off, NULL, &s12));
    obsw_tm_store_init(&store);
    obsw_s12_check(&s12);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&store));

    obsw_tc_t on = make_tc(1, 0);
    TEST_ASSERT_EQUAL_INT(0, obsw_s12_enable(&on, NULL, &s12));
    obsw_tm_store_init(&store);
    obsw_s12_check(&s12); /* still out of limit: re-reported after enable */
    expect_event(2, EV_HIGH, 60.0f);
}

/* @verifies OBSW-SRS-295 */
void test_high_severity_safe_trigger_commands_safe(void)
{
    setup_ctx();
    obsw_fsm_config_t cfg = {0};
    obsw_fsm_init(&fsm, &cfg);
    obsw_fsm_to_nominal(&fsm);
    s5.fsm                 = (struct obsw_fsm_ctx *)&fsm;
    s5.safe_trigger_count  = 1;
    s5.safe_trigger_ids[0] = EV_HIGH;

    add_ok(P_TEMP, 0.0f, 50.0f, 4);
    params[0].value.u32 = 99;
    obsw_s12_check(&s12);
    TEST_ASSERT_TRUE(obsw_fsm_is_safe(&fsm));
}

/* @verifies OBSW-SRS-172 */
void test_s20_as_float_follows_type(void)
{
    obsw_s20_param_t u = {.param_id = 1, .value = {.u32 = 7}};
    obsw_s20_param_t i = {.param_id = 2, .value = {.i32 = -3}, .type = OBSW_S20_I32};
    obsw_s20_param_t f = {.param_id = 3, .value = {.f32 = 2.5f}, .type = OBSW_S20_F32};
    TEST_ASSERT_EQUAL_FLOAT(7.0f, obsw_s20_as_float(&u));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, obsw_s20_as_float(&i));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, obsw_s20_as_float(&f));
    obsw_s20_param_t tbl[] = {u, i, f};
    TEST_ASSERT_EQUAL_PTR(&tbl[1], obsw_s20_find(tbl, 3, 2));
    TEST_ASSERT_NULL(obsw_s20_find(tbl, 3, 9));
    TEST_ASSERT_NULL(obsw_s20_find(NULL, 0, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_stores_definition_and_reports_tm11_tm17);
    RUN_TEST(test_add_same_parameter_replaces);
    RUN_TEST(test_add_unknown_parameter_rejected);
    RUN_TEST(test_add_bad_severity_rejected);
    RUN_TEST(test_add_low_above_high_rejected);
    RUN_TEST(test_add_truncated_rejected);
    RUN_TEST(test_add_when_full_rejected);
    RUN_TEST(test_delete_one_keeps_others);
    RUN_TEST(test_delete_unknown_rejected);
    RUN_TEST(test_delete_all);
    RUN_TEST(test_high_crossing_raises_event_once_until_recovery);
    RUN_TEST(test_low_crossing_on_float_parameter);
    RUN_TEST(test_nan_limits_never_trip);
    RUN_TEST(test_disable_stops_checks_and_enable_rearms);
    RUN_TEST(test_high_severity_safe_trigger_commands_safe);
    RUN_TEST(test_s20_as_float_follows_type);
    return UNITY_END();
}
