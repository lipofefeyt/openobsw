/**
 * @file test_dashboard.c
 * @brief Unit tests for the status dashboard formatter and link statistics.
 */

#include "obsw/ui/dashboard.h"
#include "unity.h"

#include <string.h>

static obsw_dash_snapshot_t snap;
static obsw_dash_frame_t frame;

void setUp(void)
{
    memset(&snap, 0, sizeof(snap));
    memset(&frame, 0xAA, sizeof(frame));
    obsw_dash_link_init(&snap.link);
    snap.mode  = OBSW_FSM_NOMINAL;
    snap.reset = OBSW_DASH_RESET_POR;
}
void tearDown(void)
{
}

/* Copy row @p r into a NUL-terminated string for comparison. */
static const char *row(uint8_t r)
{
    static char buf[OBSW_DASH_COLS + 1U];
    memcpy(buf, frame.text[r], OBSW_DASH_COLS);
    buf[OBSW_DASH_COLS] = '\0';
    return buf;
}

/* Minimal PUS-C TM packet: 6 B primary + 11 B secondary + data. */
static uint16_t make_tm(uint8_t *pkt, uint8_t svc, uint8_t subsvc, uint16_t event_id)
{
    memset(pkt, 0, 32);
    pkt[7]  = svc;
    pkt[8]  = subsvc;
    pkt[17] = (uint8_t)(event_id >> 8);
    pkt[18] = (uint8_t)(event_id & 0xFFU);
    return 19U;
}

/* @verifies OBSW-SRS-310 */
void test_layout_nominal(void)
{
    snap.obt_s               = 1234U;
    snap.uptime_s            = 3600U + 20U * 60U + 34U;
    snap.wdg_kicks           = 4834U;
    snap.link.tc_rx          = 12U;
    snap.link.tc_rej         = 1U;
    snap.link.tm_tx          = 42U;
    snap.link.has_last_tm    = true;
    snap.link.last_svc       = 17U;
    snap.link.last_subsvc    = 2U;
    snap.link.last_event_sev = 1U;
    snap.link.last_event_id  = 0x0001U;
    snap.aocs                = OBSW_DASH_AOCS_BDOT;
    snap.mag_valid           = true;
    snap.mag_t[0]            = 21.3e-6f;
    snap.mag_t[1]            = -4.0e-6f;
    snap.mag_t[2]            = 38.94e-6f;
    snap.dipole_valid        = true;
    snap.dipole[0]           = 0.12f;
    snap.dipole[1]           = -0.034f;
    snap.dipole[2]           = 0.4f;

    obsw_dash_format(&snap, "openobsw v0.18  STM32H750", &frame);

    TEST_ASSERT_EQUAL_STRING("openobsw v0.18  STM32H750 ", row(0));
    TEST_ASSERT_EQUAL_STRING("OBT 00001234  RST POR     ", row(1));
    TEST_ASSERT_EQUAL_STRING("TC rx 000012  rej 000001  ", row(2));
    TEST_ASSERT_EQUAL_STRING("TM tx 000042  last 17,2   ", row(3));
    TEST_ASSERT_EQUAL_STRING("EVT 0x0001 INFO           ", row(4));
    TEST_ASSERT_EQUAL_STRING("AOCS BDOT     MAG ok      ", row(5));
    TEST_ASSERT_EQUAL_STRING("B uT   +21.3   -4.0  +38.9", row(6));
    TEST_ASSERT_EQUAL_STRING("M Am2  +0.12  -0.03  +0.40", row(7));
    TEST_ASSERT_EQUAL_STRING("UP 001:20:34  WDG 00004834", row(8));
    TEST_ASSERT_EQUAL_STRING("MODE NOMINAL              ", row(9));

    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_TITLE, frame.style[0]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_NORMAL, frame.style[1]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_NORMAL, frame.style[4]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_NORMAL, frame.style[5]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_NORMAL, frame.style[6]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_BAR_OK, frame.style[9]);
}

/* @verifies OBSW-SRS-310 */
void test_title_truncated_and_null_title_blank(void)
{
    obsw_dash_format(&snap, "0123456789abcdefghijklmnopqrstuvwxyz", &frame);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdefghijklmnop", row(0));

    obsw_dash_format(&snap, NULL, &frame);
    TEST_ASSERT_EQUAL_STRING("                          ", row(0));
}

/* @verifies OBSW-SRS-310 */
void test_mode_status_bar_styles(void)
{
    snap.mode = OBSW_FSM_SAFE;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("MODE SAFE                 ", row(9));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_BAR_ALARM, frame.style[9]);

    snap.mode = OBSW_FSM_STANDBY;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("MODE STANDBY              ", row(9));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_BAR_WARN, frame.style[9]);
}

