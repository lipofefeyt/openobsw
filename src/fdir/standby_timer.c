/**
 * @file standby_timer.c
 * @brief LEOP STANDBY → SAFE autonomy timer. Implements OBSW-SRS-187, 188.
 */

#include "obsw/fdir/standby_timer.h"

#include <stddef.h>

void obsw_standby_timer_reset(obsw_standby_timer_t *t)
{
    if (t != NULL) {
        t->elapsed_s = 0U;
    }
}

bool obsw_standby_timer_tick(obsw_standby_timer_t *t,
                             obsw_fsm_mode_t mode,
                             uint32_t timeout_s,
                             bool ground_contact)
{
    bool expired = false;

    if (t == NULL) {
        return false;
    }
    if ((mode != OBSW_FSM_STANDBY) || ground_contact) {
        t->elapsed_s = 0U;
    } else {
        if (t->elapsed_s < UINT32_MAX) {
            t->elapsed_s++;
        }
        expired = (t->elapsed_s >= timeout_s);
    }
    return expired;
}
