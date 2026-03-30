/**
 * @file s3.h
 * @brief PUS-C Service 3 — Housekeeping.
 *
 * Manages static housekeeping parameter sets. Each set is a collection
 * of pointers to live application variables. The application calls
 * obsw_s3_tick() every control cycle; the service decrements collection
 * interval countdowns and emits TM(3,25) reports automatically.
 *
 * TC handlers registered in the dispatcher routing table:
 *   TC(3,5)  Enable periodic HK report generation    → obsw_s3_enable()
 *   TC(3,6)  Disable periodic HK report generation   → obsw_s3_disable()
 *
 * Reports emitted:
 *   TM(3,25) Housekeeping parameter report
 */
#ifndef OBSW_PUS_S3_H
#define OBSW_PUS_S3_H

#include "obsw/pus/pus_tm.h"
#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ------------------------------------------------------------------ */
    /* Parameter descriptor                                                */
    /* ------------------------------------------------------------------ */

    /** Supported parameter sizes (bytes). */
    typedef enum
    {
        OBSW_S3_PARAM_U8 = 1,
        OBSW_S3_PARAM_U16 = 2,
        OBSW_S3_PARAM_U32 = 4,
    } obsw_s3_param_size_t;

    /**
     * A single housekeeping parameter — a non-owning pointer into the
     * application's live state, plus its width.
     */
    typedef struct
    {
        const void *ptr; /**< Points to the live application variable */
        obsw_s3_param_size_t size;
    } obsw_s3_param_t;

    /* ------------------------------------------------------------------ */
    /* Housekeeping set                                                    */
    /* ------------------------------------------------------------------ */

    /**
     * A housekeeping set — a named collection of parameters with a
     * periodic collection interval.
     *
     * All arrays are caller-owned static memory; the set holds pointers only.
     */
    typedef struct
    {
        uint8_t set_id;          /**< HK structure ID (SID)             */
        obsw_s3_param_t *params; /**< Caller-owned parameter array       */
        uint8_t param_count;
        uint32_t interval_ticks; /**< Collection period in ticks         */
        uint32_t countdown;      /**< Ticks remaining until next report  */
        bool enabled;
    } obsw_s3_set_t;

    /* ------------------------------------------------------------------ */
    /* Context                                                             */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        obsw_tm_store_t *tm_store;
        obsw_s1_ctx_t *s1; /**< For TC verification reports       */
        uint16_t apid;
        uint16_t msg_counter;
        uint32_t timestamp;
        obsw_s3_set_t *sets; /**< Caller-owned static set array      */
        uint8_t set_count;
    } obsw_s3_ctx_t;

    /* ------------------------------------------------------------------ */
    /* TC handlers                                                         */
    /* ------------------------------------------------------------------ */

    /**
     * TC(3,5) — enable periodic HK report generation for a set.
     * User data field: [set_id (1 byte)] [interval_ticks (4 bytes BE)]
     */
    int obsw_s3_enable(const obsw_tc_t *tc,
                       obsw_tc_responder_t respond,
                       void *ctx);

    /**
     * TC(3,6) — disable periodic HK report generation for a set.
     * User data field: [set_id (1 byte)]
     */
    int obsw_s3_disable(const obsw_tc_t *tc,
                        obsw_tc_responder_t respond,
                        void *ctx);

    /* ------------------------------------------------------------------ */
    /* Periodic tick                                                       */
    /* ------------------------------------------------------------------ */

    /**
     * Advance all HK set countdowns by one tick.
     * Emits TM(3,25) for any set whose countdown reaches zero.
     * Call once per control cycle.
     */
    void obsw_s3_tick(obsw_s3_ctx_t *ctx);

    /* ------------------------------------------------------------------ */
    /* Direct report                                                       */
    /* ------------------------------------------------------------------ */

    /**
     * Immediately emit TM(3,25) for the set identified by @p set_id.
     * Useful for on-demand reports outside the periodic schedule.
     */
    int obsw_s3_report(obsw_s3_ctx_t *ctx, uint8_t set_id);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S3_H */