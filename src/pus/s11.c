/**
 * @file s11.c
 * @brief PUS-C Service 11 — Time-Based Scheduling implementation.
 *
 * A fixed, unordered table of time-tagged TCs. obsw_s11_run() releases the
 * due ones earliest first through the TC dispatcher.
 */
#include "obsw/pus/s11.h"

#include "obsw/util/bytes.h"

#include <string.h>

/* CUC-4,2 as one comparable 48-bit value */
static uint64_t tag_key(uint32_t coarse, uint16_t fine)
{
    return ((uint64_t)coarse << 16) | (uint64_t)fine;
}

static obsw_s11_activity_t *find_act(obsw_s11_ctx_t *s, uint16_t request_id)
{
    for (uint8_t i = 0; i < s->count; i++) {
        if (s->acts[i].request_id == request_id) {
            return &s->acts[i];
        }
    }
    return NULL;
}

/* Next request ID: never 0, never one still in the table */
static uint16_t take_id(obsw_s11_ctx_t *s)
{
    uint16_t id = s->next_id;
    while ((id == 0U) || (find_act(s, id) != NULL)) {
        id++;
    }
    s->next_id = (uint16_t)(id + 1U);
    return id;
}

/* Remove by moving the last activity into the hole */
static void remove_at(obsw_s11_ctx_t *s, obsw_s11_activity_t *a)
{
    s->count--;
    *a = s->acts[s->count];
}

void obsw_s11_init(obsw_s11_ctx_t *ctx, obsw_s1_ctx_t *s1, obsw_s11_activity_t *acts,
                   uint8_t capacity)
{
    if (ctx == NULL) {
        return;
    }
    ctx->s1       = s1;
    ctx->acts     = acts;
    ctx->capacity = (acts != NULL) ? capacity : 0U;
    ctx->count    = 0U;
    ctx->next_id  = 1U;
    ctx->enabled  = true;
}

/* ------------------------------------------------------------------ */
/* TC handlers                                                         */
/* ------------------------------------------------------------------ */

int obsw_s11_insert(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s11_ctx_t *s = (obsw_s11_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }

    uint8_t svc = 0U;
    bool ok     = (tc->user_data != NULL) && (tc->user_data_len > OBSW_S11_TAG_LEN);
    uint16_t tc_len = ok ? (uint16_t)(tc->user_data_len - OBSW_S11_TAG_LEN) : 0U;
    ok = ok && (tc_len <= OBSW_S11_TC_MAX_LEN) &&
         (obsw_tc_packet_check(tc->user_data + OBSW_S11_TAG_LEN, tc_len, &svc) == OBSW_TC_OK) &&
         (svc != 11U);
    if (!ok) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }
    if (s->count >= s->capacity) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    obsw_s11_activity_t *a = &s->acts[s->count];
    a->request_id          = take_id(s);
    a->coarse              = obsw_be32(tc->user_data);
    a->fine                = obsw_be16(tc->user_data + 4);
    a->tc_len              = tc_len;
    (void)memcpy(a->tc, tc->user_data + OBSW_S11_TAG_LEN, tc_len);
    s->count++;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s11_delete(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s11_ctx_t *s = (obsw_s11_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    obsw_s11_activity_t *a = NULL;
    if ((tc->user_data != NULL) && (tc->user_data_len >= 2U)) {
        a = find_act(s, obsw_be16(tc->user_data));
    }
    if (a == NULL) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    remove_at(s, a);
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s11_delete_all(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s11_ctx_t *s = (obsw_s11_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->count = 0U;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s11_enable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s11_ctx_t *s = (obsw_s11_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->enabled = true;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s11_disable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s11_ctx_t *s = (obsw_s11_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->enabled = false;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Release                                                             */
/* ------------------------------------------------------------------ */

uint8_t obsw_s11_run(obsw_s11_ctx_t *ctx, const obsw_obt_t *now,
                     obsw_tc_dispatcher_t *dispatcher)
{
    if ((ctx == NULL) || (now == NULL) || (dispatcher == NULL) || !ctx->enabled) {
        return 0U;
    }
    uint64_t t_now = tag_key(now->coarse, now->fine);
    uint8_t released = 0U;
    uint8_t limit    = ctx->count; /* each activity is released at most once */

    while (released < limit) {
        /* Earliest due activity; equal tags go in insertion (ID) order */
        obsw_s11_activity_t *next = NULL;
        for (uint8_t i = 0; i < ctx->count; i++) {
            obsw_s11_activity_t *a = &ctx->acts[i];
            uint64_t t = tag_key(a->coarse, a->fine);
            if (t > t_now) {
                continue;
            }
            if ((next == NULL) || (t < tag_key(next->coarse, next->fine)) ||
                ((t == tag_key(next->coarse, next->fine)) && (a->request_id < next->request_id))) {
                next = a;
            }
        }
        if (next == NULL) {
            break;
        }
        /* Copy out and remove first, so the released TC sees a consistent table */
        uint8_t pkt[OBSW_S11_TC_MAX_LEN];
        uint16_t len = next->tc_len;
        (void)memcpy(pkt, next->tc, len);
        remove_at(ctx, next);
        (void)obsw_tc_dispatcher_feed(dispatcher, pkt, len);
        released++;
    }
    return released;
}
