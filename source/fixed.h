// Fixed-point math for gameplay: 20.12, the same layout as libnds f32, so
// values can be passed straight to the 3D API. Platform independent.

#ifndef DELVE_FIXED_H
#define DELVE_FIXED_H

#include <stdint.h>

#ifdef __NDS__
// The DS has a hardware divider and square root unit; libnds' f32 helpers
// use the same 20.12 layout. The portable versions below are for the host.
#include <nds/arm9/math.h>
#endif

typedef int32_t fx;

#define FX_SHIFT 12
#define FX_ONE   (1 << FX_SHIFT)
#define FX_HALF  (FX_ONE / 2)

// For compile-time constants only (folds to an integer).
#define FX(f)      ((fx)((f) * 4096.0 + ((f) >= 0 ? 0.5 : -0.5)))
#define FX_INT(i)  ((fx)(i) * FX_ONE)
#define FX_TO_INT(v) ((v) >> FX_SHIFT)

static inline fx fx_mul(fx a, fx b)
{
    return (fx)(((int64_t)a * b) >> FX_SHIFT);
}

static inline fx fx_div(fx a, fx b)
{
#ifdef __NDS__
    return divf32(a, b);
#else
    return (fx)(((int64_t)a * FX_ONE) / b);
#endif
}

static inline fx fx_abs(fx a)
{
    return a < 0 ? -a : a;
}

static inline fx fx_min(fx a, fx b)
{
    return a < b ? a : b;
}

static inline fx fx_max(fx a, fx b)
{
    return a > b ? a : b;
}

static inline fx fx_clamp(fx v, fx lo, fx hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// t in [0, FX_ONE]
static inline fx fx_lerp(fx a, fx b, fx t)
{
    return a + fx_mul(b - a, t);
}

// Smoothstep ease for t in [0, FX_ONE].
static inline fx fx_smooth(fx t)
{
    return fx_mul(fx_mul(t, t), FX_INT(3) - 2 * t);
}

// Integer square root of a non-negative fixed-point value, result in fx.
fx fx_sqrt(fx v);

// Plain integer helpers (tile coordinates, counters, table values).
static inline int imin(int a, int b)
{
    return a < b ? a : b;
}

static inline int imax(int a, int b)
{
    return a > b ? a : b;
}

static inline int iabs(int a)
{
    return a < 0 ? -a : a;
}

static inline int iclamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

#endif // DELVE_FIXED_H
