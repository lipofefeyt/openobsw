/**
 * @file libc_shim.c
 * @brief Minimal libc replacements for ZynqMP bare-metal.
 *
 * Provides memcpy, memset, strlen, sqrtf, acosf and errno stub.
 * No system headers — intentional reimplementation for bare-metal.
 */

/* Suppress missing-prototype warnings — intentional reimplementation */
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
size_t strlen(const char *s);
int   *__errno(void);
float  sqrtf(float x);
double sqrt(double x);
float  acosf(float x);

/* ------------------------------------------------------------------ */

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

/* errno stub required by newlib math functions */
static int _errno_val = 0;
int *__errno(void) { return &_errno_val; }

/* Math via compiler builtins — no libm needed */
float  sqrtf(float x)  { return __builtin_sqrtf(x); }
double sqrt(double x)  { return __builtin_sqrt(x);  }
float  acosf(float x)  { return __builtin_acosf(x); }
