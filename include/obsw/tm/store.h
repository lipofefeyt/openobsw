/**
 * @file store.h
 * @brief TM telemetry store — lock-free ring buffer for TM space packets.
 *
 * Store-and-forward buffer between telemetry sources and the TM frame
 * builder. Sized at compile time; no dynamic allocation.
 */
#ifndef OBSW_TM_STORE_H
#define OBSW_TM_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBSW_TM_OK              0
#define OBSW_TM_ERR_NULL       -1
#define OBSW_TM_ERR_FULL       -2
#define OBSW_TM_ERR_EMPTY      -3
#define OBSW_TM_ERR_TOO_LARGE  -4

/** Maximum TM packet size supported by the store (bytes). */
#ifndef OBSW_TM_MAX_PACKET_LEN
#define OBSW_TM_MAX_PACKET_LEN 1024U
#endif

/** Number of slots in the ring buffer. Must be a power of two. */
#ifndef OBSW_TM_STORE_SLOTS
#define OBSW_TM_STORE_SLOTS 32U
#endif

/** One slot in the ring buffer. */
typedef struct {
    uint8_t  data[OBSW_TM_MAX_PACKET_LEN];
    uint16_t len;
} obsw_tm_slot_t;

/** TM store — embed in application state, initialise with obsw_tm_store_init. */
typedef struct {
    obsw_tm_slot_t slots[OBSW_TM_STORE_SLOTS];
    volatile size_t head;   /**< Next slot to write */
    volatile size_t tail;   /**< Next slot to read  */
} obsw_tm_store_t;

/** Initialise store to empty state. */
int obsw_tm_store_init(obsw_tm_store_t *s);

/**
 * Enqueue a TM packet.
 *
 * @param s    Initialised store.
 * @param pkt  Packet bytes to copy in.
 * @param len  Packet length (must be <= OBSW_TM_MAX_PACKET_LEN).
 * @return OBSW_TM_OK, OBSW_TM_ERR_FULL, or OBSW_TM_ERR_TOO_LARGE.
 */
int obsw_tm_store_enqueue(obsw_tm_store_t *s,
                           const uint8_t *pkt, uint16_t len);

/**
 * Dequeue the oldest TM packet.
 *
 * @param s        Initialised store.
 * @param buf      Caller-provided buffer to copy into.
 * @param buf_len  Size of buf.
 * @param out_len  Set to actual packet length on success.
 * @return OBSW_TM_OK, OBSW_TM_ERR_EMPTY, or OBSW_TM_ERR_TOO_LARGE.
 */
int obsw_tm_store_dequeue(obsw_tm_store_t *s,
                           uint8_t *buf, size_t buf_len,
                           uint16_t *out_len);

/** True if the store contains no packets. */
bool obsw_tm_store_empty(const obsw_tm_store_t *s);

/** Number of packets currently held in the store. */
size_t obsw_tm_store_count(const obsw_tm_store_t *s);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_TM_STORE_H */