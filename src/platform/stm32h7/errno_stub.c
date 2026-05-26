/* newlib-nano does not export __errno(); provide a bare-metal stub so that
 * libm (acosf, sqrtf wrappers) can link without the full reentrant C library. */
int *__errno(void)
{
    static int errno_val = 0;
    return &errno_val;
}
