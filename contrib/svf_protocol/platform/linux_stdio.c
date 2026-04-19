/**
 * @file linux_stdio.c
 * @brief SVF I/O implementation for Linux host (stdin/stdout).
 *
 * Use this when building obsw_sim on x86_64 or aarch64 Linux.
 * SVF connects via subprocess stdin/stdout pipes.
 */

#include "../obsw_svf_protocol.h"
#include <stdio.h>

int svf_io_read(uint8_t *buf, uint16_t len)
{
    size_t n = fread(buf, 1, len, stdin);
    return (n == len) ? 0 : -1;
}

int svf_io_write(const uint8_t *buf, uint16_t len)
{
    size_t n = fwrite(buf, 1, len, stdout);
    return (n == len) ? 0 : -1;
}

void svf_io_flush(void)
{
    fflush(stdout);
}
