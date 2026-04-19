/**
 * @file obsw_svf_protocol.c
 * @brief SVF pipe protocol — portable implementation.
 *
 * Implements the SVF wire protocol v3.
 * Platform I/O provided via svf_io_read/write/flush.
 *
 * No dynamic allocation. No OS dependencies.
 * Suitable for bare-metal, RTOS, and Linux host environments.
 *
 * License: Apache 2.0
 */

#include "obsw_svf_protocol.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void write_u16_be(uint16_t v)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)(v & 0xFFU);
    svf_io_write(buf, 2);
}

static void write_frame(uint8_t type, const uint8_t *data, uint16_t len)
{
    uint8_t type_byte = type;
    svf_io_write(&type_byte, 1);
    write_u16_be(len);
    svf_io_write(data, len);
}

/* ------------------------------------------------------------------ */
/* Protocol API implementation                                        */
/* ------------------------------------------------------------------ */

void svf_init(const char *srdb_version)
{
    /* Platform-specific: route to stderr or UART debug output.
     * For Linux host: use fprintf(stderr, ...).
     * For bare-metal: use your UART debug port.
     * The SVF OBCEmulatorAdapter reads these lines from stderr. */
    const char *banner =
        "[OBSW] Host sim started (type-frame protocol v2).\n";
    const char *ver_prefix = "[OBSW] SRDB version: ";
    const char *newline    = "\n";

    /* Write to stderr (host) or debug UART (bare-metal).
     * Adapt svf_io_write_debug() for your platform. */
    (void)banner;
    (void)ver_prefix;
    (void)srdb_version;
    (void)newline;
    /* TODO: implement svf_debug_print() for your platform */
}

uint8_t svf_read_frame(uint8_t *buf, uint16_t bufsize, uint16_t *out_len)
{
    uint8_t type;
    uint8_t len_bytes[2];
    uint16_t len;

    if (svf_io_read(&type, 1) != 0)
        return 0;

    if (svf_io_read(len_bytes, 2) != 0)
        return 0;

    len = (uint16_t)(((uint16_t)len_bytes[0] << 8) | len_bytes[1]);

    if (len > bufsize)
        return 0;

    if (len > 0 && svf_io_read(buf, len) != 0)
        return 0;

    *out_len = len;
    return type;
}

void svf_write_tm(const uint8_t *pkt, uint16_t len)
{
    write_frame(SVF_FRAME_TM, pkt, len);
    svf_io_flush();
}

void svf_write_actuator(const svf_actuator_frame_t *act)
{
    write_frame(SVF_FRAME_ACTUATOR,
                (const uint8_t *)act,
                SVF_ACTUATOR_FRAME_LEN);
    svf_io_flush();
}

void svf_write_sync(void)
{
    uint8_t sync = SVF_SYNC_BYTE;
    svf_io_write(&sync, 1);
    svf_io_flush();
}