/* @verifies OBSW-SRS-310 */
void test_reset_cause_names_and_warning(void)
{
    snap.reset = OBSW_DASH_RESET_IWDG;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("OBT 00000000  RST IWDG    ", row(1));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_WARN, frame.style[1]);

    snap.reset = OBSW_DASH_RESET_BOR;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_WARN, frame.style[1]);

    snap.reset = OBSW_DASH_RESET_PIN;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("OBT 00000000  RST PIN     ", row(1));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_NORMAL, frame.style[1]);

    snap.reset = OBSW_DASH_RESET_UNKNOWN;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("OBT 00000000  RST ?       ", row(1));
}

/* @verifies OBSW-SRS-310, OBSW-SRS-311 */
void test_empty_link_and_invalid_sensors(void)
{
    snap.aocs = OBSW_DASH_AOCS_IDLE;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("TM tx 000000  last ---    ", row(3));
    TEST_ASSERT_EQUAL_STRING("EVT ---                   ", row(4));
    TEST_ASSERT_EQUAL_STRING("AOCS IDLE     MAG --      ", row(5));
    TEST_ASSERT_EQUAL_STRING("B uT  ---                 ", row(6));
    TEST_ASSERT_EQUAL_STRING("M Am2 ---                 ", row(7));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_DIM, frame.style[4]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_WARN, frame.style[5]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_DIM, frame.style[6]);
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_DIM, frame.style[7]);
}

/* @verifies OBSW-SRS-311 */
void test_counters_and_times_saturate(void)
{
    snap.obt_s       = 123456789U;
    snap.wdg_kicks   = 0xFFFFFFFFU;
    snap.uptime_s    = 0xFFFFFFFFU;
    snap.link.tc_rx  = 5000000U;
    snap.link.tc_rej = OBSW_DASH_COUNTER_MAX;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("OBT 99999999  RST POR     ", row(1));
    TEST_ASSERT_EQUAL_STRING("TC rx 999999  rej 999999  ", row(2));
    /* hours clamp at 999; minutes/seconds stay the true remainders */
    TEST_ASSERT_EQUAL_STRING("UP 999:28:15  WDG 99999999", row(8));
}

/* @verifies OBSW-SRS-311 */
void test_physical_values_clamp_round_and_nan(void)
{
    snap.mag_valid    = true;
    snap.mag_t[0]     = 1.0f;      /* 1e6 uT → clamps to 999.9 */
    snap.mag_t[1]     = -2.0e-3f;  /* -2000 uT → clamps */
    snap.mag_t[2]     = -0.04e-6f; /* rounds to 0.0, no "-0.0" */
    snap.dipole_valid = true;
    snap.dipole[0]    = 0.0051f;   /* rounds to nearest → 0.01 */
    snap.dipole[1]    = -123.0f;   /* clamps to -99.99 */
    snap.dipole[2]    = NAN;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("B uT  +999.9 -999.9   +0.0", row(6));
    TEST_ASSERT_EQUAL_STRING("M Am2  +0.01 -99.99    nan", row(7));
}

/* @verifies OBSW-SRS-310 */
void test_last_tm_three_digit_fields(void)
{
    snap.link.has_last_tm = true;
    snap.link.last_svc    = 255U;
    snap.link.last_subsvc = 128U;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("TM tx 000000  last 255,128", row(3));
}

/* @verifies OBSW-SRS-310 */
void test_event_severity_styles(void)
{
    snap.link.last_event_id  = 0xBEEFU;
    snap.link.last_event_sev = 2U;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("EVT 0xBEEF LOW            ", row(4));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_WARN, frame.style[4]);

    snap.link.last_event_sev = 3U;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("EVT 0xBEEF MED            ", row(4));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_WARN, frame.style[4]);

    snap.link.last_event_sev = 4U;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("EVT 0xBEEF HIGH           ", row(4));
    TEST_ASSERT_EQUAL(OBSW_DASH_STYLE_ALARM, frame.style[4]);
}

/* @verifies OBSW-SRS-310 */
void test_aocs_law_names(void)
{
    snap.aocs = OBSW_DASH_AOCS_OFF;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("AOCS OFF      MAG --      ", row(5));
    snap.aocs = OBSW_DASH_AOCS_PD;
    obsw_dash_format(&snap, "", &frame);
    TEST_ASSERT_EQUAL_STRING("AOCS PD       MAG --      ", row(5));
}

/* @verifies OBSW-SRS-310 */
void test_format_null_args_do_nothing(void)
{
    obsw_dash_format(NULL, "x", &frame);
    TEST_ASSERT_EQUAL_HEX8(0xAA, (uint8_t)frame.text[0][0]);
    obsw_dash_format(&snap, "x", NULL); /* must not crash */
}

