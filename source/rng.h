// Seeded PRNG (xorshift32) and seed hashing.
//
// Platform independent: no libnds includes, so it can be unit-tested on the
// host with plain gcc.

#ifndef DELVE_RNG_H
#define DELVE_RNG_H

#include <stdint.h>

typedef struct {
    uint32_t state;
} Rng;

// Seeds the generator. A zero seed is remapped, xorshift must not be zero.
void rng_seed(Rng *rng, uint32_t seed);

// Next raw 32-bit value.
uint32_t rng_next(Rng *rng);

// Uniform integer in [0, n). Returns 0 when n is 0.
uint32_t rng_range(Rng *rng, uint32_t n);

// Uniform integer in [lo, hi] inclusive.
int32_t rng_between(Rng *rng, int32_t lo, int32_t hi);

// Returns 1 with probability percent/100.
int rng_chance(Rng *rng, uint32_t percent);

// Mixes three values into one seed: room_seed = hash(run_seed, depth, door).
uint32_t rng_hash3(uint32_t a, uint32_t b, uint32_t c);

#endif // DELVE_RNG_H
