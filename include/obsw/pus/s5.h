/**
 * @file s5.h
 * @brief PUS-C Service 5 — Event Reporting.
 *
 * Emits event reports at four severity levels. Called directly by
 * application code and the FDIR layer — not via the TC dispatcher.
 *
 * Reports emitted:
 *   TM(5,1)  Informational event
 *   TM(5,2)  Low-severity anomaly
 *   TM(5,3)  Medium-severity anomaly
 *   TM(5,4)  High-severity anomaly / failure
 *
 * FDIR integration:
 *   If fsm is non-NULL and severity is OBSW_S5_HIGH and the event_id
 *   appears in safe_trigger_ids[], obsw_fsm_to_safe() is called
 *   automatically after the TM report is enqueued.
 */
#ifndef OBSW_PUS_S5_H
#define OBSW_PUS_S5_H

#include "obsw/pus/pus_tm.h"
#include <stdint.h>

/* Forward declaration — avoids circular include with fsm.h */
struct obsw_fsm_ctx;

#ifdef __cplusplus
extern "C"
{
#endif

    /* ------------------------------------------------------------------ */
    /* Severity levels                                                     */
    /* ------------------------------------------------------------------ */

    typedef enum
    {
        OBSW_S5_INFO = 1,   /**< TM(5,1) — informational                */
        OBSW_S5_LOW = 2,    /**< TM(5,2) — low-severity anomaly         */
        OBSW_S5_MEDIUM = 3, /**< TM(5,3) — medium-severity anomaly      */
        OBSW_S5_HIGH = 4,   /**< TM(5,4) — high-severity / failure      */
    } obsw_s5_severity_t;

    /* ------------------------------------------------------------------ */
    /* Context                                                             */
    /* ------------------------------------------------------------------ */

#ifndef OBSW_S5_MAX_SAFE_TRIGGERS
#define OBSW_S5_MAX_SAFE_TRIGGERS 8U
#endif

    typedef struct
    {
        obsw_tm_store_t *tm_store;
        uint16_t apid;
        uint16_t msg_counter;
        uint32_t timestamp;

        /**
         * Optional FSM context. If non-NULL, HIGH severity events whose
         * event_id appears in safe_trigger_ids will trigger a safe mode
         * transition automatically.
         */
        struct obsw_fsm_ctx *fsm;
        uint16_t safe_trigger_ids[OBSW_S5_MAX_SAFE_TRIGGERS];
        uint8_t safe_trigger_count;
    } obsw_s5_ctx_t;

    /* ------------------------------------------------------------------ */
    /* API                                                                 */
    /* ------------------------------------------------------------------ */

    /**
     * Emit an event report at the given severity level.
     *
     * @param ctx       Initialised S5 context.
     * @param severity  One of OBSW_S5_INFO .. OBSW_S5_HIGH.
     * @param event_id  Mission-defined event identifier (2 bytes).
     * @param data      Optional auxiliary data (may be NULL).
     * @param data_len  Length of auxiliary data in bytes.
     * @return OBSW_PUS_TM_OK or negative error code.
     */
    int obsw_s5_report(obsw_s5_ctx_t *ctx,
                       obsw_s5_severity_t severity,
                       uint16_t event_id,
                       const uint8_t *data,
                       uint8_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S5_H */