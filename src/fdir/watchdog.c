#include "obsw/fdir/watchdog.h"

int obsw_wd_init(obsw_wd_ctx_t *wd,
                 uint32_t timeout_ticks,
                 obsw_wd_expire_cb_t expire_cb,
                 void *cb_ctx)
{
    if (!wd || !expire_cb)
        return OBSW_WD_ERR_NULL;
    if (timeout_ticks == 0U)
        return OBSW_WD_ERR_PARAM;

    wd->timeout_ticks = timeout_ticks;
    wd->countdown = timeout_ticks;
    wd->kicked = false;
    wd->expired = false;
    wd->expire_cb = expire_cb;
    wd->cb_ctx = cb_ctx;
    return OBSW_WD_OK;
}

void obsw_wd_kick(obsw_wd_ctx_t *wd)
{
    if (!wd)
        return;
    wd->kicked = true;
}

void obsw_wd_tick(obsw_wd_ctx_t *wd)
{
    if (!wd || wd->expired)
        return;

    if (wd->kicked)
    {
        wd->countdown = wd->timeout_ticks;
        wd->kicked = false;
        return;
    }

    if (wd->countdown > 0U)
        wd->countdown--;

    if (wd->countdown == 0U)
    {
        wd->expired = true;
        wd->expire_cb(wd->cb_ctx);
    }
}

bool obsw_wd_expired(const obsw_wd_ctx_t *wd)
{
    if (!wd)
        return false;
    return wd->expired;
}