/**
 * @file dashboard.c
 * @brief Fixed-layout status dashboard: link statistics and frame formatter.
 *
 * See obsw/ui/dashboard.h for the layout. Implements OBSW-SRS-310..312.
 */

#include "obsw/ui/dashboard.h"

#include "obsw/ccsds/space_packet.h"
#include "obsw/pus/pus_tm.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* PUS-C TM secondary header: [0] version, [1] service, [2] subservice */
#define TM_SVC_OFFSET (OBSW_SP_PRIMARY_HEADER_LEN + 1U)
#define TM_SUBSVC_OFFSET (OBSW_SP_PRIMARY_HEADER_LEN + 2U)
#define TM_DATA_OFFSET (OBSW_SP_PRIMARY_HEADER_LEN + OBSW_PUS_TM_SEC_HDR_LEN)

/* Row indices */
#define ROW_TITLE 0U
#define ROW_OBT 1U
#define ROW_TC 2U
#define ROW_TM 3U
#define ROW_EVT 4U
#define ROW_AOCS 5U
#define ROW_MAG 6U
#define ROW_DIPOLE 7U
#define ROW_UP 8U
#define ROW_STATUS 9U

/* Physical-value fields: right-aligned, width 7, always one leading space */
#define VAL_WIDTH 7U
#define VAL_COL0 5U

/* ------------------------------------------------------------------ */
/* Link statistics                                                      */
/* ------------------------------------------------------------------ */

static void count_sat(uint32_t *counter)
{
    if (*counter < OBSW_DASH_COUNTER_MAX) {
        (*counter)++;
    }
}

void obsw_dash_link_init(obsw_dash_link_t *link)
{
    if (link != NULL) {
        (void)memset(link, 0, sizeof(*link));
    }
}

void obsw_dash_link_note_tc(obsw_dash_link_t *link)
{
    if (link != NULL) {
        count_sat(&link->tc_rx);
    }
}

void obsw_dash_link_note_tm(obsw_dash_link_t *link, const uint8_t *pkt, uint16_t len)
{
    if ((link == NULL) || (pkt == NULL)) {
        return;
    }
    count_sat(&link->tm_tx);
    if (len < TM_DATA_OFFSET) {
        return;
    }

    uint8_t svc       = pkt[TM_SVC_OFFSET];
    uint8_t subsvc    = pkt[TM_SUBSVC_OFFSET];
    link->has_last_tm = true;
    link->last_svc    = svc;
    link->last_subsvc = subsvc;

    if ((svc == 1U) && ((subsvc == 2U) || (subsvc == 8U))) {
        count_sat(&link->tc_rej);
    }
    if ((svc == 5U) && (subsvc >= 1U) && (subsvc <= 4U) && (len >= (TM_DATA_OFFSET + 2U))) {
        link->last_event_sev = subsvc;
        link->last_event_id =
            (uint16_t)(((uint16_t)pkt[TM_DATA_OFFSET] << 8U) | (uint16_t)pkt[TM_DATA_OFFSET + 1U]);
    }
}

/* ------------------------------------------------------------------ */
/* Field writers (all clip at the row end)                              */
/* ------------------------------------------------------------------ */

/* Least-significant decimal digit of @p v as a character. */
static char digit(uint32_t v)
{
    static const char digits[] = "0123456789";
    return digits[v % 10U];
}

static void put_char(char *row, uint8_t col, char c)
{
    if (col < OBSW_DASH_COLS) {
        row[col] = c;
    }
}

/* Write a NUL-terminated string; returns the column after the last char. */
static uint8_t put_str(char *row, uint8_t col, const char *s)
{
    uint8_t c = col;
    if (s != NULL) {
        for (const char *p = s; (*p != '\0') && (c < OBSW_DASH_COLS); p++) {
            row[c] = *p;
            c++;
        }
    }
    return c;
}

/* Zero-padded decimal, exactly @p width digits (1..9); saturates at 10^width - 1. */
static void put_uint(char *row, uint8_t col, uint32_t value, uint8_t width)
{
    uint32_t max = 1U;
    for (uint8_t i = 0U; i < width; i++) {
        max *= 10U;
    }
    uint32_t v = (value >= max) ? (max - 1U) : value;
    for (uint8_t i = width; i > 0U; i--) {
        put_char(row, (uint8_t)(col + i - 1U), digit(v));
        v /= 10U;
    }
}

/* Four upper-case hex digits. */
static void put_hex4(char *row, uint8_t col, uint16_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t i = 0U; i < 4U; i++) {
        uint8_t shift = (uint8_t)(12U - (4U * i));
        put_char(row, (uint8_t)(col + i), hex[((uint32_t)value >> shift) & 0xFU]);
    }
}

/*
 * Signed fixed-point value, right-aligned in VAL_WIDTH columns with at least
 * one leading space: sign, integer digits, '.', @p decimals digits (1 or 2).
 * Clamped to +/-(10^(VAL_WIDTH-3) - 1) / 10^decimals, e.g. 999.9 or 99.99.
 */
