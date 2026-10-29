/**
 * @file s19.c
 * @brief PUS-C Service 19 — Event-Action implementation.
 *
 * A fixed table of event ID → action TC. The S5 hook queues actions; the
 * owner of the dispatcher runs them on its next tick.
 */
#include "obsw/pus/s19.h"

#include "obsw/ccsds/space_packet.h"
#include "obsw/util/bytes.h"

#include <string.h>

/* PUS-C TC secondary header: version/ack(1) service(1) subservice(1) source(2) */
#define S19_PUS_SEC_HDR_LEN 5U
#define S19_ACTION_MIN_LEN (OBSW_SP_PRIMARY_HEADER_LEN + S19_PUS_SEC_HDR_LEN)

static obsw_s19_def_t *find_def(obsw_s19_ctx_t *s, uint16_t event_id)
{
    for (uint8_t i = 0; i < s->count; i++) {
        if (s->defs[i].event_id == event_id) {
            return &s->defs[i];
        }
    }
    return NULL;
}

/* An action must be a complete PUS TC packet, and not an S19 TC */
static bool action_valid(const uint8_t *pkt, uint16_t len)
{
    if ((len < S19_ACTION_MIN_LEN) || (len > OBSW_S19_ACTION_MAX_LEN)) {
        return false;
    }
    obsw_sp_packet_t sp;
    if (obsw_sp_parse(pkt, len, &sp) != OBSW_SP_OK) {
        return false;
    }
    return (sp.hdr.type == OBSW_SP_TYPE_TC) && (sp.hdr.sec_hdr_flag != 0U) &&
           (sp.payload_len >= S19_PUS_SEC_HDR_LEN) && (sp.payload[1] != 19U);
}

void obsw_s19_init(obsw_s19_ctx_t *ctx, obsw_s1_ctx_t *s1, obsw_s19_def_t *defs,
                   uint8_t capacity)
{
    if (ctx == NULL) {
        return;
    }
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->s1       = s1;
    ctx->defs     = defs;
    ctx->capacity = (defs != NULL) ? capacity : 0U;
    ctx->enabled  = true;
}

/* ------------------------------------------------------------------ */
/* TC handlers                                                         */
/* ------------------------------------------------------------------ */

int obsw_s19_add(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s19_ctx_t *s = (obsw_s19_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    if ((tc->user_data == NULL) || (tc->user_data_len < 2U) ||
        !action_valid(tc->user_data + 2, (uint16_t)(tc->user_data_len - 2U))) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    uint16_t event_id = obsw_be16(tc->user_data);
    obsw_s19_def_t *d = find_def(s, event_id);
    if ((d == NULL) && (s->count >= s->capacity)) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    if (d == NULL) {
        d = &s->defs[s->count];
        s->count++;
    }
    d->event_id   = event_id;
    d->action_len = (uint16_t)(tc->user_data_len - 2U);
    (void)memcpy(d->action, tc->user_data + 2, d->action_len);
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s19_delete(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s19_ctx_t *s = (obsw_s19_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    obsw_s19_def_t *d = NULL;
    if ((tc->user_data != NULL) && (tc->user_data_len >= 2U)) {
        d = find_def(s, obsw_be16(tc->user_data));
    }
    if (d == NULL) {
        (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    (void)obsw_s1_accept_success(s->s1, tc);
    /* Keep the table packed: move the last definition into the hole */
    s->count--;
    *d = s->defs[s->count];
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s19_delete_all(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s19_ctx_t *s = (obsw_s19_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->count = 0U;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s19_enable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s19_ctx_t *s = (obsw_s19_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->enabled = true;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

int obsw_s19_disable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)&respond; /* unused: handlers report through S1 */
    obsw_s19_ctx_t *s = (obsw_s19_ctx_t *)ctx;
    if ((s == NULL) || (tc == NULL)) {
        return -1;
    }
    (void)obsw_s1_accept_success(s->s1, tc);
    s->enabled = false;
    s->queued  = 0U;
    (void)obsw_s1_completion_success(s->s1, tc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Event hook and dispatch                                             */
/* ------------------------------------------------------------------ */

void obsw_s19_on_event(uint16_t event_id, void *ctx)
{
    obsw_s19_ctx_t *s = (obsw_s19_ctx_t *)ctx;
    if ((s == NULL) || !s->enabled || (find_def(s, event_id) == NULL)) {
        return;
    }
    if (s->queued >= OBSW_S19_QUEUE_LEN) {
        s->dropped++;
        return;
    }
    s->queue[s->queued] = event_id;
    s->queued++;
}

uint8_t obsw_s19_run(obsw_s19_ctx_t *ctx, obsw_tc_dispatcher_t *dispatcher)
{
    if ((ctx == NULL) || (dispatcher == NULL)) {
        return 0U;
    }
    /* Take the batch first: actions may raise events that queue more,
     * and those wait for the next call */
    uint16_t batch[OBSW_S19_QUEUE_LEN];
    uint8_t n = ctx->queued;
    (void)memcpy(batch, ctx->queue, (size_t)n * sizeof(batch[0]));
    ctx->queued = 0U;

    uint8_t run = 0U;
    for (uint8_t i = 0; i < n; i++) {
        const obsw_s19_def_t *d = find_def(ctx, batch[i]);
        if (d != NULL) {
            (void)obsw_tc_dispatcher_feed(dispatcher, d->action, d->action_len);
            run++;
        }
    }
    return run;
}
