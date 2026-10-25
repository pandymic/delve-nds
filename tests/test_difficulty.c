// The difficulty curve and enemy unlock depths.

#include "test.h"
#include "config.h"
#include "difficulty.h"
#include "roomgen.h"

void run_difficulty_tests(void)
{
    Difficulty d1 = difficulty(1);
    CHECK_EQ(d1.weights[ENEMY_SHOOTER], 0);   // Mages unlock at depth 2
    CHECK_EQ(d1.elite_chance, 0);
    CHECK_EQ(d1.hp_pct, 100);
    CHECK_EQ(d1.damage_bonus, 0);
    CHECK(difficulty(2).weights[ENEMY_SHOOTER] > 0);
    // Exact unlock depths for the Dungeon's roster; slimes are never rolled.
    static const int unlock[ENEMY_TYPE_COUNT] = {
        [ENEMY_CHASER] = 1, [ENEMY_SHOOTER] = 2, [ENEMY_BRUTE] = 3,
        [ENEMY_ARCHER] = 4, [ENEMY_CASTER] = 6, [ENEMY_SHAMAN] = 8,
        [ENEMY_TURRET] = 7, [ENEMY_SLIME] = 1000, [ENEMY_DUMMY] = 1000,
        [ENEMY_WARLORD] = 1000, [ENEMY_SLIME_KING] = 1000,
        // Area enemies (area.c) never come from the depth table.
        [ENEMY_BAT] = 1000, [ENEMY_DIGGER] = 1000, [ENEMY_CRYSTAL] = 1000,
        [ENEMY_GOLEM] = 1000, [ENEMY_FIRE_SLIME] = 1000,
        [ENEMY_MAGMA_BRUTE] = 1000, [ENEMY_IMP] = 1000,
        [ENEMY_MAGMA_LORD] = 1000,
    };
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++) {
        for (int depth = 1; depth <= 40; depth++) {
            bool on = difficulty(depth).weights[t] > 0;
            CHECK_EQ(on, depth >= unlock[t]);
        }
    }
    // Traps and explosive barrels unlock at exact depths.
    CHECK_EQ(difficulty(2).hazards, 0);
    CHECK_EQ(difficulty(3).hazards, 1);
    CHECK_EQ(difficulty(3).hazard_kinds, 1);
    CHECK_EQ(difficulty(4).hazard_kinds, 2);
    CHECK_EQ(difficulty(5).hazard_kinds, 3);
    CHECK_EQ(d1.explosive_pct, 0);
    CHECK(difficulty(2).explosive_pct > 0);
    CHECK_EQ(difficulty(5).elite_chance, 0);
    CHECK(difficulty(6).elite_chance > 0);
    CHECK_EQ(difficulty(0).enemy_count, d1.enemy_count); // clamps

    // Every knob is non-decreasing with depth and bounded.
    Difficulty prev = d1;
    for (int depth = 2; depth <= 300; depth++) {
        Difficulty d = difficulty(depth);
        CHECK(d.enemy_count >= prev.enemy_count);
        CHECK(d.hp_pct >= prev.hp_pct);
        CHECK(d.damage_bonus >= prev.damage_bonus);
        CHECK(d.elite_chance >= prev.elite_chance);
        CHECK(d.weights[ENEMY_SHOOTER] >= 0);
        CHECK(d.hazards >= prev.hazards && d.hazards <= MAX_HAZARDS);
        CHECK(d.explosive_pct >= prev.explosive_pct);
        CHECK(d.enemy_count >= 1 && d.enemy_count <= MAX_ENEMIES);
        CHECK(d.elite_chance <= 45);
        CHECK(d.damage_bonus <= 3);
        prev = d;
    }
    // Absurd depths stay sane.
    Difficulty far = difficulty(10000);
    CHECK(far.enemy_count <= MAX_ENEMIES);
    CHECK(far.hp_pct <= 1000);

    // The ramp is slow: depth 10 is not more than 3x depth 1 enemies.
    CHECK(difficulty(10).enemy_count <= 3 * d1.enemy_count);

    // Generated combat rooms follow the curve (minus failed placements).
    static Room r;
    for (int depth = 1; depth <= 40; depth += 3) {
        roomgen_generate(&r, roomgen_seed(5, depth, 0), depth, RT_COMBAT,
                         SIDE_PZ, 6);
        CHECK(r.enemy_count <= difficulty(depth).enemy_count);
        CHECK(r.enemy_count >= difficulty(depth).enemy_count - 1);
        for (int i = 0; i < r.enemy_count; i++) {
            if (1 == depth)
                CHECK_EQ(r.enemies[i].type, 0); // Chasers only
            if (depth < 6)
                CHECK_EQ(r.enemies[i].elite, 0);
        }
    }
}
