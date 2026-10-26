/**
 * @file obt.h
 * @brief On-board time (OBT) — free-running mission clock, settable via PUS S9.
 *
 * Represented as CUC-4,2 (ECSS-E-ST-70-41C annex A): 4-byte coarse seconds +
 * 2-byte fine fraction (1/65536 s resolution), matching the wire format
 * opensvf's PusService9 sends in TC(9,128).
 *
 * Free-runs at the tick rate of whatever loop owns it (`obsw_obt_advance()`
 * each tick) and jumps to a new value on TC(9,128) (`obsw_obt_set()`) — no
 * epoch/correlation offset, matching opensvf's `ObcEquipment` OBT model.
 */
#ifndef OBSW_TIME_OBT_H
#define OBSW_TIME_OBT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t coarse; /**< Whole seconds                          */
    uint16_t fine;   /**< Fractional seconds, 1/65536 s units    */
    uint16_t rem;    /**< Sub-fine remainder, 1/1000 fine units  */
} obsw_obt_t;

/**
 * Advance @p obt by @p dt_ms milliseconds.
 *
 * @p dt_ms must be < 65536 (65.536 s) to avoid overflow in the
 * intermediate fixed-point math; all current callers tick at
 * 100 ms - 1000 ms, well within range. The ms-to-fine remainder is
 * carried in `rem`, so repeated ticks do not drift (ten 100 ms ticks
 * equal exactly one second).
 */
void obsw_obt_advance(obsw_obt_t *obt, uint32_t dt_ms);

/** Set @p obt to an absolute coarse/fine value (TC(9,128) Set OBT); clears `rem`. */
void obsw_obt_set(obsw_obt_t *obt, uint32_t coarse, uint16_t fine);

/** Whole-seconds view of @p obt, for HK parameters and TM timestamps. */
uint32_t obsw_obt_seconds(const obsw_obt_t *obt);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_TIME_OBT_H */
