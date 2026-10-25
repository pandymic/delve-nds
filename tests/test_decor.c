// Room dressing: torches, clutter and decals.

#include <string.h>

#include "test.h"
#include "difficulty.h"
#include "roomgen.h"
#include "run.h"

static bool near_door(const Room *r, WallSide side, int pos)
{
    for (int i = 0; i < r->door_count; i++)
        if (r->doors[i].side == side && pos >= r->doors[i].pos - 1
            && pos <= r->doors[i].pos + 1)
            return true;
    return r->entry_side == side && pos >= r->entry_pos - 1
        && pos <= r->entry_pos + 1;
}

// Traps: on open floor, away from the player's spawn and the doorways,
// never in rest rooms, as many as the difficulty allows.
static int hazards_seen[HAZ_KIND_COUNT];

static void check_hazards(const Room *r, int depth)
{
    Difficulty d = difficulty(depth);
    CHECK(r->hazard_count <= d.hazards);
    if (RT_REST == r->type)
        CHECK_EQ(r->hazard_count, 0);
    int sx = FX_TO_INT(r->spawn_x), sz = FX_TO_INT(r->spawn_z);
    for (int i = 0; i < r->hazard_count; i++) {
        const Hazard *h = &r->hazards[i];
        // One of the kinds the area puts in the unlocked trap slots.
        bool allowed = false;
        for (int k = 0; k < d.hazard_kinds; k++)
            allowed |= area_defs[r->area].traps[k] == h->kind;
        CHECK(allowed);
        hazards_seen[h->kind]++;
        int x0 = FX_TO_INT(h->x), z0 = FX_TO_INT(h->z);
        int x1 = FX_TO_INT(h->x2), z1 = FX_TO_INT(h->z2);
        CHECK(x0 == x1 || z0 == z1);   // saw tracks are straight
        if (HAZ_SAW != h->kind)
            CHECK(x0 == x1 && z0 == z1);
        int n = (x1 - x0) + (z1 - z0) + 1;
        for (int k = 0; k < n; k++) {
            int x = x0 + (x1 > x0 ? k : 0), z = z0 + (z1 > z0 ? k : 0);
            CHECK_EQ(room_cell(r, x, z), CELL_FLOOR);
            CHECK(!(x >= sx - 2 && x <= sx + 2 && z >= sz - 2 && z <= sz + 2));
            for (int j = 0; j < r->door_count; j++) {
                int dx, dz;
                door_cell(r->doors[j].side, r->doors[j].pos, &dx, &dz);
                CHECK(!(x == dx && z == dz));
            }
        }
    }
}

static void generation_tests(void)
{
    static Room a, b;
    int total_torches = 0, total_pots = 0;
    for (uint32_t seed = 1; seed <= 3000; seed++) {
        RoomType type = (RoomType)(seed % RT_COUNT);
        WallSide entry = (WallSide)(seed % SIDE_COUNT);
        roomgen_generate(&a, seed * 2654435761u, 1 + (int)(seed % 30), type,
                         entry, 2 + (int)(seed % 8));
        CHECK(a.decor_count <= MAX_DECOR);
        int torches = 0;
        for (int i = 0; i < a.decor_count; i++) {
            const Decor *d = &a.decor[i];
            CHECK(d->kind < DECOR_KIND_COUNT);
            CHECK(d->x >= 0 && d->x <= FX_INT(ROOM_SIZE));
            CHECK(d->z >= 0 && d->z <= FX_INT(ROOM_SIZE));
            if (decor_is_wall_mounted(d->kind)) {
                // Back walls only, never at a doorway.
                CHECK(SIDE_NX == d->wall || SIDE_NZ == d->wall);
                int pos = FX_TO_INT(SIDE_NX == d->wall ? d->z : d->x);
                CHECK(!near_door(&a, d->wall, pos));
                torches += DECOR_TORCH == d->kind;
            } else {
                CHECK_EQ(d->wall, SIDE_NONE);
                // On open floor (decals and clutter alike).
                int cx = FX_TO_INT(d->x), cz = FX_TO_INT(d->z);
                CHECK_EQ(room_cell(&a, cx, cz), CELL_FLOOR);
                total_pots += DECOR_POT == d->kind;
            }
            if (DECOR_POT == d->kind || DECOR_BARREL == d->kind) {
                // Clutter never stands in a doorway's approach.
                for (int j = 0; j < a.door_count; j++) {
                    int dx, dz;
                    door_cell(a.doors[j].side, a.doors[j].pos, &dx, &dz);
                    CHECK(!(FX_TO_INT(d->x) == dx && FX_TO_INT(d->z) == dz));
                }
            }
        }
        CHECK(torches <= MAX_TORCH_LIGHTS);
        total_torches += torches;
        check_hazards(&a, 1 + (int)(seed % 30));

        // Same seed, same room, dressing included.
        roomgen_generate(&b, seed * 2654435761u, 1 + (int)(seed % 30), type,
                         entry, 2 + (int)(seed % 8));
        CHECK(0 == memcmp(&a, &b, sizeof(a)));
    }
    // Dressing and traps actually show up.
    CHECK(total_torches > 3000);
    CHECK(total_pots > 1000);
    // (Depths 1-30 here: crust is only in the Molten Depths, 31+.)
    for (int k = 0; k < HAZ_KIND_COUNT; k++)
        if (HAZ_CRUST != k)
            CHECK(hazards_seen[k] > 100);
}

static void pot_tests(void)
{
    static Run run;
    int with_gold = 0, pots = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        run_start(&run, seed);
        Room *r = &run.room;
        for (int i = 0; i < r->decor_count; i++) {
            Decor *d = &r->decor[i];
            if (DECOR_POT != d->kind)
                continue;
            pots++;
            // Stand next to the pot, facing it, and swing.
            run.player.x = d->x - FX(0.6);
            run.player.z = d->z;
            run.player.facing = DIR_SE;   // +x on the ground
            run.player.action = PACT_IDLE;
            int gold = run.gold;
            PlayerInput in = { .move = DIR_NONE, .attack = true };
            bool broke = false;
            for (int t = 0; t < PLAYER_ATTACK_FRAMES + 2; t++) {
                run_update(&run, &in);
                in.attack = false;
                broke |= 0 != (run.events & EV_POT_BREAK);
            }
            CHECK(broke);
            CHECK(run.decor_broken & (1u << i));
            bool has = run_pot_has_gold(&run, i);
            with_gold += has;
            // Gold pops out as a coin, settles, and is drawn in.
            for (int t = 0; t < 60; t++)
                run_update(&run, &in);
            CHECK_EQ(run.gold - gold, has ? REWARD_POT_GOLD : 0);
            // A broken pot stays broken.
            int before = run.gold;
            in.attack = true;
            for (int t = 0; t < PLAYER_ATTACK_FRAMES + 2; t++) {
                run_update(&run, &in);
                in.attack = false;
            }
            CHECK_EQ(run.gold, before);
            break; // one pot per room is enough
        }
    }
    CHECK(pots > 50);
    CHECK(with_gold > pots / 5 && with_gold < pots * 3 / 4);
}

void run_decor_tests(void)
{
    generation_tests();
    pot_tests();
}
