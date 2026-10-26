/**
 * @file s9.h
 * @brief PUS-C Service 9 — Time Management.
 *
 * TC handler registered in the dispatcher routing table:
 *   TC(9,128)  Set on-board time  → obsw_s9_set_obt()
 *
 * App data: 4-byte coarse seconds (BE) + 2-byte fine fraction (BE),
 * 1/65536 s resolution — CUC-4,2, matching opensvf's PusService9 codec.
 * No TM(9,x) report is emitted; TM(1,1)/TM(1,7) (or TM(1,2) on a malformed
 * request) are the only telemetry. The resulting OBT is externally visible
 * via the `obc_obt` HK parameter (S3 set DHS_OBC_HK).
 */
#ifndef OBSW_PUS_S9_H
#define OBSW_PUS_S9_H

#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"
#include "obsw/time/obt.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    obsw_s1_ctx_t *s1; /**< For TC(9,128) acceptance/completion reports */
    obsw_obt_t *obt;   /**< OBT owned by the caller's tick loop         */
} obsw_s9_ctx_t;

/* ------------------------------------------------------------------ */
/* TC handler                                                          */
/* ------------------------------------------------------------------ */

/**
 * TC(9,128) — set on-board time.
 * Emits TM(1,1) + TM(1,7) on success, TM(1,2) if app data is malformed.
 */
int obsw_s9_set_obt(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S9_H */
