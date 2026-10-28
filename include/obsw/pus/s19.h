/**
 * @file s19.h
 * @brief PUS-C Service 19 — Event-Action.
 *
 * Ground links an S5 event ID to a stored action TC. When that event is
 * raised, S19 queues the action; obsw_s19_run() later feeds queued actions
 * through the normal TC dispatcher, so they get the same acceptance checks
 * (including the SAFE/STANDBY gate) and S1 reports as an uplinked TC.
 * Dispatch is deferred so an event raised inside a TC handler never
 * re-enters the dispatcher.
 *
 * TC handlers registered in the dispatcher routing table:
 *   TC(19,1)  Add event-action definition       → obsw_s19_add()
 *   TC(19,2)  Delete event-action definition    → obsw_s19_delete()
 *   TC(19,3)  Delete all definitions            → obsw_s19_delete_all()
 *   TC(19,4)  Enable event-action               → obsw_s19_enable()
 *   TC(19,5)  Disable event-action              → obsw_s19_disable()
 *
 * Subtype numbers and the TC(19,1) layout follow opensvf's PusService19
 * codec so the same campaigns run against both.
 *
 * Wiring: set the S5 context's on_event = obsw_s19_on_event and
 * on_event_ctx = the S19 context, then call obsw_s19_run() once per tick.
 */
#ifndef OBSW_PUS_S19_H
#define OBSW_PUS_S19_H

#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* TC user data layouts                                                */
/*                                                                     */
/* TC(19,1) add:    event_id(2) | action TC space packet (11..64 B)   */
/* TC(19,2) delete: event_id(2)                                        */
/* ------------------------------------------------------------------ */

#ifndef OBSW_S19_ACTION_MAX_LEN
#define OBSW_S19_ACTION_MAX_LEN 64U /**< Largest stored action TC packet */
#endif

#ifndef OBSW_S19_QUEUE_LEN
#define OBSW_S19_QUEUE_LEN 4U /**< Actions waiting for obsw_s19_run() */
#endif

/** One event-action definition. One per event ID: adding replaces. */
typedef struct {
    uint16_t event_id;
    uint16_t action_len;
    uint8_t action[OBSW_S19_ACTION_MAX_LEN]; /**< Space packet, as uplinked */
} obsw_s19_def_t;

typedef struct {
    obsw_s1_ctx_t *s1;
    obsw_s19_def_t *defs; /**< Caller-owned definition storage */
    uint8_t capacity;
    uint8_t count;
    bool enabled;
    uint16_t queue[OBSW_S19_QUEUE_LEN]; /**< Event IDs awaiting dispatch */
    uint8_t queued;
    uint16_t dropped; /**< Actions lost because the queue was full */
} obsw_s19_ctx_t;

/** Initialise with no definitions, event-action enabled. */
void obsw_s19_init(obsw_s19_ctx_t *ctx, obsw_s1_ctx_t *s1, obsw_s19_def_t *defs,
                   uint8_t capacity);

/**
 * TC(19,1) — add a definition, or replace the one for the same event.
 * Rejected with TM(1,2) 0x0005 if the action is not a TC space packet with
 * a PUS secondary header of 11..OBSW_S19_ACTION_MAX_LEN bytes, or is itself
 * an S19 TC; 0x0004 when the table is full.
 */
int obsw_s19_add(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(19,2) — delete the definition for an event (0x0005 if none). */
int obsw_s19_delete(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(19,3) — delete all definitions. */
int obsw_s19_delete_all(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(19,4) — enable event-action. */
int obsw_s19_enable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(19,5) — disable event-action; definitions are kept, queued actions dropped. */
int obsw_s19_disable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/**
 * S5 event hook (obsw_s5_event_hook_t): queue the action defined for
 * @p event_id, if any and if event-action is enabled. Never dispatches.
 */
void obsw_s19_on_event(uint16_t event_id, void *ctx);

/**
 * Dispatch the actions queued since the last call, in event order. An
 * action whose definition was deleted meanwhile is skipped.
 * @return Number of actions fed to the dispatcher.
 */
uint8_t obsw_s19_run(obsw_s19_ctx_t *ctx, obsw_tc_dispatcher_t *dispatcher);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S19_H */
