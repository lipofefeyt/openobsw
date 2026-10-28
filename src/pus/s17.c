/**
 * @file s17.c
 * @brief PUS-C Service 17 — Are-You-Alive implementation.
 *
 * Handles TC(17,1) ping: emits TM(1,1) acceptance, TM(17,2) pong,
 * and TM(1,7) completion. Used as a heartbeat by SVF and YAMCS.
 */
#include "obsw/pus/s17.h"

int obsw_s17_ping(const obsw_tc_t *tc,
                  obsw_tc_responder_t respond,
                  void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s17_ctx_t *s = (obsw_s17_ctx_t *)ctx;
    if (!s || !tc) {
        return -1;
    }

    /* TM(1,1) — acceptance success */
    (void)obsw_s1_accept_success(s->s1, tc);

    /* TM(17,2) — are-you-alive response */
    uint16_t seq = s->msg_counter;
    s->msg_counter++;
    (void)obsw_pus_tm_build(s->tm_store, s->apid, seq,
                      17, 2, seq, 0, s->timestamp,
                      NULL, 0);

    /* TM(1,7) — completion success */
    (void)obsw_s1_completion_success(s->s1, tc);

    return 0;
}
