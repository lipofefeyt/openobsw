/**
 * @file bytes.h
 * @brief Big-endian byte extraction helpers.
 *
 * Inline helpers for reading multi-byte big-endian integers from a byte
 * pointer. Used by PUS service TC user-data parsers throughout the library.
 */
#ifndef OBSW_UTIL_BYTES_H
#define OBSW_UTIL_BYTES_H

#include <stdint.h>

/** Read a big-endian uint16 from two consecutive bytes at @p p. */
static inline uint16_t obsw_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/** Read a big-endian uint32 from four consecutive bytes at @p p. */
static inline uint32_t obsw_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8)
         |  (uint32_t)p[3];
}

#endif /* OBSW_UTIL_BYTES_H */
