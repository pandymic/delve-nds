// Fixed-point square root (fixed.h).

#include "fixed.h"

fx fx_sqrt(fx v)
{
    if (v <= 0)
        return 0;
#ifdef __NDS__
    return (fx)sqrtf32((uint32_t)v);   // hardware square root
#else
    // sqrt(v / 4096) * 4096 = sqrt(v * 4096); bitwise integer sqrt on 64 bits.
    uint64_t n = (uint64_t)v << FX_SHIFT;
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (fx)res;
#endif
}
