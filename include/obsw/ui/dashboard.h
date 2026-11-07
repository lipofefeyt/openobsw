#ifndef OBSW_UI_DASHBOARD_H
#define OBSW_UI_DASHBOARD_H

/**
 * @file obsw/ui/dashboard.h
 * @brief Fixed-layout status dashboard: link statistics and frame formatter.
 *
 * Platform-independent half of the on-board status display. A target collects
 * an obsw_dash_snapshot_t once per refresh, calls obsw_dash_format() to lay it
 * out as a grid of OBSW_DASH_ROWS x OBSW_DASH_COLS characters with one style per
 * row, and hands the frame to its own renderer (e.g. the STM32H7 ST7735R LCD in
 * hal/stm32h7/lcd_dash.h). Styles are semantic; the renderer picks the colours.
 *
 * Layout (26 x 10, every field at a fixed column):
 *
 *     0  <title>
 *     1  OBT 00001234  RST POR
 *     2  TC rx 000012  rej 000001
 *     3  TM tx 000042  last 17,2
 *     4  EVT 0x0001 INFO
 *     5  AOCS BDOT     MAG ok
 *     6  B uT   +21.3   -4.0  +38.9
 *     7  M Am2  +0.12  -0.03  +0.40
 *     8  UP 000:20:34  WDG 00001234
 *     9  MODE NOMINAL                  (status bar)
 *
 * No dynamic allocation, no printf, no global state: every function works on
 * caller-owned storage.
 */

#include "obsw/fdir/fsm.h"

#include <stdbool.h>
#include <stdint.h>

#define OBSW_DASH_COLS 26U
#define OBSW_DASH_ROWS 10U

/** Largest value a 6-digit link counter shows; counters saturate here. */
#define OBSW_DASH_COUNTER_MAX 999999U

/* ------------------------------------------------------------------ */
/* Link statistics (fed from the TM downlink path)                     */
/* ------------------------------------------------------------------ */

/**
 * Downlink-side view of TC/TM traffic. Fed by whoever owns the link
 * (obsw_dash_link_note_tc / _note_tm); read by the dashboard. All counters
 * saturate at OBSW_DASH_COUNTER_MAX instead of wrapping.
 */
typedef struct {
    uint32_t tc_rx;         /**< TC frames received from the link            */
    uint32_t tm_tx;         /**< TM packets sent to the link                 */
    uint32_t tc_rej;        /**< TM(1,2) and TM(1,8) failure reports sent    */
    bool has_last_tm;       /**< last_svc / last_subsvc are valid            */
    uint8_t last_svc;       /**< Service type of the last TM sent            */
    uint8_t last_subsvc;    /**< Subservice of the last TM sent              */
    uint8_t last_event_sev; /**< Subservice of the last TM(5,x); 0 = none    */
    uint16_t last_event_id; /**< Event ID of the last TM(5,x)                */
} obsw_dash_link_t;

/** Zero all statistics. */
void obsw_dash_link_init(obsw_dash_link_t *link);

/** Count one TC frame received from the link. */
void obsw_dash_link_note_tc(obsw_dash_link_t *link);

/**
 * Count one TM packet sent and classify it. @p pkt is the full space packet
 * (primary header + PUS-C TM secondary header + data). Packets too short to
 * carry the PUS secondary header are counted but not classified.
 */
void obsw_dash_link_note_tm(obsw_dash_link_t *link, const uint8_t *pkt, uint16_t len);

/* ------------------------------------------------------------------ */
/* Snapshot and frame                                                   */
/* ------------------------------------------------------------------ */

/** Cause of the last processor reset, as decoded by the platform. */
typedef enum {
    OBSW_DASH_RESET_UNKNOWN = 0,
    OBSW_DASH_RESET_POR,  /**< Power-on                 */
    OBSW_DASH_RESET_PIN,  /**< External reset pin       */
    OBSW_DASH_RESET_BOR,  /**< Brown-out                */
    OBSW_DASH_RESET_SW,   /**< Software request         */
    OBSW_DASH_RESET_IWDG, /**< Independent watchdog     */
    OBSW_DASH_RESET_WWDG, /**< Window watchdog          */
} obsw_dash_reset_t;

/** Control law the AOCS ran on its last tick. */
typedef enum {
    OBSW_DASH_AOCS_OFF = 0, /**< STANDBY — actuators held at zero        */
    OBSW_DASH_AOCS_IDLE,    /**< Mode wants control, sensors not valid   */
    OBSW_DASH_AOCS_BDOT,    /**< SAFE — B-dot detumble                   */
    OBSW_DASH_AOCS_PD,      /**< NOMINAL — PD quaternion control         */
} obsw_dash_aocs_t;

/** Everything the dashboard shows, collected once per refresh. */
typedef struct {
    obsw_fsm_mode_t mode;
    uint32_t obt_s;     /**< On-board time, coarse seconds          */
    uint32_t uptime_s;  /**< Seconds since boot                     */
    uint32_t wdg_kicks; /**< Watchdog refresh count                 */
    obsw_dash_reset_t reset;
    obsw_dash_link_t link;
    obsw_dash_aocs_t aocs;
    bool mag_valid;
    float mag_t[3];  /**< Magnetic field [T]                     */
    bool dipole_valid;
    float dipole[3]; /**< Commanded MTQ dipole [Am^2]            */
} obsw_dash_snapshot_t;

/** Semantic row style; the renderer maps it to colours. */
typedef enum {
    OBSW_DASH_STYLE_NORMAL = 0,
    OBSW_DASH_STYLE_DIM,       /**< Absent / idle data                      */
    OBSW_DASH_STYLE_WARN,      /**< Needs attention                         */
    OBSW_DASH_STYLE_ALARM,     /**< Fault                                   */
    OBSW_DASH_STYLE_TITLE,     /**< Header bar                              */
    OBSW_DASH_STYLE_BAR_OK,    /**< Status bar, NOMINAL                     */
    OBSW_DASH_STYLE_BAR_WARN,  /**< Status bar, STANDBY                     */
    OBSW_DASH_STYLE_BAR_ALARM, /**< Status bar, SAFE                        */
} obsw_dash_style_t;

/** One laid-out screen. Text is space-padded and not NUL-terminated. */
typedef struct {
    char text[OBSW_DASH_ROWS][OBSW_DASH_COLS];
    obsw_dash_style_t style[OBSW_DASH_ROWS];
} obsw_dash_frame_t;

/**
 * Lay out @p snap into @p frame. @p title (NUL-terminated, may be NULL) fills
 * row 0 and is truncated to OBSW_DASH_COLS. Values that do not fit their field
 * are clamped to the largest displayable value; NaN shows as "nan".
 */
void obsw_dash_format(const obsw_dash_snapshot_t *snap,
                      const char *title,
                      obsw_dash_frame_t *frame);

#endif /* OBSW_UI_DASHBOARD_H */
