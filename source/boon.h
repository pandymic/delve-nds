// Boons: run upgrades. After each cleared combat room the player picks one
// of three. They stack up to a limit and last until the run ends. Pure
// logic; the offer is seeded, so a run's seed reproduces its choices.

#ifndef DELVE_BOON_H
#define DELVE_BOON_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOON_KEEN_BLADE,     // sword +1 damage
    BOON_WIDE_ARC,       // sword reach +25%
    BOON_TWIN_DAGGERS,   // +1 dagger charge
    BOON_PIERCING,       // daggers pass through one more enemy
    BOON_QUICK_DRAW,     // daggers recharge faster
    BOON_FLAME_DASH,     // dashing leaves a trail of fire
    BOON_SHOCK_DASH,     // the end of a dash knocks enemies back
    BOON_EXTRA_DASH,     // +1 dash charge
    BOON_OVERCHARGE,     // the blast meter needs fewer hits
    BOON_BIG_BANG,       // a wider, harder blast
    BOON_VAMPIRIC,       // kills sometimes drop a heart
    BOON_THICK_SKIN,     // +1 max heart, healed
    BOON_GOLD_RUSH,      // +50% gold
    BOON_COUNT
} BoonId;

typedef struct {
    const char *name;
    const char *desc;    // one line, up to 26 characters
    uint8_t max;         // stacks
} BoonDef;

extern const BoonDef boon_defs[BOON_COUNT];

#define BOON_CHOICES 3

// Effects (applied by boon_apply / read by run.c).
#define FLAME_LIFE        70     // frames a flame from Flame Dash burns
#define FLAME_RADIUS      FX(0.4)
#define FLAME_DAMAGE      1
#define FLAME_EVERY       3      // frames between flames while dashing
#define SHOCK_RADIUS      FX(1.6)
#define SHOCK_DAMAGE      1
#define SHOCK_KNOCKBACK   FX(0.3)
#define VAMPIRIC_PCT      10     // per stack, chance a kill drops a heart
#define GOLD_RUSH_PCT     50     // per stack, extra gold

struct Run;

// Fills out[] with up to BOON_CHOICES distinct boons not yet at their
// limit; returns how many (fewer only when almost everything is maxed).
int boon_offer(const struct Run *run, BoonId out[BOON_CHOICES]);

// Takes a boon: one more stack and its effect.
void boon_apply(struct Run *run, BoonId id);

// Gold scaled by Gold Rush.
int boon_gold(const struct Run *run, int amount);

#endif // DELVE_BOON_H
