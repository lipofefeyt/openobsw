/**
 * @file obsw_svf_protocol.c
 * @brief SVF pipe protocol — portable implementation.
 *
 * Implements the SVF wire protocol v4.
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
    /* OpenSVF reads these lines before the first frame and checks the
     * wire protocol version on the "Host sim started" line. */
    char ver[4];
    uint8_t n = 0;
    unsigned v = SVF_PROTOCOL_VERSION;

    do {
        ver[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U && n < 3U);
    /* Digits were written least significant first */
    for (uint8_t i = 0; i < n / 2U; i++) {
        char t = ver[i];
        ver[i] = ver[n - 1U - i];
        ver[n - 1U - i] = t;
    }
    ver[n] = '\0';

    svf_io_debug("[OBSW] Host sim started (wire protocol v");
    svf_io_debug(ver);
    svf_io_debug(").\n");
    svf_io_debug("[OBSW] SRDB version: ");
    svf_io_debug(srdb_version != NULL ? srdb_version : "unknown");
    svf_io_debug("\n");
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
