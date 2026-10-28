/**
 * @file obt.c
 * @brief On-board time (OBT) implementation.
 */
#include "obsw/time/obt.h"

void obsw_obt_advance(obsw_obt_t *obt, uint32_t dt_ms)
{
    if (!obt) {
        return;
    }

    uint32_t scaled   = (dt_ms * 65536U) + (uint32_t)obt->rem;
    uint32_t new_fine = (uint32_t)obt->fine + (scaled / 1000U);

    obt->rem = (uint16_t)(scaled % 1000U);

    obt->coarse += new_fine / 65536U;
    obt->fine = (uint16_t)(new_fine % 65536U);
}

void obsw_obt_set(obsw_obt_t *obt, uint32_t coarse, uint16_t fine)
{
    if (!obt) {
        return;
    }

    obt->coarse = coarse;
    obt->fine   = fine;
    obt->rem    = 0U;
}

uint32_t obsw_obt_seconds(const obsw_obt_t *obt)
{
    if (!obt) {
        return 0U;
    }

    return obt->coarse;
}
