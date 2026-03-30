/**
 * @file s1.h
 * @brief PUS-C Service 1 — TC Verification.
 *
 * Emits acceptance, start, progress and completion reports for incoming
 * telecommands. Every other service calls S1 to acknowledge its TCs.
 *
 * Reports emitted by this service:
 *   TM(1,1)  Acceptance success
 *   TM(1,2)  Acceptance failure
 *   TM(1,3)  Start of execution success
 *   TM(1,4)  Start of execution failure
 *   TM(1,7)  Completion of execution success
 *   TM(1,8)  Completion of execution failure
 */
#ifndef OBSW_PUS_S1_H
#define OBSW_PUS_S1_H

#include "obsw/pus/pus_tm.h"
#include "obsw/tc/dispatcher.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------ */
/* Failure codes (ECSS-E-ST-70-41C §5.3.4)                            */
/* ------------------------------------------------------------------ */
#define OBSW_S1_FAIL_ILLEGAL_APID 0x0001U
#define OBSW_S1_FAIL_ILLEGAL_SVC 0x0002U
#define OBSW_S1_FAIL_ILLEGAL_SUBSVC 0x0003U
#define OBSW_S1_FAIL_EXEC_ERROR 0x0004U

    /* ------------------------------------------------------------------ */
    /* Context                                                             */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        obsw_tm_store_t *tm_store;
        uint16_t apid;        /**< Source APID for TM(1,x) packets   */
        uint16_t msg_counter; /**< Incremented on each TM emit       */
        uint32_t timestamp;   /**< Mission time — caller updates this */
    } obsw_s1_ctx_t;

    /* ------------------------------------------------------------------ */
    /* Report emitters — called by other services, not by the dispatcher  */
    /* ------------------------------------------------------------------ */

    /** TM(1,1) — acceptance success. */
    int obsw_s1_accept_success(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc);

    /** TM(1,2) — acceptance failure. */
    int obsw_s1_accept_failure(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc,
                               uint16_t failure_code);

    /** TM(1,7) — completion success. */
    int obsw_s1_completion_success(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc);

    /** TM(1,8) — completion failure. */
    int obsw_s1_completion_failure(obsw_s1_ctx_t *ctx, const obsw_tc_t *tc,
                                   uint16_t failure_code);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S1_H */