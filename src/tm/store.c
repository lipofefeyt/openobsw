#include "obsw/tm/store.h"
#include <string.h>

int obsw_tm_store_init(obsw_tm_store_t *s)
{
    if (!s)
        return OBSW_TM_ERR_NULL;
    s->head = 0;
    s->tail = 0;
    return OBSW_TM_OK;
}

static size_t next_slot(size_t idx)
{
    return (idx + 1U) & (OBSW_TM_STORE_SLOTS - 1U);
}

int obsw_tm_store_enqueue(obsw_tm_store_t *s, const uint8_t *pkt, uint16_t len)
{
    if (!s || !pkt)
        return OBSW_TM_ERR_NULL;
    if (len > OBSW_TM_MAX_PACKET_LEN)
        return OBSW_TM_ERR_TOO_LARGE;
    if (next_slot(s->head) == s->tail)
        return OBSW_TM_ERR_FULL;

    memcpy(s->slots[s->head].data, pkt, len);
    s->slots[s->head].len = len;
    s->head = next_slot(s->head);
    return OBSW_TM_OK;
}

int obsw_tm_store_dequeue(obsw_tm_store_t *s, uint8_t *buf,
                          size_t buf_len, uint16_t *out_len)
{
    if (!s || !buf || !out_len)
        return OBSW_TM_ERR_NULL;
    if (s->head == s->tail)
        return OBSW_TM_ERR_EMPTY;

    uint16_t len = s->slots[s->tail].len;
    if (buf_len < len)
        return OBSW_TM_ERR_TOO_LARGE;

    memcpy(buf, s->slots[s->tail].data, len);
    *out_len = len;
    s->tail = next_slot(s->tail);
    return OBSW_TM_OK;
}

bool obsw_tm_store_empty(const obsw_tm_store_t *s)
{
    return s && (s->head == s->tail);
}

size_t obsw_tm_store_count(const obsw_tm_store_t *s)
{
    if (!s)
        return 0;
    return (s->head - s->tail) & (OBSW_TM_STORE_SLOTS - 1U);
}