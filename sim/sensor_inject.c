#include "sensor_inject.h"

#include <stdio.h>
#include <string.h>

uint8_t obsw_sim_read_frame(uint8_t *buf, uint16_t buf_len, uint16_t *out_len)
{
    *out_len = 0;

    /* Read type byte */
    uint8_t type;
    if (fread(&type, 1, 1, stdin) != 1)
        return 0;

    /* Read 2-byte BE length */
    uint8_t len_buf[2];
    if (fread(len_buf, 1, 2, stdin) != 2)
        return 0;
    uint16_t length = (uint16_t)((len_buf[0] << 8) | len_buf[1]);

    if (length == 0 || length > buf_len)
        return 0;

    /* Read body */
    if (fread(buf, 1, length, stdin) != length)
        return 0;

    *out_len = length;
    return type;
}

bool obsw_sim_parse_sensor(const uint8_t *buf, uint16_t len, obsw_sensor_frame_t *out)
{
    if (!buf || !out || len < OBSW_SENSOR_FRAME_LEN)
        return false;
    memcpy(out, buf, sizeof(obsw_sensor_frame_t));
    return true;
}