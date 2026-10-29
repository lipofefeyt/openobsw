/**
 * @file s12.h
 * @brief PUS-C Service 12 — On-Board Monitoring.
 *
 * Ground defines limit checks on S20 parameters. Each control tick,
 * obsw_s12_check() compares every definition's parameter against its low
 * and high limits and raises an S5 event on entry into an out-of-limit
 * state. The event is latched: it is not repeated until the value returns
 * inside the limit and crosses it again.
 *
 * TC handlers registered in the dispatcher routing table:
 *   TC(12,1)  Enable monitoring                 → obsw_s12_enable()
 *   TC(12,2)  Disable monitoring                → obsw_s12_disable()
 *   TC(12,3)  Add or replace a definition       → obsw_s12_add()
 *   TC(12,4)  Delete a definition               → obsw_s12_delete()
 *   TC(12,5)  Delete all definitions            → obsw_s12_delete_all()
 *
 * Subtype numbers and the TC(12,3) layout follow opensvf's PusService12
 * codec so the same campaigns run against both.
 *
 * Reports emitted (through S5):
 *   TM(5,severity)  event_id(2) | value(4, IEEE 754 float, BE)
 */
#ifndef OBSW_PUS_S12_H
#define OBSW_PUS_S12_H

#include "obsw/pus/s1.h"
#include "obsw/pus/s20.h"
#include "obsw/pus/s5.h"
#include "obsw/tc/dispatcher.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* TC user data layouts                                                */
/*                                                                     */
/* TC(12,3) add:    param_id(2) | low(4 f32) | high(4 f32)            */
/*                  | event_low(2) | event_high(2) | severity(1)      */
/*                  all big-endian; a NaN limit disables that check    */
/* TC(12,4) delete: param_id(2)                                        */
/* ------------------------------------------------------------------ */

#define OBSW_S12_ADD_LEN 15U
#define OBSW_S12_DELETE_LEN 2U

/** One monitoring definition. One per parameter: adding replaces. */
typedef struct {
    uint16_t param_id;
    float low;           /**< Out of limit when value < low (NaN: no check)  */
    float high;          /**< Out of limit when value > high (NaN: no check) */
    uint16_t event_low;  /**< S5 event raised on entering the low state      */
    uint16_t event_high; /**< S5 event raised on entering the high state     */
    obsw_s5_severity_t severity;
    bool in_low;         /**< Latched out-of-limit states                     */
    bool in_high;
} obsw_s12_def_t;

typedef struct {
    obsw_s1_ctx_t *s1;
    obsw_s5_ctx_t *s5;                 /**< Events go through S5 (and its FDIR hook) */
    const obsw_s20_param_t *params;    /**< Parameters definitions may reference */
    uint8_t param_count;
    obsw_s12_def_t *defs;              /**< Caller-owned definition storage */
    uint8_t capacity;
    uint8_t count;
    bool enabled;
} obsw_s12_ctx_t;

/**
 * Initialise with no definitions and monitoring enabled.
 * @param defs      Caller-owned array of @p capacity definitions.
 * @param params    S20 table definitions resolve against; read every check.
 */
void obsw_s12_init(obsw_s12_ctx_t *ctx,
                   obsw_s1_ctx_t *s1,
                   obsw_s5_ctx_t *s5,
                   const obsw_s20_param_t *params,
                   uint8_t param_count,
                   obsw_s12_def_t *defs,
                   uint8_t capacity);

/** TC(12,1) — enable monitoring; clears every latched out-of-limit state. */
int obsw_s12_enable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(12,2) — disable monitoring; definitions are kept. */
int obsw_s12_disable(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/**
 * TC(12,3) — add a definition, or replace the one for the same parameter.
 * Rejected with TM(1,2) 0x0005 for malformed data, an unknown parameter,
 * a severity outside 1..4 or low > high; 0x0004 when the table is full.
 */
int obsw_s12_add(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(12,4) — delete the definition for a parameter (0x0005 if none). */
int obsw_s12_delete(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/** TC(12,5) — delete all definitions. */
int obsw_s12_delete_all(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/**
 * Evaluate every definition against its parameter's current value and
 * raise S5 events for new out-of-limit transitions. Call once per control
 * tick. Does nothing while monitoring is disabled.
 */
void obsw_s12_check(obsw_s12_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S12_H */
