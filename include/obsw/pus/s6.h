/**
 * @file s6.h
 * @brief PUS-C Service 6 — Memory Management.
 *
 * Provides ground-commanded memory load, check and dump over TT&C.
 * All operations work on raw memory addresses — address validity is
 * platform-specific and not validated here.
 *
 * TC handlers registered in the dispatcher routing table:
 *   TC(6,2)  Load memory area         → obsw_s6_load()
 *   TC(6,5)  Check memory area        → obsw_s6_check()
 *   TC(6,9)  Dump memory area         → obsw_s6_dump()
 *
 * Reports emitted:
 *   TM(6,6)   Memory dump data
 *   TM(6,10)  Memory check success
 *   TM(6,11)  Memory check failure
 *
 * Dump length is bounded by OBSW_TM_MAX_PACKET_LEN minus headers.
 * Single-packet dumps only; segmented dumps are out of scope for v0.5.
 */
#ifndef OBSW_PUS_S6_H
#define OBSW_PUS_S6_H

#include "obsw/pus/pus_tm.h"
#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* TC user data layouts                                                */
/*                                                                     */
/* TC(6,2) Load:   start_addr(4 BE) | data_len(2 BE) | data(...)      */
/* TC(6,5) Check:  start_addr(4 BE) | length(2 BE) | mem_id(1)        */
/*                 | expected_crc(2 BE)                                */
/* TC(6,9) Dump:   start_addr(4 BE) | length(2 BE) | mem_id(1)        */
/* ------------------------------------------------------------------ */

#define OBSW_S6_LOAD_MIN_LEN 6U /**< addr(4) + data_len(2)        */
#define OBSW_S6_CHECK_LEN 9U    /**< addr(4) + len(2) + id(1) + crc(2) */
#define OBSW_S6_DUMP_LEN 7U     /**< addr(4) + len(2) + id(1)     */

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    obsw_tm_store_t *tm_store;
    obsw_s1_ctx_t *s1;
    uint16_t apid;
    uint16_t msg_counter;
    uint32_t timestamp;
} obsw_s6_ctx_t;

/* ------------------------------------------------------------------ */
/* TC handlers                                                         */
/* ------------------------------------------------------------------ */

/**
 * TC(6,2) — load memory area.
 * Writes the data field of the TC directly to the address specified
 * in the user data field. Issues TM(1,1) and TM(1,7) via S1.
 */
int obsw_s6_load(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/**
 * TC(6,5) — check memory area.
 * Computes CRC-16/CCITT over the specified region and compares with
 * the expected value in the TC. Emits TM(6,10) on match, TM(6,11)
 * with actual CRC on mismatch.
 */
int obsw_s6_check(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

/**
 * TC(6,9) — dump memory area.
 * Reads the specified memory region and emits it as TM(6,6).
 * Returns an error if the requested length exceeds the maximum
 * single-packet dump size.
 */
int obsw_s6_dump(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S6_H */