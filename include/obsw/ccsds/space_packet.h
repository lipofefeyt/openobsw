/**
 * @file space_packet.h
 * @brief CCSDS Space Packet Protocol (CCSDS 133.0-B-2)
 *
 * Provides encode/decode for the 6-byte primary header and
 * optional secondary header (timestamp + PUS data field header).
 */
#ifndef OBSW_CCSDS_SPACE_PACKET_H
#define OBSW_CCSDS_SPACE_PACKET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define OBSW_SP_PRIMARY_HEADER_LEN  6U
#define OBSW_SP_MAX_DATA_LEN        65536U  /* data field: 0..65535 octets */
#define OBSW_SP_APID_IDLE           0x7FFU  /* idle packet APID            */

/* Packet type */
#define OBSW_SP_TYPE_TM  0U
#define OBSW_SP_TYPE_TC  1U

/* Sequence flags */
#define OBSW_SP_SEQ_CONTINUATION  0U
#define OBSW_SP_SEQ_FIRST         1U
#define OBSW_SP_SEQ_LAST          2U
#define OBSW_SP_SEQ_UNSEGMENTED   3U

/* Return codes */
#define OBSW_SP_OK                0
#define OBSW_SP_ERR_NULL          -1
#define OBSW_SP_ERR_BUF_TOO_SMALL -2
#define OBSW_SP_ERR_BAD_VERSION   -3
#define OBSW_SP_ERR_BAD_LENGTH    -4

/* ------------------------------------------------------------------ */
/* Primary header                                                      */
/* ------------------------------------------------------------------ */

/**
 * Decoded primary header fields.
 * All multi-bit fields are stored as plain integers — no bitfields,
 * for portability across compilers and target ABIs.
 */
typedef struct {
    uint8_t  version;           /**< Packet Version Number (0)           */
    uint8_t  type;              /**< 0 = TM, 1 = TC                      */
    uint8_t  sec_hdr_flag;      /**< 1 = secondary header present        */
    uint16_t apid;              /**< Application Process ID (11 bits)    */
    uint8_t  seq_flags;         /**< Sequence flags (2 bits)             */
    uint16_t seq_count;         /**< Packet Sequence Count (14 bits)     */
    uint16_t data_len;          /**< Packet Data Length field (octets-1) */
} obsw_sp_primary_hdr_t;

/**
 * Encode primary header into @p buf (exactly 6 bytes).
 *
 * @param hdr  Header fields to encode.
 * @param buf  Output buffer, must be at least 6 bytes.
 * @return OBSW_SP_OK or negative error code.
 */
int obsw_sp_encode_primary(const obsw_sp_primary_hdr_t *hdr,
                            uint8_t *buf);

/**
 * Decode primary header from @p buf.
 *
 * @param buf     Input buffer, must be at least 6 bytes.
 * @param buf_len Buffer length (must be >= 6).
 * @param hdr     Output decoded header.
 * @return OBSW_SP_OK or negative error code.
 */
int obsw_sp_decode_primary(const uint8_t *buf, size_t buf_len,
                            obsw_sp_primary_hdr_t *hdr);

/* ------------------------------------------------------------------ */
/* Full packet helper                                                  */
/* ------------------------------------------------------------------ */

/**
 * Complete space packet descriptor (header + payload pointer).
 * The payload buffer is NOT owned by this struct.
 */
typedef struct {
    obsw_sp_primary_hdr_t hdr;
    const uint8_t        *payload;   /**< Points into caller-owned buffer */
    uint16_t              payload_len;
} obsw_sp_packet_t;

/**
 * Parse a complete space packet from a flat buffer.
 *
 * @param buf     Raw bytes (frame or stream segment).
 * @param buf_len Length of buf.
 * @param pkt     Output packet descriptor.
 * @return OBSW_SP_OK, or negative on bad header / length mismatch.
 */
int obsw_sp_parse(const uint8_t *buf, size_t buf_len,
                  obsw_sp_packet_t *pkt);

/**
 * Total wire length of a packet (primary header + data field).
 * data_len is the raw Packet Data Length field value.
 */
static inline size_t obsw_sp_total_len(uint16_t data_len)
{
    return (size_t)OBSW_SP_PRIMARY_HEADER_LEN + data_len + 1U;
}

#ifdef __cplusplus
}
#endif

#endif /* OBSW_CCSDS_SPACE_PACKET_H */