/* @verifies OBSW-SRS-312 */
void test_link_counts_and_classifies_tm(void)
{
    obsw_dash_link_t l;
    uint8_t pkt[32];
    obsw_dash_link_init(&l);

    obsw_dash_link_note_tc(&l);
    obsw_dash_link_note_tc(&l);
    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 1U, 1U, 0U));
    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 17U, 2U, 0U));
    TEST_ASSERT_EQUAL_UINT32(2U, l.tc_rx);
    TEST_ASSERT_EQUAL_UINT32(2U, l.tm_tx);
    TEST_ASSERT_EQUAL_UINT32(0U, l.tc_rej);
    TEST_ASSERT_TRUE(l.has_last_tm);
    TEST_ASSERT_EQUAL_UINT8(17U, l.last_svc);
    TEST_ASSERT_EQUAL_UINT8(2U, l.last_subsvc);
    TEST_ASSERT_EQUAL_UINT8(0U, l.last_event_sev);

    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 1U, 2U, 0U));
    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 1U, 8U, 0U));
    TEST_ASSERT_EQUAL_UINT32(2U, l.tc_rej);

    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 5U, 4U, 0x1234U));
    TEST_ASSERT_EQUAL_UINT8(4U, l.last_event_sev);
    TEST_ASSERT_EQUAL_HEX16(0x1234U, l.last_event_id);

    /* Non-event TM keeps the last event */
    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 3U, 25U, 0U));
    TEST_ASSERT_EQUAL_UINT8(4U, l.last_event_sev);
    TEST_ASSERT_EQUAL_UINT8(3U, l.last_svc);
}

/* @verifies OBSW-SRS-312 */
void test_link_short_packets_counted_not_classified(void)
{
    obsw_dash_link_t l;
    uint8_t pkt[32];
    obsw_dash_link_init(&l);

    (void)make_tm(pkt, 1U, 2U, 0U);
    obsw_dash_link_note_tm(&l, pkt, 16U); /* shorter than headers */
    TEST_ASSERT_EQUAL_UINT32(1U, l.tm_tx);
    TEST_ASSERT_FALSE(l.has_last_tm);
    TEST_ASSERT_EQUAL_UINT32(0U, l.tc_rej);

    /* TM(5,1) without room for the event ID: classified, event not taken */
    (void)make_tm(pkt, 5U, 1U, 0x0042U);
    obsw_dash_link_note_tm(&l, pkt, 17U);
    TEST_ASSERT_TRUE(l.has_last_tm);
    TEST_ASSERT_EQUAL_UINT8(0U, l.last_event_sev);

    /* Out-of-range S5 subservice is not an event */
    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 5U, 9U, 0x0042U));
    TEST_ASSERT_EQUAL_UINT8(0U, l.last_event_sev);

    /* NULL arguments are ignored */
    obsw_dash_link_note_tm(NULL, pkt, 19U);
    obsw_dash_link_note_tm(&l, NULL, 19U);
    obsw_dash_link_note_tc(NULL);
    obsw_dash_link_init(NULL);
    TEST_ASSERT_EQUAL_UINT32(3U, l.tm_tx);
}

/* @verifies OBSW-SRS-312 */
void test_link_counters_saturate(void)
{
    obsw_dash_link_t l;
    uint8_t pkt[32];
    obsw_dash_link_init(&l);
    l.tc_rx  = OBSW_DASH_COUNTER_MAX;
    l.tm_tx  = OBSW_DASH_COUNTER_MAX;
    l.tc_rej = OBSW_DASH_COUNTER_MAX;
    obsw_dash_link_note_tc(&l);
    obsw_dash_link_note_tm(&l, pkt, make_tm(pkt, 1U, 2U, 0U));
    TEST_ASSERT_EQUAL_UINT32(OBSW_DASH_COUNTER_MAX, l.tc_rx);
    TEST_ASSERT_EQUAL_UINT32(OBSW_DASH_COUNTER_MAX, l.tm_tx);
    TEST_ASSERT_EQUAL_UINT32(OBSW_DASH_COUNTER_MAX, l.tc_rej);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_layout_nominal);
    RUN_TEST(test_title_truncated_and_null_title_blank);
    RUN_TEST(test_mode_status_bar_styles);
    RUN_TEST(test_reset_cause_names_and_warning);
    RUN_TEST(test_empty_link_and_invalid_sensors);
    RUN_TEST(test_counters_and_times_saturate);
    RUN_TEST(test_physical_values_clamp_round_and_nan);
    RUN_TEST(test_last_tm_three_digit_fields);
    RUN_TEST(test_event_severity_styles);
    RUN_TEST(test_aocs_law_names);
    RUN_TEST(test_format_null_args_do_nothing);
    RUN_TEST(test_link_counts_and_classifies_tm);
    RUN_TEST(test_link_short_packets_counted_not_classified);
    RUN_TEST(test_link_counters_saturate);
    return UNITY_END();
}
