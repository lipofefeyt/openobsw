/**
 * @file fsm.h
 * @brief Satellite mode FSM.
 *
 * Three-state mode machine: STANDBY → SAFE ↔ NOMINAL.
 *
 *   STANDBY — boot state; AOCS off; minimal subsystems active.
 *             Auto-transitions to SAFE after a timeout managed by the
 *             Mode Manager task, or on ground command.
 *   SAFE    — B-dot detumbling (MAG+MTQ only); TC whitelist enforced.
 *             Fault-triggered fallback from NOMINAL via FDIR.
 *   NOMINAL — full AOCS (PD quaternion, ST+GYR+RW); all TCs allowed.
 *
 * The Mode Manager task owns the FSM context and drives nominal transitions.
 * FDIR calls obsw_fsm_to_safe() for fault-triggered fallback only.
 * S5 HIGH events with matching trigger IDs call obsw_fsm_to_safe() directly.
 *
 * TC whitelist is enforced in both STANDBY and SAFE; all TCs pass in NOMINAL.
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
    OBSW_FSM_NOMINAL  = 0,
    OBSW_FSM_SAFE     = 1,
    OBSW_FSM_STANDBY  = 2,
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

typedef struct obsw_fsm_ctx {
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

/** Transition to STANDBY. No-op if already STANDBY. */
void obsw_fsm_to_standby(obsw_fsm_ctx_t *fsm);

/** Transition to SAFE. No-op if already SAFE. Fires on_enter_safe hook. */
void obsw_fsm_to_safe(obsw_fsm_ctx_t *fsm);

/**
 * Transition to NOMINAL. No-op if already NOMINAL.
 * Fires on_exit_safe hook only when transitioning from SAFE (not STANDBY).
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