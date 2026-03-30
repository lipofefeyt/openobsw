/**
 * @file s17.h
 * @brief PUS-C Service 17 — Test (are-you-alive).
 *
 * TC handler registered in the dispatcher routing table:
 *   TC(17,1)  Are-you-alive connection test  → obsw_s17_ping()
 *
 * Reports emitted:
 *   TM(17,2)  Are-you-alive connection test response
 */
#ifndef OBSW_PUS_S17_H
#define OBSW_PUS_S17_H

#include "obsw/pus/pus_tm.h"
#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ------------------------------------------------------------------ */
    /* Context                                                             */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        obsw_tm_store_t *tm_store;
        obsw_s1_ctx_t *s1; /**< For TC(17,1) acceptance report    */
        uint16_t apid;
        uint16_t msg_counter;
        uint32_t timestamp;
    } obsw_s17_ctx_t;

    /* ------------------------------------------------------------------ */
    /* TC handler                                                          */
    /* ------------------------------------------------------------------ */

    /**
     * TC(17,1) — are-you-alive connection test.
     * Emits TM(1,1) acceptance, then TM(17,2) response, then TM(1,7) completion.
     */
    int obsw_s17_ping(const obsw_tc_t *tc,
                      obsw_tc_responder_t respond,
                      void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S17_H */