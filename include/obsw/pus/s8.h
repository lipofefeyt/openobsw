/**
 * @file s8.h
 * @brief PUS-C Service 8 — Function Management.
 *
 * Executes named on-board functions by ID. The function table is a
 * static caller-owned array — zero dynamic allocation.
 *
 * This replaces the mission-defined TC(128,1) recover_nominal command.
 * Function ID 1 = recover to NOMINAL mode.
 *
 * TC handler registered in the dispatcher routing table:
 *   TC(8,1)  Perform a function  → obsw_s8_perform()
 *
 * TC(8,1) user data layout:
 *   function_id (2 bytes BE) | args_len (1 byte) | args (args_len bytes)
 */
#ifndef OBSW_PUS_S8_H
#define OBSW_PUS_S8_H

#include "obsw/pus/pus_tm.h"
#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Well-known function IDs                                             */
/* ------------------------------------------------------------------ */

#define OBSW_S8_FN_RECOVER_NOMINAL 1U /**< Recover to NOMINAL mode  */

/* ------------------------------------------------------------------ */
/* Function table                                                      */
/* ------------------------------------------------------------------ */

/**
 * An on-board function implementation.
 *
 * @param args      Optional argument bytes from the TC user data field.
 * @param args_len  Length of args in bytes (may be 0).
 * @param ctx       Function-specific context pointer.
 * @return 0 on success, negative on error.
 */
typedef int (*obsw_s8_fn_t)(const uint8_t *args, uint8_t args_len, void *ctx);

/** Single entry in the static function table. */
typedef struct {
    uint16_t function_id;
    obsw_s8_fn_t fn;
    void *ctx;
} obsw_s8_entry_t;

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    obsw_tm_store_t *tm_store;
    obsw_s1_ctx_t *s1;
    uint16_t apid;
    uint16_t msg_counter;
    uint32_t timestamp;
    obsw_s8_entry_t *table; /**< Caller-owned static function table */
    uint8_t table_len;
} obsw_s8_ctx_t;

/* ------------------------------------------------------------------ */
/* TC handler                                                          */
/* ------------------------------------------------------------------ */

/**
 * TC(8,1) — perform a function.
 * Looks up function_id in the table and calls the registered function.
 * Emits TM(1,1) acceptance, then TM(1,7) or TM(1,8) on completion.
 */
int obsw_s8_perform(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_S8_H */