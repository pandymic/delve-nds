// Seeded xorshift32 generator and seed hashing (rng.h).

#include "rng.h"

void rng_seed(Rng *rng, uint32_t seed)
{
    rng->state = seed ? seed : 0x9E3779B9u;
}

uint32_t rng_next(Rng *rng)
{
    // Marsaglia xorshift32 (13, 17, 5)
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

uint32_t rng_range(Rng *rng, uint32_t n)
{
    if (0 == n)
        return 0;
    // Multiply-shift avoids the slow modulo and its bias towards low values.
    return (uint32_t)(((uint64_t)rng_next(rng) * n) >> 32);
}

int32_t rng_between(Rng *rng, int32_t lo, int32_t hi)
{
    if (hi <= lo)
        return lo;
    return lo + (int32_t)rng_range(rng, (uint32_t)(hi - lo) + 1);
}

int rng_chance(Rng *rng, uint32_t percent)
{
    return rng_range(rng, 100) < percent;
}

// Finaliser from MurmurHash3, good avalanche for cheap.
static uint32_t mix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

uint32_t rng_hash3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = mix32(a ^ 0xA511E9B3u);
    h = mix32(h ^ (b * 0x9E3779B1u));
    h = mix32(h ^ (c * 0x7FEB352Du));
    return h;
}
