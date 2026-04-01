#include "obsw/pus/s8.h"

/* TC(8,1) user data: function_id(2 BE) | args_len(1) | args(...) */
#define S8_MIN_LEN 3U

int obsw_s8_perform(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)respond;
    obsw_s8_ctx_t *s = (obsw_s8_ctx_t *)ctx;
    if (!s || !tc)
        return -1;

    if (!tc->user_data || tc->user_data_len < S8_MIN_LEN) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_SUBSVC);
        return -1;
    }

    uint16_t fn_id      = (uint16_t)(((uint16_t)tc->user_data[0] << 8) | tc->user_data[1]);
    uint8_t args_len    = tc->user_data[2];
    const uint8_t *args = (args_len > 0U) ? tc->user_data + 3U : NULL;

    /* Lookup */
    obsw_s8_entry_t *entry = NULL;
    for (uint8_t i = 0; i < s->table_len; i++) {
        if (s->table[i].function_id == fn_id) {
            entry = &s->table[i];
            break;
        }
    }

    if (!entry) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    obsw_s1_accept_success(s->s1, tc);

    int rc = entry->fn(args, args_len, entry->ctx);
    if (rc == 0) {
        obsw_s1_completion_success(s->s1, tc);
    } else {
        obsw_s1_completion_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }
    return 0;
}