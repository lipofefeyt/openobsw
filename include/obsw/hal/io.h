/**
 * @file io.h
 * @brief Hardware Abstraction Layer — I/O interface.
 *
 * Provides a thin, pointer-based I/O abstraction. Platform ports
 * (host/bare-metal) implement obsw_io_ops_t and pass it in at init.
 * The rest of the stack never calls platform I/O directly.
 *
 * Uses uint16_t for buffer length instead of size_t for portability
 * across 16-bit targets (MSP430) where size_t is __int20 unsigned
 * and causes function pointer type mismatches.
 */
#ifndef OBSW_HAL_IO_H
#define OBSW_HAL_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBSW_IO_OK 0
#define OBSW_IO_ERR_NULL -1
#define OBSW_IO_ERR_WRITE -2
#define OBSW_IO_ERR_READ -3

typedef struct {
    int (*write)(const uint8_t *buf, uint16_t len, void *ctx);
    int (*read)(uint8_t *buf, uint16_t len, void *ctx);
    void *ctx;
} obsw_io_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* OBSW_HAL_IO_H */