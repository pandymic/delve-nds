// Visual effects: particles (sparks, shards, splinters, dust, embers) and
// floating damage numbers. Pure logic in fixed point; render.c draws them.
// Static pools: when full, the oldest particle is reused.

#ifndef DELVE_FX_H
#define DELVE_FX_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "fixed.h"
#include "rng.h"

typedef enum {
    PART_SPARK,     // white, hits
    PART_SHARD,     // terracotta, pots
    PART_SPLINTER,  // wood, barrels and crates
    PART_DUST,      // grey, walls and landings
    PART_EMBER,     // orange, torches and explosions
    PART_SMOKE,     // dark grey, explosions (rises)
    PART_GOLD,      // gold glints
    PART_BLOOD,     // dark red, the player being hurt
    PART_MOTE,      // drifting dust in the air (drawn like PART_DUST)
    PART_CINDER,    // a hot speck: flickers up, cools white to red
    PART_FLAME,     // a lick of flame: rises a little, shrinks
    PART_WISP,      // a thin wisp of smoke: drifts up, thins out
    PART_KIND_COUNT
} ParticleKind;

typedef struct {
    bool active;
    uint8_t kind;   // ParticleKind
    uint8_t life;   // frames left
    uint8_t max_life;
    fx x, y, z;
    fx vx, vy, vz;
} Particle;

typedef struct {
    bool active;
    uint8_t value;
    uint8_t life;
    bool kill;      // a killing blow (drawn bigger, gold)
    fx x, y, z;
} Floater;

#define FX_GRAVITY      FX(0.012)
#define FLOATER_LIFE    40

typedef struct {
    Particle parts[MAX_PARTICLES];
    Floater floaters[MAX_FLOATERS];
    int next;       // round-robin slot when the pool is full
    Rng rng;
} Fx;

// Empties the pools; seed drives the particles' random spread.
void fx_init(Fx *f, uint32_t seed);
// Empties the pools, keeping the random stream.
void fx_clear(Fx *f);

// n particles of a kind bursting from (x, y, z) with the given speed.
void fx_burst(Fx *f, ParticleKind kind, int n, fx x, fx y, fx z, fx speed);

// One particle with an exact velocity.
void fx_emit(Fx *f, ParticleKind kind, fx x, fx y, fx z, fx vx, fx vy,
             fx vz, int life);

// A damage number rising from (x, z).
void fx_number(Fx *f, int value, fx x, fx z, bool kill);

// Fire and smoke particles: they rise instead of falling, and some age
// through several looks (render.c).
bool fx_floats(ParticleKind kind);

// Advances everything one frame. Particles bounce once on the floor
// (y = 0) and settle; smoke, embers, cinders and flames float up instead.
void fx_update(Fx *f);

// Particles alive now (tests and the debug meter).
int fx_active_count(const Fx *f);

#endif // DELVE_FX_H
