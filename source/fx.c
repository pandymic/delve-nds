// Particles and floating damage numbers in static pools (fx.h).

#include <string.h>

#include "fx.h"

void fx_init(Fx *f, uint32_t seed)
{
    memset(f, 0, sizeof(*f));
    rng_seed(&f->rng, seed);
}

void fx_clear(Fx *f)
{
    memset(f->parts, 0, sizeof(f->parts));
    memset(f->floaters, 0, sizeof(f->floaters));
}

static Particle *slot(Fx *f)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (!f->parts[i].active)
            return &f->parts[i];
    // Full: recycle round-robin, so a burst never fails outright.
    Particle *p = &f->parts[f->next];
    f->next = (f->next + 1) % MAX_PARTICLES;
    return p;
}

void fx_emit(Fx *f, ParticleKind kind, fx x, fx y, fx z, fx vx, fx vy,
             fx vz, int life)
{
    Particle *p = slot(f);
    p->active = true;
    p->kind = (uint8_t)kind;
    p->life = p->max_life = (uint8_t)(life > 255 ? 255 : life);
    p->x = x;
    p->y = y;
    p->z = z;
    p->vx = vx;
    p->vy = vy;
    p->vz = vz;
}

// Signed random fraction of range, in fixed point.
static fx spread(Fx *f, fx range)
{
    int r = (int)rng_range(&f->rng, 2001) - 1000;
    return range * r / 1000;
}

bool fx_floats(ParticleKind kind)
{
    return PART_SMOKE == kind || PART_EMBER == kind || PART_MOTE == kind
        || PART_CINDER == kind || PART_FLAME == kind || PART_WISP == kind;
}

void fx_burst(Fx *f, ParticleKind kind, int n, fx x, fx y, fx z, fx speed)
{
    bool floaty = fx_floats(kind);
    for (int i = 0; i < n; i++) {
        fx vx = spread(f, speed), vz = spread(f, speed);
        fx vy = floaty ? speed / 3 + spread(f, speed / 4)
                       : speed / 2 + (fx)rng_range(&f->rng, (uint32_t)speed + 1);
        int life = 18 + (int)rng_range(&f->rng, 18);
        if (PART_SHARD == kind || PART_SPLINTER == kind)
            life += 30;   // debris lingers on the floor a little
        fx_emit(f, kind, x, y, z, vx, vy, vz, life);
    }
}

void fx_number(Fx *f, int value, fx x, fx z, bool kill)
{
    Floater *best = &f->floaters[0];
    for (int i = 0; i < MAX_FLOATERS; i++) {
        if (!f->floaters[i].active) {
            best = &f->floaters[i];
            break;
        }
        if (f->floaters[i].life < best->life)
            best = &f->floaters[i];
    }
    best->active = true;
    best->value = (uint8_t)(value > 99 ? 99 : value < 0 ? 0 : value);
    best->life = FLOATER_LIFE;
    best->kill = kill;
    best->x = x + spread(f, FX(0.15));
    best->z = z + spread(f, FX(0.15));
    best->y = FX(1.0);
}

void fx_update(Fx *f)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &f->parts[i];
        if (!p->active)
            continue;
        if (0 == --p->life) {
            p->active = false;
            continue;
        }
        p->x += p->vx;
        p->z += p->vz;
        p->y += p->vy;
        switch (p->kind) {
        case PART_CINDER:
            // Flickers from side to side as it rises.
            p->vx = fx_mul(p->vx, FX(0.9)) + spread(f, FX(0.008));
            p->vz = fx_mul(p->vz, FX(0.9)) + spread(f, FX(0.008));
            continue;
        case PART_FLAME:
            // A short lick upwards that loses its push.
            p->vy = fx_mul(p->vy, FX(0.9));
            p->vx = fx_mul(p->vx, FX(0.8));
            p->vz = fx_mul(p->vz, FX(0.8));
            continue;
        case PART_WISP:
            // Slow, wavering, slowing as it spreads.
            p->vy = fx_mul(p->vy, FX(0.985));
            p->vx = fx_mul(p->vx, FX(0.96)) + spread(f, FX(0.0015));
            p->vz = fx_mul(p->vz, FX(0.96)) + spread(f, FX(0.0015));
            continue;
        default:
            break;
        }
        if (fx_floats((ParticleKind)p->kind)) {
            // Rise and drift, slowing down.
            p->vx = fx_mul(p->vx, FX(0.94));
            p->vz = fx_mul(p->vz, FX(0.94));
            continue;
        }
        p->vy -= FX_GRAVITY;
        if (p->y <= 0 && p->vy <= 0) {
            // One small bounce, then it lies still.
            p->y = 0;
            p->vy = p->vy < -FX(0.04) ? fx_mul(-p->vy, FX(0.35)) : 0;
            p->vx = fx_mul(p->vx, FX(0.5));
            p->vz = fx_mul(p->vz, FX(0.5));
        }
    }
    for (int i = 0; i < MAX_FLOATERS; i++) {
        Floater *fl = &f->floaters[i];
        if (!fl->active)
            continue;
        fl->y += FX(0.02);
        if (0 == --fl->life)
            fl->active = false;
    }
}

int fx_active_count(const Fx *f)
{
    int n = 0;
    for (int i = 0; i < MAX_PARTICLES; i++)
        n += f->parts[i].active;
    return n;
}
