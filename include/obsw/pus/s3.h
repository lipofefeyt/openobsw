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
 *   TC(3,1)  Create a HK parameter report structure  → obsw_s3_define()
 *   TC(3,3)  Delete HK parameter report structures   → obsw_s3_delete()
 *   TC(3,5)  Enable periodic HK report generation    → obsw_s3_enable()
 *   TC(3,6)  Disable periodic HK report generation   → obsw_s3_disable()
 *
 * TC(3,1) builds a set at run time from S20 parameter IDs. It needs free
 * set slots (set_capacity > set_count) and a parameter pool in the
 * context; without them it is rejected. Defined sets persist until reset.
 *
 * Reports emitted:
 *   TM(3,25) Housekeeping parameter report
 */
#ifndef OBSW_PUS_S3_H
#define OBSW_PUS_S3_H

#include "obsw/pus/pus_tm.h"
#include "obsw/pus/s1.h"
#include "obsw/pus/s20.h"
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
     *
     * @c ptr points to a native-endian variable of @c size bytes (U32 also
     * covers int32_t and float). TM(3,25) carries it big-endian.
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
        uint8_t set_count;   /**< Sets in use, sets[0..set_count)    */

        /* TC(3,1) — all optional; zero/NULL rejects every TC(3,1) */
        uint8_t set_capacity;          /**< Length of sets[] (>= set_count) */
        obsw_s3_param_t *param_pool;   /**< Storage for defined sets' params */
        uint16_t param_pool_len;
        uint16_t param_pool_used;
        const obsw_s20_param_t *param_table; /**< IDs TC(3,1) may reference */
        uint8_t param_table_len;
    } obsw_s3_ctx_t;

    /* ------------------------------------------------------------------ */
    /* TC handlers                                                         */
    /* ------------------------------------------------------------------ */

    /**
     * TC(3,1) — create a housekeeping parameter report structure.
     * User data field: [set_id (1)] [interval_ticks (4 BE)] [N (1)]
     *                  [param_id (2 BE)] x N
     * Each param_id must be in param_table; it is reported as its 32-bit
     * S20 value. The set is created disabled (enable with TC(3,5)).
     * TM(1,2) 0x0005 if malformed, the set_id exists, a param_id is
     * unknown or the report would not fit one TM packet; 0x0004 if no
     * set slot or pool space is left.
     */
    int obsw_s3_define(const obsw_tc_t *tc,
                       obsw_tc_responder_t respond,
                       void *ctx);

    /**
     * TC(3,3) — delete housekeeping sets created by TC(3,1).
     * User data field: [N (1)] [set_id (1)] x N
     * All-or-nothing: TM(1,2) 0x0005, with no set deleted, if the data is
     * malformed or any set_id is unknown, repeated, a boot-time set (not
     * created by TC(3,1)) or still enabled. Freed set slots and parameter
     * pool space are reusable by later TC(3,1).
     */
    int obsw_s3_delete(const obsw_tc_t *tc,
                       obsw_tc_responder_t respond,
                       void *ctx);

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
    /* Layout binding                                                      */
    /* ------------------------------------------------------------------ */

    /**
     * Take a boot-time set's parameter widths from the SRDB, so its TM(3,25)
     * layout matches the generated XTCE. Pass SRDB_HK_<SET>_SIZES and
     * SRDB_HK_<SET>_PARAM_COUNT; the caller supplies only the pointers, in
     * SRDB parameter order, to variables of those widths.
     *
     * @return false, leaving the set unchanged, if the counts differ or a
     *         size is not 1, 2 or 4.
     */
    bool obsw_s3_bind_sizes(obsw_s3_set_t *set, const uint8_t *sizes, uint8_t count);

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
