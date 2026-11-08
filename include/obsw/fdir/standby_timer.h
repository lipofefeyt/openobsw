#ifndef OBSW_FDIR_STANDBY_TIMER_H
#define OBSW_FDIR_STANDBY_TIMER_H

/**
 * @file obsw/fdir/standby_timer.h
 * @brief LEOP STANDBY → SAFE autonomy timer.
 *
 * After separation the spacecraft waits in STANDBY for ground contact. If
 * none arrives within the configured timeout it commands SAFE on its own,
 * so detumbling starts even when the first pass is missed.
 *
 * The owner calls obsw_standby_timer_tick() once per second with the
 * current mode, the timeout and whether ground contact happened since the
 * previous tick, and commands SAFE when it returns true. Pure logic: no
 * clock, no FSM access, no global state.
 *
 * Implements OBSW-SRS-187, OBSW-SRS-188.
 */

#include "obsw/fdir/fsm.h"

#include <stdbool.h>
#include <stdint.h>

/** Default STANDBY → SAFE timeout [s] when the platform sets none. */
#ifndef OBSW_STANDBY_SAFE_TIMEOUT_S
#define OBSW_STANDBY_SAFE_TIMEOUT_S 300U
#endif

typedef struct {
    uint32_t elapsed_s; /**< Seconds in STANDBY since entry or last contact */
} obsw_standby_timer_t;

/** Start a fresh countdown. */
void obsw_standby_timer_reset(obsw_standby_timer_t *t);

/**
 * Advance the timer by one second.
 *
 * @param t              Timer state.
 * @param mode           Current FSM mode. Outside STANDBY the countdown
 *                       resets and the function returns false.
 * @param timeout_s      Seconds without contact before SAFE; 0 means
 *                       command SAFE on the first STANDBY tick.
 * @param ground_contact True if a well-formed TC was received since the
 *                       previous tick; restarts the countdown.
 * @return true when the caller should command SAFE.
 */
bool obsw_standby_timer_tick(obsw_standby_timer_t *t,
                             obsw_fsm_mode_t mode,
                             uint32_t timeout_s,
                             bool ground_contact);

#endif /* OBSW_FDIR_STANDBY_TIMER_H */
