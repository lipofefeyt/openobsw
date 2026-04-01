#ifndef OBSW_CCSDS_TC_FRAME_H
#define OBSW_CCSDS_TC_FRAME_H

/**
 * @file tc_frame.h
 * @brief TC Space Data Link Protocol — CCSDS 232.0-B-4
 *
 * Covers TC Transfer Frame primary header parse and CRC-16/CCITT validation.
 * CLTU sync/channel coding (CCSDS 231.0-B) is out of scope for v0.1.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBSW_TC_FRAME_PRIMARY_HDR_LEN 5U
#define OBSW_TC_FRAME_CRC_LEN 2U
#define OBSW_TC_FRAME_MIN_LEN (OBSW_TC_FRAME_PRIMARY_HDR_LEN + OBSW_TC_FRAME_CRC_LEN + 1U)
#ifndef OBSW_TC_FRAME_MAX_LEN
#define OBSW_TC_FRAME_MAX_LEN 1024U /**< Mission-configurable, CCSDS max 1024 */
#endif

typedef enum {
    OBSW_TC_FRAME_OK            = 0,
    OBSW_TC_FRAME_ERR_NULL      = -1,
    OBSW_TC_FRAME_ERR_TOO_SHORT = -2,
    OBSW_TC_FRAME_ERR_CRC       = -3, /**< CRC mismatch — frame corrupted */
    OBSW_TC_FRAME_ERR_BUF_SMALL = -4,
} obsw_tc_frame_err_t;

/**
 * @brief Decoded TC Transfer Frame primary header.
 */
typedef struct {
    uint8_t version;            /**< Transfer Frame Version Number (2-bit, always 0b00) */
    uint8_t bypass_flag;        /**< Bypass flag (Type-A/B service selection)           */
    uint8_t ctrl_cmd_flag;      /**< Control Command Flag                                */
    uint16_t spacecraft_id;     /**< Spacecraft ID (10-bit)                              */
    uint8_t virtual_channel_id; /**< VC ID (6-bit)                                       */
    uint16_t frame_len;         /**< Total frame length in bytes (1-1024)                */
    uint8_t frame_seq_num;      /**< Frame Sequence Number (8-bit)                       */
} obsw_tc_frame_header_t;

/**
 * @brief Non-owning view of a decoded TC frame.
 *
 * `data` points into the original buffer. CRC is already validated.
 */
typedef struct {
    obsw_tc_frame_header_t header;
    const uint8_t *data; /**< Pointer to frame data field */
    uint16_t data_len;   /**< Length of data field (excl. CRC) */
} obsw_tc_frame_t;

/**
 * @brief Decode and CRC-validate a TC Transfer Frame.
 *
 * @param[in]  buf  Raw frame buffer.
 * @param[in]  len  Number of valid bytes.
 * @param[out] frm  Decoded frame view (data points into buf).
 * @return OBSW_TC_FRAME_OK on success, negative obsw_tc_frame_err_t on failure.
 */
int obsw_tc_frame_decode(const uint8_t *buf, size_t len, obsw_tc_frame_t *frm);

/**
 * @brief Compute CRC-16/CCITT over buf[0..len-1].
 *
 * Polynomial 0x1021, initial value 0xFFFF, no final XOR.
 * Exposed for testing and re-use by higher layers.
 */
uint16_t obsw_crc16_ccitt(const uint8_t *buf, size_t len);

/**
 * @brief Return human-readable string for a TC frame error code.
 */
const char *obsw_tc_frame_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_CCSDS_TC_FRAME_H */