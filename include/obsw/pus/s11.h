/**
 * @file s11.h
 * @brief PUS-C Service 11 — Time-Based Scheduling.
 *
 * Ground uplinks TCs tagged with an on-board time. They are held in a
 * fixed table and released by obsw_s11_run() once OBT reaches the tag,
 * through the normal TC dispatcher: a released TC gets the same
 * acceptance checks, SAFE/STANDBY gate and S1 reports as an uplinked TC,
 * judged at release time.
 *
 * TC handlers registered in the dispatcher routing table:
 *   TC(11,4)   Insert a time-tagged activity   → obsw_s11_insert()
 *   TC(11,5)   Delete an activity              → obsw_s11_delete()
 *   TC(11,6)   Delete all activities           → obsw_s11_delete_all()
 *   TC(11,17)  Enable the schedule             → obsw_s11_enable()
 *   TC(11,18)  Disable the schedule            → obsw_s11_disable()
 *
 * Subtype numbers and the TC(11,4) layout follow opensvf's PusService11
 * codec. As there, activities get request IDs 1, 2, 3, ... in insertion
 * order since boot; ground deletes an activity by that ID.
 */
#ifndef OBSW_PUS_S11_H
#define OBSW_PUS_S11_H

#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"
#include "obsw/time/obt.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* TC user data layouts                                                */
/*                                                                     */
/* TC(11,4) insert: coarse(4) | fine(2) | TC space packet (11..64 B)  */
/*                  time tag is CUC-4,2, big-endian                    */
/* TC(11,5) delete: request_id(2)                                      */
/* ------------------------------------------------------------------ */

#define OBSW_S11_TAG_LEN 6U

#ifndef OBSW_S11_TC_MAX_LEN
#define OBSW_S11_TC_MAX_LEN 64U /**< Largest scheduled TC packet */
#endif

/** One scheduled activity. */
typedef struct {
    uint16_t request_id;
    uint32_t coarse; /**< Release time, CUC-4,2 */
    uint16_t fine;
    uint16_t tc_len;
    uint8_t tc[OBSW_S11_TC_MAX_LEN];
} obsw_s11_activity_t;

typedef struct {
    obsw_s1_ctx_t *s1;
    obsw_s11_activity_t *acts; /**< Caller-owned activity storage, unordered */
    uint8_t capacity;
    uint8_t count;
    uint16_t next_id;
    bool enabled;
} obsw_s11_ctx_t;

/** Initialise with an empty, enabled schedule; the first ID is 1. */
void obsw_s11_init(obsw_s11_ctx_t *ctx, obsw_s1_ctx_t *s1, obsw_s11_activity_t *acts,
                   uint8_t capacity);

/**
 * TC(11,4) — insert an activity. Rejected with TM(1,2) 0x0005 if the
 * embedded TC is not a PUS TC packet of up to OBSW_S11_TC_MAX_LEN bytes or
 * is itself an S11 TC; 0x0004 when the schedule is full. A tag already in
 * the past is accepted and released on the next run.
 */
int obsw_s11_insert(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(11,5) — delete the activity with a request ID (0x0005 if none). */
int obsw_s11_delete(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(11,6) — delete all activities. */
int obsw_s11_delete_all(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(11,17) — enable the schedule; overdue activities run on the next run. */
int obsw_s11_enable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(11,18) — disable the schedule; activities are held, not dropped. */
int obsw_s11_disable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/**
 * Release every activity whose time tag is at or before @p now, earliest
 * first (ties in insertion order), removing each before dispatching it.
 * Does nothing while the schedule is disabled. Call once per tick.
 * @return Number of activities released.
 */
uint8_t obsw_s11_run(obsw_s11_ctx_t *ctx, const obsw_obt_t *now,
                     obsw_tc_dispatcher_t *dispatcher);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S11_H */