static void put_fixed(char *row, uint8_t col, float value, uint8_t decimals)
{
    for (uint8_t i = 0U; i < VAL_WIDTH; i++) {
        put_char(row, (uint8_t)(col + i), ' ');
    }
    if (isnan(value) != 0) {
        (void)put_str(row, (uint8_t)(col + VAL_WIDTH - 3U), "nan");
        return;
    }

    float scale            = (decimals >= 2U) ? 100.0f : 10.0f;
    const float max_scaled = 9999.0f; /* 10^(VAL_WIDTH - 3) - 1 */
    float scaled           = value * scale;
    bool negative          = (scaled < 0.0f);
    float mag              = negative ? -scaled : scaled;
    mag += 0.5f;
    if (mag > max_scaled) {
        mag = max_scaled;
    }
    uint32_t v = (uint32_t)mag;
    if (v == 0U) {
        negative = false; /* no "-0.0" */
    }

    uint8_t c = (uint8_t)(col + VAL_WIDTH - 1U);
    for (uint8_t d = 0U; d < decimals; d++) {
        put_char(row, c, digit(v));
        v /= 10U;
        c--;
    }
    put_char(row, c, '.');
    c--;
    do {
        put_char(row, c, digit(v));
        v /= 10U;
        c--;
    } while (v > 0U);
    put_char(row, c, negative ? '-' : '+');
}

/* ------------------------------------------------------------------ */
/* Labels                                                               */
/* ------------------------------------------------------------------ */

static const char *mode_name(obsw_fsm_mode_t mode)
{
    const char *name;
    switch (mode) {
    case OBSW_FSM_NOMINAL:
        name = "NOMINAL";
        break;
    case OBSW_FSM_SAFE:
        name = "SAFE";
        break;
    case OBSW_FSM_STANDBY:
        name = "STANDBY";
        break;
    default:
        name = "?";
        break;
    }
    return name;
}

static const char *reset_name(obsw_dash_reset_t reset)
{
    const char *name;
    switch (reset) {
    case OBSW_DASH_RESET_POR:
        name = "POR";
        break;
    case OBSW_DASH_RESET_PIN:
        name = "PIN";
        break;
    case OBSW_DASH_RESET_BOR:
        name = "BOR";
        break;
    case OBSW_DASH_RESET_SW:
        name = "SW";
        break;
    case OBSW_DASH_RESET_IWDG:
        name = "IWDG";
        break;
    case OBSW_DASH_RESET_WWDG:
        name = "WWDG";
        break;
    default:
        name = "?";
        break;
    }
    return name;
}

static const char *aocs_name(obsw_dash_aocs_t aocs)
{
    const char *name;
    switch (aocs) {
    case OBSW_DASH_AOCS_OFF:
        name = "OFF";
        break;
    case OBSW_DASH_AOCS_IDLE:
        name = "IDLE";
        break;
    case OBSW_DASH_AOCS_BDOT:
        name = "BDOT";
        break;
    case OBSW_DASH_AOCS_PD:
        name = "PD";
        break;
    default:
        name = "?";
        break;
    }
    return name;
}

/* TM(5,1..4) subservice → severity label */
static const char *severity_name(uint8_t subsvc)
{
    const char *name;
    switch (subsvc) {
    case 1U:
        name = "INFO";
        break;
    case 2U:
        name = "LOW";
        break;
    case 3U:
        name = "MED";
        break;
    case 4U:
        name = "HIGH";
        break;
    default:
        name = "?";
        break;
    }
    return name;
}

/* ------------------------------------------------------------------ */
/* Rows                                                                 */
/* ------------------------------------------------------------------ */

static obsw_dash_style_t row_obt(char *row, const obsw_dash_snapshot_t *s)
{
    (void)put_str(row, 0U, "OBT");
    put_uint(row, 4U, s->obt_s, 8U);
    (void)put_str(row, 14U, "RST");
    (void)put_str(row, 18U, reset_name(s->reset));
    bool bad_reset = (s->reset == OBSW_DASH_RESET_IWDG) || (s->reset == OBSW_DASH_RESET_WWDG) ||
                     (s->reset == OBSW_DASH_RESET_BOR);
    return bad_reset ? OBSW_DASH_STYLE_WARN : OBSW_DASH_STYLE_NORMAL;
}

static obsw_dash_style_t row_tc(char *row, const obsw_dash_link_t *l)
{
    (void)put_str(row, 0U, "TC rx");
    put_uint(row, 6U, l->tc_rx, 6U);
    (void)put_str(row, 14U, "rej");
    put_uint(row, 18U, l->tc_rej, 6U);
    return OBSW_DASH_STYLE_NORMAL;
}

static obsw_dash_style_t row_tm(char *row, const obsw_dash_link_t *l)
{
    (void)put_str(row, 0U, "TM tx");
    put_uint(row, 6U, l->tm_tx, 6U);
    uint8_t c = put_str(row, 14U, "last ");
    if (l->has_last_tm) {
        uint8_t svc_w = (l->last_svc >= 100U) ? 3U : ((l->last_svc >= 10U) ? 2U : 1U);
        uint8_t sub_w = (l->last_subsvc >= 100U) ? 3U : ((l->last_subsvc >= 10U) ? 2U : 1U);
        put_uint(row, c, l->last_svc, svc_w);
        c += svc_w;
        put_char(row, c, ',');
        c++;
        put_uint(row, c, l->last_subsvc, sub_w);
    } else {
        (void)put_str(row, c, "---");
    }
    return OBSW_DASH_STYLE_NORMAL;
}

