/**
 * @file dispatcher.h
 * @brief TC command dispatcher — routes incoming TC packets to handlers.
 *
 * Design principles:
 *  - Zero dynamic allocation; routing table is caller-owned static memory.
 *  - Handlers are decoupled from transport via the responder callback.
 *  - Each handler carries its own context pointer — no global state.
 */
#ifndef OBSW_TC_DISPATCHER_H
#define OBSW_TC_DISPATCHER_H

#include "obsw/ccsds/space_packet.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Return codes                                                        */
/* ------------------------------------------------------------------ */

#define OBSW_TC_OK               0
#define OBSW_TC_ERR_NULL        -1
#define OBSW_TC_ERR_NO_ROUTE    -2   /**< No handler matched          */
#define OBSW_TC_ERR_HANDLER     -3   /**< Handler returned an error   */
#define OBSW_TC_ERR_PARSE       -4   /**< Packet could not be parsed  */

/* ------------------------------------------------------------------ */
/* TC packet descriptor                                               */
/* ------------------------------------------------------------------ */

/**
 * A fully parsed TC command passed to handlers.
 * All pointers reference the original frame buffer — zero copy.
 */
typedef struct {
    uint16_t        apid;
    uint8_t         service;
    uint8_t         subservice;
    uint16_t        seq_count;
    const uint8_t  *user_data;     /**< PUS user data field (may be NULL) */
    uint16_t        user_data_len;
} obsw_tc_t;

/* ------------------------------------------------------------------ */
/* Responder callback                                                  */
/* ------------------------------------------------------------------ */

/** Acknowledgement flags (PUS-C S1 style) */
#define OBSW_TC_ACK_ACCEPT   0x01U
#define OBSW_TC_ACK_START    0x02U
#define OBSW_TC_ACK_PROGRESS 0x04U
#define OBSW_TC_ACK_COMPLETE 0x08U
#define OBSW_TC_ACK_REJECT   0x80U   /**< Negative ack */

/**
 * Called by a handler to emit a TM acknowledgement.
 *
 * @param flags   Combination of OBSW_TC_ACK_* flags.
 * @param tc      The originating TC command.
 * @param ctx     Opaque context provided at dispatcher init.
 */
typedef void (*obsw_tc_responder_t)(uint8_t flags,
                                    const obsw_tc_t *tc,
                                    void *ctx);

/* ------------------------------------------------------------------ */
/* Handler and routing table                                           */
/* ------------------------------------------------------------------ */

/**
 * TC handler function.
 *
 * @param tc       Parsed TC command.
 * @param respond  Callback to emit TM ack/nak.
 * @param ctx      Handler-specific context (from route table entry).
 * @return 0 on success, negative on error (triggers NACK).
 */
typedef int (*obsw_tc_handler_fn)(const obsw_tc_t *tc,
                                   obsw_tc_responder_t respond,
                                   void *ctx);

/** Single entry in the static routing table. */
typedef struct {
    uint16_t           apid;        /**< 0xFFFF = match any APID       */
    uint8_t            service;     /**< PUS service type               */
    uint8_t            subservice;  /**< PUS subservice type            */
    obsw_tc_handler_fn handler;
    void              *ctx;         /**< Passed through to handler      */
} obsw_tc_route_t;

/* ------------------------------------------------------------------ */
/* Dispatcher                                                          */
/* ------------------------------------------------------------------ */

/** Opaque dispatcher state. Embed in your application struct. */
typedef struct {
    obsw_tc_route_t      *table;
    size_t                table_len;
    obsw_tc_responder_t   responder;
    void                 *responder_ctx;
} obsw_tc_dispatcher_t;

/**
 * Initialise the dispatcher.
 *
 * @param d              Dispatcher to initialise.
 * @param table          Caller-owned static routing table.
 * @param table_len      Number of entries in table.
 * @param responder      TM ack/nak callback.
 * @param responder_ctx  Passed through to every responder call.
 * @return OBSW_TC_OK or OBSW_TC_ERR_NULL.
 */
int obsw_tc_dispatcher_init(obsw_tc_dispatcher_t *d,
                             obsw_tc_route_t *table,
                             size_t table_len,
                             obsw_tc_responder_t responder,
                             void *responder_ctx);

/**
 * Feed a raw TC frame into the dispatcher.
 *
 * The dispatcher parses the space packet, looks up the route,
 * and calls the matching handler. If no route matches,
 * the responder is called with OBSW_TC_ACK_REJECT.
 *
 * @param d          Initialised dispatcher.
 * @param frame      Raw TC frame bytes.
 * @param frame_len  Length of frame in bytes.
 * @return OBSW_TC_OK, OBSW_TC_ERR_NO_ROUTE, OBSW_TC_ERR_PARSE,
 *         or OBSW_TC_ERR_HANDLER.
 */
int obsw_tc_dispatcher_feed(obsw_tc_dispatcher_t *d,
                             const uint8_t *frame,
                             size_t frame_len);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_TC_DISPATCHER_H */