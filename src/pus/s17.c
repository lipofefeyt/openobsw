#include "obsw/pus/s17.h"

int obsw_s17_ping(const obsw_tc_t *tc,
                  obsw_tc_responder_t respond,
                  void *ctx)
{
    (void)respond;
    obsw_s17_ctx_t *s = (obsw_s17_ctx_t *)ctx;
    if (!s || !tc)
        return -1;

    /* TM(1,1) — acceptance success */
    obsw_s1_accept_success(s->s1, tc);

    /* TM(17,2) — are-you-alive response */
    uint16_t seq = s->msg_counter++;
    obsw_pus_tm_build(s->tm_store, s->apid, seq,
                      17, 2, seq, 0, s->timestamp,
                      NULL, 0);

    /* TM(1,7) — completion success */
    obsw_s1_completion_success(s->s1, tc);

    return 0;
}