static obsw_dash_style_t row_evt(char *row, const obsw_dash_link_t *l)
{
    obsw_dash_style_t style = OBSW_DASH_STYLE_DIM;
    (void)put_str(row, 0U, "EVT");
    if (l->last_event_sev == 0U) {
        (void)put_str(row, 4U, "---");
    } else {
        (void)put_str(row, 4U, "0x");
        put_hex4(row, 6U, l->last_event_id);
        (void)put_str(row, 11U, severity_name(l->last_event_sev));
        if (l->last_event_sev >= 4U) {
            style = OBSW_DASH_STYLE_ALARM;
        } else if (l->last_event_sev >= 2U) {
            style = OBSW_DASH_STYLE_WARN;
        } else {
            style = OBSW_DASH_STYLE_NORMAL;
        }
    }
    return style;
}

static obsw_dash_style_t row_aocs(char *row, const obsw_dash_snapshot_t *s)
{
    (void)put_str(row, 0U, "AOCS");
    (void)put_str(row, 5U, aocs_name(s->aocs));
    (void)put_str(row, 14U, "MAG");
    (void)put_str(row, 18U, s->mag_valid ? "ok" : "--");
    return s->mag_valid ? OBSW_DASH_STYLE_NORMAL : OBSW_DASH_STYLE_WARN;
}

static obsw_dash_style_t row_vec(
    char *row, const char *label, bool valid, const float v[3], float unit_scale, uint8_t decimals)
{
    (void)put_str(row, 0U, label);
    if (!valid) {
        (void)put_str(row, (uint8_t)(VAL_COL0 + 1U), "---");
        return OBSW_DASH_STYLE_DIM;
    }
    for (uint8_t i = 0U; i < 3U; i++) {
        put_fixed(row, (uint8_t)(VAL_COL0 + (i * VAL_WIDTH)), v[i] * unit_scale, decimals);
    }
    return OBSW_DASH_STYLE_NORMAL;
}

static obsw_dash_style_t row_up(char *row, const obsw_dash_snapshot_t *s)
{
    uint32_t hours = s->uptime_s / 3600U;
    uint32_t mins  = (s->uptime_s / 60U) % 60U;
    uint32_t secs  = s->uptime_s % 60U;
    (void)put_str(row, 0U, "UP");
    put_uint(row, 3U, hours, 3U);
    put_char(row, 6U, ':');
    put_uint(row, 7U, mins, 2U);
    put_char(row, 9U, ':');
    put_uint(row, 10U, secs, 2U);
    (void)put_str(row, 14U, "WDG");
    put_uint(row, 18U, s->wdg_kicks, 8U);
    return OBSW_DASH_STYLE_NORMAL;
}

static obsw_dash_style_t row_status(char *row, obsw_fsm_mode_t mode)
{
    obsw_dash_style_t style;
    (void)put_str(row, 0U, "MODE");
    (void)put_str(row, 5U, mode_name(mode));
    switch (mode) {
    case OBSW_FSM_NOMINAL:
        style = OBSW_DASH_STYLE_BAR_OK;
        break;
    case OBSW_FSM_SAFE:
        style = OBSW_DASH_STYLE_BAR_ALARM;
        break;
    default:
        style = OBSW_DASH_STYLE_BAR_WARN;
        break;
    }
    return style;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void obsw_dash_format(const obsw_dash_snapshot_t *snap, const char *title, obsw_dash_frame_t *frame)
{
    if ((snap == NULL) || (frame == NULL)) {
        return;
    }
    (void)memset(frame->text, (int)' ', sizeof(frame->text));

    (void)put_str(frame->text[ROW_TITLE], 0U, title);
    frame->style[ROW_TITLE] = OBSW_DASH_STYLE_TITLE;
    frame->style[ROW_OBT]   = row_obt(frame->text[ROW_OBT], snap);
    frame->style[ROW_TC]    = row_tc(frame->text[ROW_TC], &snap->link);
    frame->style[ROW_TM]    = row_tm(frame->text[ROW_TM], &snap->link);
    frame->style[ROW_EVT]   = row_evt(frame->text[ROW_EVT], &snap->link);
    frame->style[ROW_AOCS]  = row_aocs(frame->text[ROW_AOCS], snap);
    frame->style[ROW_MAG] =
        row_vec(frame->text[ROW_MAG], "B uT", snap->mag_valid, snap->mag_t, 1.0e6f, 1U);
    frame->style[ROW_DIPOLE] =
        row_vec(frame->text[ROW_DIPOLE], "M Am2", snap->dipole_valid, snap->dipole, 1.0f, 2U);
    frame->style[ROW_UP]     = row_up(frame->text[ROW_UP], snap);
    frame->style[ROW_STATUS] = row_status(frame->text[ROW_STATUS], snap->mode);
}
