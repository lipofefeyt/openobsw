/**
 * @file s9.c
 * @brief PUS-C Service 9 — Time Management implementation.
 */
#include "obsw/pus/s9.h"

#include "obsw/util/bytes.h"

/* TC(9,128) user data: coarse(4 BE) | fine(2 BE) */
#define S9_SET_OBT_LEN 6U

int obsw_s9_set_obt(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s9_ctx_t *s = (obsw_s9_ctx_t *)ctx;
    if (!s || !tc) {
        return -1;
    }

    if (!tc->user_data || tc->user_data_len < S9_SET_OBT_LEN) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    uint32_t coarse = obsw_be32(tc->user_data);
    uint16_t fine   = obsw_be16(tc->user_data + 4);

    (void)obsw_s1_accept_success(s->s1, tc);
    obsw_obt_set(s->obt, coarse, fine);
    (void)obsw_s1_completion_success(s->s1, tc);

    return 0;
}
