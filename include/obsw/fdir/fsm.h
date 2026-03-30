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
 * Recovery to NOMINAL is ground-commanded only:
 *   - obsw_fsm_recover()          TC handler, registered by the application
 *
 * In SAFE mode, incoming TCs are filtered against a static whitelist.
 * The dispatcher calls obsw_fsm_tc_allowed() before routing.
 *
 * Entry/exit hooks are mission-defined function pointers — e.g. disable
 * payload, switch to safe beacon, enable watchdog.
 */
#ifndef OBSW_FDIR_FSM_H
#define OBSW_FDIR_FSM_H

#include "obsw/pus/s1.h"
#include "obsw/tc/dispatcher.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ------------------------------------------------------------------ */
    /* Modes                                                               */
    /* ------------------------------------------------------------------ */

    typedef enum
    {
        OBSW_FSM_NOMINAL = 0,
        OBSW_FSM_SAFE = 1,
    } obsw_fsm_mode_t;

    /* ------------------------------------------------------------------ */
    /* TC whitelist entry                                                  */
    /* ------------------------------------------------------------------ */

    /** A single allowed TC in SAFE mode. */
    typedef struct
    {
        uint8_t service;
        uint8_t subservice;
    } obsw_fsm_tc_entry_t;

    /* ------------------------------------------------------------------ */
    /* Configuration                                                       */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        /** Called on entry to SAFE mode. May be NULL. */
        void (*on_enter_safe)(void *ctx);

        /** Called on exit from SAFE mode (return to NOMINAL). May be NULL. */
        void (*on_exit_safe)(void *ctx);

        /** Passed through to both hooks. */
        void *hook_ctx;

        /**
         * Static whitelist of TCs allowed in SAFE mode.
         * All other TCs are rejected with TM(1,2).
         * If NULL, all TCs are rejected in SAFE mode.
         */
        const obsw_fsm_tc_entry_t *safe_tc_whitelist;
        uint8_t whitelist_len;
    } obsw_fsm_config_t;

    /* ------------------------------------------------------------------ */
    /* Context                                                             */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        obsw_fsm_mode_t mode;
        obsw_fsm_config_t config;
        uint32_t safe_entry_count; /**< Total SAFE transitions since boot */
    } obsw_fsm_ctx_t;

    /* ------------------------------------------------------------------ */
    /* Lifecycle                                                           */
    /* ------------------------------------------------------------------ */

    /**
     * Initialise the FSM in NOMINAL mode.
     */
    int obsw_fsm_init(obsw_fsm_ctx_t *fsm, const obsw_fsm_config_t *config);

    /* ------------------------------------------------------------------ */
    /* Transitions                                                         */
    /* ------------------------------------------------------------------ */

    /**
     * Transition to SAFE mode.
     * No-op if already in SAFE. Fires on_enter_safe hook.
     * Called by watchdog, trap handlers, or S5 high-severity events.
     */
    void obsw_fsm_to_safe(obsw_fsm_ctx_t *fsm);

    /**
     * TC handler — recover to NOMINAL mode.
     * Register in the routing table at a mission-defined APID/svc/subsvc.
     * Emits TM(1,1) acceptance and TM(1,7) completion via S1 context in ctx.
     * No-op if already NOMINAL.
     *
     * ctx must point to an obsw_fsm_recover_ctx_t.
     */
    int obsw_fsm_recover(const obsw_tc_t *tc,
                         obsw_tc_responder_t respond,
                         void *ctx);

    /* ------------------------------------------------------------------ */
    /* TC policy                                                           */
    /* ------------------------------------------------------------------ */

    /**
     * Returns true if the given TC is allowed in the current mode.
     * In NOMINAL mode, always returns true.
     * In SAFE mode, checks the whitelist.
     */
    bool obsw_fsm_tc_allowed(const obsw_fsm_ctx_t *fsm,
                             uint8_t svc, uint8_t subsvc);

    /* ------------------------------------------------------------------ */
    /* Accessors                                                           */
    /* ------------------------------------------------------------------ */

    /** Current operating mode. */
    obsw_fsm_mode_t obsw_fsm_mode(const obsw_fsm_ctx_t *fsm);

    /** True if currently in SAFE mode. */
    bool obsw_fsm_is_safe(const obsw_fsm_ctx_t *fsm);

    /* ------------------------------------------------------------------ */
    /* Recovery TC context                                                 */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        obsw_fsm_ctx_t *fsm;
        obsw_s1_ctx_t *s1;
    } obsw_fsm_recover_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* OBSW_FDIR_FSM_H */