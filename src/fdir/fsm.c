#include "obsw/fdir/fsm.h"

#include <string.h>

int obsw_fsm_init(obsw_fsm_ctx_t *fsm, const obsw_fsm_config_t *config)
{
    if (!fsm || !config)
        return -1;
    fsm->mode             = OBSW_FSM_NOMINAL;
    fsm->safe_entry_count = 0;
    fsm->config           = *config;
    return 0;
}

void obsw_fsm_to_safe(obsw_fsm_ctx_t *fsm)
{
    if (!fsm || fsm->mode == OBSW_FSM_SAFE)
        return;
    fsm->mode = OBSW_FSM_SAFE;
    fsm->safe_entry_count++;
    if (fsm->config.on_enter_safe)
        fsm->config.on_enter_safe(fsm->config.hook_ctx);
}

void obsw_fsm_to_nominal(obsw_fsm_ctx_t *fsm)
{
    if (!fsm || fsm->mode == OBSW_FSM_NOMINAL)
        return;
    if (fsm->config.on_exit_safe)
        fsm->config.on_exit_safe(fsm->config.hook_ctx);
    fsm->mode = OBSW_FSM_NOMINAL;
}

bool obsw_fsm_tc_allowed(const obsw_fsm_ctx_t *fsm, uint8_t svc, uint8_t subsvc)
{
    if (!fsm || fsm->mode == OBSW_FSM_NOMINAL)
        return true;

    const obsw_fsm_tc_entry_t *wl = fsm->config.safe_tc_whitelist;
    uint8_t len                   = fsm->config.whitelist_len;

    if (!wl || len == 0U)
        return false;

    for (uint8_t i = 0; i < len; i++) {
        if (wl[i].service == svc && wl[i].subservice == subsvc)
            return true;
    }
    return false;
}

obsw_fsm_mode_t obsw_fsm_mode(const obsw_fsm_ctx_t *fsm)
{
    if (!fsm)
        return OBSW_FSM_NOMINAL;
    return fsm->mode;
}

bool obsw_fsm_is_safe(const obsw_fsm_ctx_t *fsm)
{
    if (!fsm)
        return false;
    return fsm->mode == OBSW_FSM_SAFE;
}