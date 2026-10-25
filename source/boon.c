// Boons: the table, seeded offers, and their effects on the player (boon.h).

#include "boon.h"
#include "cheat.h"
#include "meta.h"
#include "rng.h"
#include "run.h"

const BoonDef boon_defs[BOON_COUNT] = {
    [BOON_KEEN_BLADE]   = { "Keen Blade",   "Sword deals +1 damage",     2 },
    [BOON_WIDE_ARC]     = { "Wide Arc",     "Sword reaches 25% further", 2 },
    [BOON_TWIN_DAGGERS] = { "Twin Daggers", "+1 dagger to throw",        2 },
    [BOON_PIERCING]     = { "Piercing",     "Daggers pierce +1 enemy",   2 },
    [BOON_QUICK_DRAW]   = { "Quick Draw",   "Daggers return 25% faster", 2 },
    [BOON_FLAME_DASH]   = { "Flame Dash",   "Dashing leaves fire",       1 },
    [BOON_SHOCK_DASH]   = { "Shock Dash",   "Dash ends in a shockwave",  1 },
    [BOON_EXTRA_DASH]   = { "Fleet Foot",   "+1 dash charge",            1 },
    [BOON_OVERCHARGE]   = { "Overcharge",   "Blast needs 3 fewer hits",  2 },
    [BOON_BIG_BANG]     = { "Big Bang",     "Blast: wider, +1 damage",   2 },
    [BOON_VAMPIRIC]     = { "Vampiric",     "Kills may drop a heart",    2 },
    [BOON_THICK_SKIN]   = { "Thick Skin",   "+1 max heart, healed",      3 },
    [BOON_GOLD_RUSH]    = { "Gold Rush",    "+50% gold",                 2 },
};

_Static_assert(BOON_COUNT <= RUN_BOON_SLOTS, "run history boon slots");

#define OVERCHARGE_STEP 3
#define METER_MIN       4
#define BIG_BANG_RADIUS FX(0.6)

int boon_offer(const Run *run, BoonId out[BOON_CHOICES])
{
    BoonId pool[BOON_COUNT];
    int n = 0;
    for (int i = 0; i < BOON_COUNT; i++)
        if (run->boons[i] < boon_defs[i].max)
            pool[n++] = (BoonId)i;
    // Seeded shuffle: the same run, depth and room give the same offer.
    Rng rng;
    rng_seed(&rng, rng_hash3(run->seed, (uint32_t)run->depth,
                             0xB00u + (uint32_t)run->rooms_cleared));
    for (int i = n - 1; i > 0; i--) {
        int j = (int)rng_range(&rng, (uint32_t)i + 1);
        BoonId t = pool[i];
        pool[i] = pool[j];
        pool[j] = t;
    }
    int count = n < BOON_CHOICES ? n : BOON_CHOICES;
    for (int i = 0; i < count; i++)
        out[i] = pool[i];
    return count;
}

void boon_apply(Run *run, BoonId id)
{
    if ((unsigned)id >= BOON_COUNT || run->boons[id] >= boon_defs[id].max)
        return;
    run->boons[id]++;
    Player *p = &run->player;
    switch (id) {
    case BOON_KEEN_BLADE:
        p->attack_damage++;
        break;
    case BOON_WIDE_ARC:
        p->attack_radius += PLAYER_ATTACK_RADIUS / 4;
        break;
    case BOON_TWIN_DAGGERS:
        p->dagger_max++;
        p->daggers++;
        break;
    case BOON_PIERCING:
        p->dagger_pierce++;
        break;
    case BOON_QUICK_DRAW:
        p->dagger_recharge_time = p->dagger_recharge_time * 3 / 4;
        break;
    case BOON_EXTRA_DASH:
        p->dash_max++;
        p->dash_charges++;
        break;
    case BOON_OVERCHARGE:
        p->meter_max -= OVERCHARGE_STEP;
        if (p->meter_max < METER_MIN)
            p->meter_max = METER_MIN;
        if (p->meter > p->meter_max)
            p->meter = p->meter_max;
        break;
    case BOON_BIG_BANG:
        p->blast_radius += BIG_BANG_RADIUS;
        p->blast_damage++;
        break;
    case BOON_THICK_SKIN:
        p->max_hp++;
        p->hp++;
        break;
    default:
        break;   // Flame/Shock Dash, Vampiric, Gold Rush act in run.c
    }
}

int boon_gold(const Run *run, int amount)
{
    int pct = GOLD_RUSH_PCT * run->boons[BOON_GOLD_RUSH]
            + (run->deep_pockets ? DEEP_POCKETS_PCT : 0);
    int gold = amount + amount * pct / 100;
    return cheat_on(run->cheats, CHEAT_GOLD) ? gold * CHEAT_GOLD_MULT : gold;
}
