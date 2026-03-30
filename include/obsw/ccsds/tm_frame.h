#ifndef OBSW_CCSDS_TM_FRAME_H
#define OBSW_CCSDS_TM_FRAME_H

/**
 * @file tm_frame.h
 * @brief TM Space Data Link Protocol — CCSDS 132.0-B-2
 *
 * TM Transfer Frame builder. Frames are assembled from TM space packets
 * stored in the TM store (see tm/store.h). Reed-Solomon and convolutional
 * coding (CCSDS 131.0-B) are out of scope for v0.1.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define OBSW_TM_FRAME_PRIMARY_HDR_LEN 6U
#define OBSW_TM_FRAME_OCF_LEN 4U    /**< Optional — disabled by default */
#define OBSW_TM_FRAME_FECF_LEN 2U   /**< Optional CRC — enabled by default */
#define OBSW_TM_FRAME_MAX_LEN 1115U /**< CCSDS maximum */

    typedef enum
    {
        OBSW_TM_FRAME_OK = 0,
        OBSW_TM_FRAME_ERR_NULL = -1,
        OBSW_TM_FRAME_ERR_BUF_SMALL = -2,
        OBSW_TM_FRAME_ERR_NO_DATA = -3, /**< TM store empty, nothing to frame */
    } obsw_tm_frame_err_t;

    /**
     * @brief TM Transfer Frame build configuration.
     *
     * Typically constant for a given virtual channel / mission config.
     */
    typedef struct
    {
        uint16_t spacecraft_id;             /**< 10-bit SCID                        */
        uint8_t virtual_channel_id;         /**< 3-bit VCID                         */
        uint8_t master_channel_frame_count; /**< Wraps at 255, incremented by caller */
        uint8_t virtual_channel_frame_count;
        uint8_t enable_fecf;           /**< 1 = append CRC-16 FECF             */
        uint16_t frame_data_field_len; /**< Fixed data zone length (mission param) */
    } obsw_tm_frame_config_t;

    /**
     * @brief Build a TM Transfer Frame into buf.
     *
     * Fills the data field zone from `payload` (caller supplies, e.g. from TM store).
     * Unused bytes are filled with idle packets per CCSDS 133.0-B §4.3.
     *
     * @param[in]  cfg         Frame configuration.
     * @param[in]  payload     TM packet data to embed.
     * @param[in]  payload_len Length of payload.
     * @param[out] buf         Output buffer.
     * @param[in]  buf_len     Size of buf (must be >= cfg->frame_data_field_len + headers).
     * @param[out] written     Total bytes written.
     * @return OBSW_TM_FRAME_OK on success, negative obsw_tm_frame_err_t on failure.
     */
    int obsw_tm_frame_build(const obsw_tm_frame_config_t *cfg,
                            const uint8_t *payload,
                            uint16_t payload_len,
                            uint8_t *buf,
                            size_t buf_len,
                            size_t *written);

    /**
     * @brief Return human-readable string for a TM frame error code.
     */
    const char *obsw_tm_frame_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_CCSDS_TM_FRAME_H */