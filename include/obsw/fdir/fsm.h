/**
 * @file fsm.h
 * @brief FDIR Safe Mode FSM.
 *
 * Two-state mode machine: NOMINAL ↔ SAFE.
 *
 * Transitions into SAFE are triggered by:
 *   - obsw_fsm_to_safe()          called directly (watchdog, trap handler)
 *   - obsw_s5_report() HIGH       for configured safe-trigger event IDs
 *
 * Recovery to NOMINAL is ground-commanded via S8 function ID
 * OBSW_S8_FN_RECOVER_NOMINAL (TC(8,1) with function_id=1).
 *
 * In SAFE mode, incoming TCs are filtered against a static whitelist.
 * The dispatcher calls obsw_fsm_tc_allowed() before routing.
 */
#ifndef OBSW_FDIR_FSM_H
#define OBSW_FDIR_FSM_H

#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    OBSW_FSM_NOMINAL = 0,
    OBSW_FSM_SAFE    = 1,
} obsw_fsm_mode_t;

/* ------------------------------------------------------------------ */
/* TC whitelist entry                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t service;
    uint8_t subservice;
} obsw_fsm_tc_entry_t;

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    void (*on_enter_safe)(void *ctx);
    void (*on_exit_safe)(void *ctx);
    void *hook_ctx;

    const obsw_fsm_tc_entry_t *safe_tc_whitelist;
    uint8_t whitelist_len;
} obsw_fsm_config_t;

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    obsw_fsm_mode_t mode;
    obsw_fsm_config_t config;
    uint32_t safe_entry_count;
} obsw_fsm_ctx_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int obsw_fsm_init(obsw_fsm_ctx_t *fsm, const obsw_fsm_config_t *config);

/* ------------------------------------------------------------------ */
/* Transitions                                                         */
/* ------------------------------------------------------------------ */

/**
 * Transition to SAFE mode.
 * No-op if already SAFE. Fires on_enter_safe hook.
 */
void obsw_fsm_to_safe(obsw_fsm_ctx_t *fsm);

/**
 * Transition to NOMINAL mode.
 * No-op if already NOMINAL. Fires on_exit_safe hook.
 * Called by S8 function ID OBSW_S8_FN_RECOVER_NOMINAL.
 */
void obsw_fsm_to_nominal(obsw_fsm_ctx_t *fsm);

/* ------------------------------------------------------------------ */
/* TC policy                                                           */
/* ------------------------------------------------------------------ */

bool obsw_fsm_tc_allowed(const obsw_fsm_ctx_t *fsm, uint8_t svc, uint8_t subsvc);

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

obsw_fsm_mode_t obsw_fsm_mode(const obsw_fsm_ctx_t *fsm);
bool obsw_fsm_is_safe(const obsw_fsm_ctx_t *fsm);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_FDIR_FSM_H */