/**
 * @file pus_tm.h
 * @brief PUS-C TM packet builder — shared utility for all PUS services.
 *
 * Builds a complete PUS-C TM space packet (primary header + secondary header
 * + data field) and enqueues it into the TM store.
 *
 * All service-specific TM reports (TM(1,1), TM(5,2), TM(17,2), etc.) are
 * emitted through this single entry point.
 */
#ifndef OBSW_PUS_TM_H
#define OBSW_PUS_TM_H

#include "obsw/tm/store.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------ */
/* PUS-C TM secondary header layout (ECSS-E-ST-70-41C §5.4.3.1)      */
/*                                                                     */
/* PUS version(4b) + spare(4b)  — 1 byte                              */
/* Service type                 — 1 byte                               */
/* Subservice type              — 1 byte                               */
/* Message sequence count       — 2 bytes                              */
/* Destination ID               — 2 bytes                              */
/* Absolute time                — 4 bytes (CUC, mission-dependent)     */
/* ------------------------------------------------------------------ */
#define OBSW_PUS_TM_SEC_HDR_LEN 11U
#define OBSW_PUS_VERSION 0x20U /**< PUS-C = 0b0010, spare = 0b0000 */

#define OBSW_PUS_TM_OK 0
#define OBSW_PUS_TM_ERR_NULL -1
#define OBSW_PUS_TM_ERR_TOO_LARGE -2
#define OBSW_PUS_TM_ERR_STORE -3

    /**
     * Build a PUS-C TM space packet and enqueue it into @p store.
     *
     * @param store       Destination TM ring buffer.
     * @param apid        Source APID (11-bit).
     * @param seq_count   Packet sequence count (caller-managed, wraps at 0x3FFF).
     * @param svc         PUS service type.
     * @param subsvc      PUS subservice type.
     * @param msg_counter Per-service message sequence counter.
     * @param dest_id     Destination ID (ground station or on-board app).
     * @param timestamp   Mission elapsed time / CUC timestamp (4 bytes).
     * @param data        Application data field (may be NULL if data_len == 0).
     * @param data_len    Length of application data in bytes.
     * @return OBSW_PUS_TM_OK or negative error code.
     */
    int obsw_pus_tm_build(obsw_tm_store_t *store,
                          uint16_t apid,
                          uint16_t seq_count,
                          uint8_t svc,
                          uint8_t subsvc,
                          uint16_t msg_counter,
                          uint16_t dest_id,
                          uint32_t timestamp,
                          const uint8_t *data,
                          uint16_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_PUS_TM_H */