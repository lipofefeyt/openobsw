/**
 * @file watchdog.h
 * @brief Software watchdog manager.
 *
 * A countdown-based software watchdog. The application calls
 * obsw_wd_kick() each control cycle to reset the countdown.
 * If obsw_wd_tick() is called without a preceding kick, the
 * countdown decrements. On expiry, the configured callback fires.
 *
 * Typical wiring:
 *
 *   obsw_wd_init(&wd, 10, on_watchdog_expiry, &fsm);
 *
 *   void on_watchdog_expiry(void *ctx) {
 *       obsw_s5_report(&s5, OBSW_S5_HIGH, EVENT_WD_EXPIRY, NULL, 0);
 *       obsw_fsm_to_safe((obsw_fsm_ctx_t *)ctx);
 *   }
 *
 * Call obsw_wd_tick() once per control cycle, after obsw_wd_kick()
 * from all monitored tasks.
 */
#ifndef OBSW_FDIR_WATCHDOG_H
#define OBSW_FDIR_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define OBSW_WD_OK 0
#define OBSW_WD_ERR_NULL -1
#define OBSW_WD_ERR_PARAM -2

    /** Callback fired on watchdog expiry. */
    typedef void (*obsw_wd_expire_cb_t)(void *ctx);

    typedef struct
    {
        uint32_t timeout_ticks; /**< Reload value                    */
        uint32_t countdown;     /**< Ticks remaining                 */
        bool kicked;            /**< Set by kick(), cleared by tick() */
        bool expired;           /**< Latched on first expiry          */
        obsw_wd_expire_cb_t expire_cb;
        void *cb_ctx;
    } obsw_wd_ctx_t;

    /**
     * Initialise the watchdog.
     *
     * @param wd             Watchdog context.
     * @param timeout_ticks  Ticks before expiry if not kicked (must be > 0).
     * @param expire_cb      Called once on expiry (may not be NULL).
     * @param cb_ctx         Passed through to expire_cb.
     */
    int obsw_wd_init(obsw_wd_ctx_t *wd,
                     uint32_t timeout_ticks,
                     obsw_wd_expire_cb_t expire_cb,
                     void *cb_ctx);

    /**
     * Kick the watchdog — signals that the monitored task is alive.
     * Resets the countdown to timeout_ticks on the next tick().
     */
    void obsw_wd_kick(obsw_wd_ctx_t *wd);

    /**
     * Advance the watchdog by one tick.
     * If kicked since the last tick, reloads the countdown.
     * Otherwise decrements; fires expire_cb once when countdown reaches zero.
     * Call once per control cycle.
     */
    void obsw_wd_tick(obsw_wd_ctx_t *wd);

    /** True if the watchdog has expired (latched until re-init). */
    bool obsw_wd_expired(const obsw_wd_ctx_t *wd);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_FDIR_WATCHDOG_H */