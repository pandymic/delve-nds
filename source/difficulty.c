// The difficulty curve: keyframes by depth, interpolated (difficulty.h).

#include <stdint.h>

#include "config.h"
#include "difficulty.h"
#include "fixed.h"

// The whole difficulty curve. Keyframes by depth; values in between are
// interpolated linearly, values past the last key are extrapolated gently.
// Tune here only: generation and spawning read nothing else.
//
// Unlock thresholds are expressed as zeros. Enemies: mages from depth 2,
// brutes 3, archers 4, casters 6, idols 7, shamans 8 (slimes are only ever
// summoned). Explosive barrels from depth 2, traps from depth 3 (fire jets
// 4, saws 5), elites from depth 6.
typedef struct {
    int depth;
    int enemy_count;
    int hp_pct;
    int damage_bonus;
    int elite_chance;
    uint8_t weights[ENEMY_TYPE_COUNT];
    int hazards;
    int hazard_kinds;
    int explosive_pct;
} DifficultyKey;

static const DifficultyKey curve[] = {
    // depth count  hp%  dmg  elite%
    //      { orc mage brute archer caster shaman idol slime }  traps kinds boom%
    {   1,    2,   100,  0,    0, { 100,  0,  0,  0,  0,  0,  0, 0 }, 0, 0,  0 },
    {   2,    3,   100,  0,    0, {  75, 25,  0,  0,  0,  0,  0, 0 }, 0, 0, 20 },
    {   3,    3,   105,  0,    0, {  60, 25, 15,  0,  0,  0,  0, 0 }, 1, 1, 22 },
    {   4,    4,   110,  0,    0, {  50, 25, 15, 10,  0,  0,  0, 0 }, 1, 2, 25 },
    {   5,    4,   118,  0,    0, {  45, 22, 15, 12,  0,  0,  0, 0 }, 2, 3, 27 },
    {   6,    5,   125,  0,    5, {  40, 20, 15, 12,  8,  0,  0, 0 }, 2, 3, 30 },
    {   7,    5,   132,  0,    6, {  36, 18, 14, 12,  8,  0,  6, 0 }, 2, 3, 31 },
    {   8,    5,   138,  0,    7, {  32, 16, 14, 12,  8, 10,  6, 0 }, 2, 3, 32 },
    {  10,    6,   150,  0,   10, {  28, 15, 14, 13, 10, 10,  8, 0 }, 2, 3, 35 },
    {  15,    7,   180,  1,   15, {  24, 14, 14, 14, 12, 12,  8, 0 }, 3, 3, 40 },
    {  20,    8,   220,  1,   20, {  22, 13, 15, 14, 13, 13,  8, 0 }, 3, 3, 40 },
    // +1 damage, not +2, through the Caverns and the Molten Depths; +2
    // comes on the second way down.
    {  30,   10,   300,  1,   30, {  20, 12, 16, 14, 14, 14,  8, 0 }, 4, 3, 45 },
};
#define CURVE_KEYS ARRAY_LEN(curve)

// Past the last key: per extra depth.
#define EXTRA_HP_PCT_PER_DEPTH     8
#define EXTRA_DEPTHS_PER_ENEMY     6
#define EXTRA_DEPTHS_PER_DAMAGE    15   // +2 from depth 45
#define ELITE_CHANCE_CAP           45
#define ENEMY_COUNT_CAP            10   // keep rooms readable and fast

static int lerp_int(int a, int b, int t, int span)
{
    return a + (b - a) * t / span;
}

// Like lerp_int, but a zero start means "locked": it stays 0 until the next
// key's depth, so unlock thresholds are exact.
static int lerp_unlock(int a, int b, int t, int span)
{
    return 0 == a ? 0 : lerp_int(a, b, t, span);
}


Difficulty difficulty(int depth)
{
    Difficulty d;
    if (depth < 1)
        depth = 1;

    const DifficultyKey *last = &curve[CURVE_KEYS - 1];
    if (depth >= last->depth) {
        int extra = depth - last->depth;
        d.enemy_count = last->enemy_count + extra / EXTRA_DEPTHS_PER_ENEMY;
        d.hp_pct = last->hp_pct + extra * EXTRA_HP_PCT_PER_DEPTH;
        d.damage_bonus = last->damage_bonus + extra / EXTRA_DEPTHS_PER_DAMAGE;
        d.elite_chance = last->elite_chance + extra / 2;
        for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
            d.weights[t] = last->weights[t];
        d.hazards = last->hazards;
        d.hazard_kinds = last->hazard_kinds;
        d.explosive_pct = last->explosive_pct;
    } else {
        int k = 0;
        while (curve[k + 1].depth <= depth)
            k++;
        const DifficultyKey *a = &curve[k], *b = &curve[k + 1];
        int t = depth - a->depth, span = b->depth - a->depth;
        d.enemy_count = lerp_int(a->enemy_count, b->enemy_count, t, span);
        d.hp_pct = lerp_int(a->hp_pct, b->hp_pct, t, span);
        // Damage steps only at keys: half a heart does not exist.
        d.damage_bonus = a->damage_bonus;
        d.elite_chance = lerp_unlock(a->elite_chance, b->elite_chance, t, span);
        for (int e = 0; e < ENEMY_TYPE_COUNT; e++)
            d.weights[e] = lerp_unlock(a->weights[e], b->weights[e], t, span);
        // Trap counts and kinds step at keys, like damage.
        d.hazards = a->hazards;
        d.hazard_kinds = a->hazard_kinds;
        d.explosive_pct = lerp_unlock(a->explosive_pct, b->explosive_pct, t,
                                      span);
    }

    int cap = ENEMY_COUNT_CAP < MAX_ENEMIES ? ENEMY_COUNT_CAP : MAX_ENEMIES;
    d.enemy_count = iclamp(d.enemy_count, 1, cap);
    d.hp_pct = iclamp(d.hp_pct, 100, 1000);
    d.damage_bonus = iclamp(d.damage_bonus, 0, 3);
    d.elite_chance = iclamp(d.elite_chance, 0, ELITE_CHANCE_CAP);
    for (int k = 0; k < ENEMY_TYPE_COUNT; k++)
        d.weights[k] = iclamp(d.weights[k], 0, 100);
    d.hazards = iclamp(d.hazards, 0, MAX_HAZARDS);
    d.hazard_kinds = iclamp(d.hazard_kinds, 0, 3);
    d.explosive_pct = iclamp(d.explosive_pct, 0, 100);
    return d;
}
