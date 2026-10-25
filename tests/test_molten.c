// The Molten Depths.

#include <string.h>

#include "test.h"
#include "roomgen.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run)
{
    run_start(run, 1);
    room_clear(&run->room);
    run->room.area = AREA_MOLTEN;
    run_reset_room(run);
    run->flood_ring = -1;
    run->player.x = run->player.safe_x = FX(6.0);
    run->player.z = run->player.safe_z = FX(6.0);
    room_path_map(&run->room, 6, 6, run->path);
}

static Enemy *ready(Run *run, EnemyType type, fx x, fx z)
{
    Enemy *e = run_spawn_enemy(run, type, x, z);
    e->state = EST_MOVE;
    e->timer = 0;
    e->cooldown = 0;
    return e;
}

static int zones_by(const Run *run, int owner)
{
    int n = 0;
    for (int i = 0; i < MAX_ZONES; i++)
        n += run->zones[i].active && run->zones[i].owner == owner;
    return n;
}

static void fire_tests(void)
{
    static Run run;
    // A fire slime bursts into flame where it dies.
    // (Another enemy stays, or clearing the room would put the fire out.)
    empty_run(&run);
    ready(&run, ENEMY_TURRET, FX(1.5), FX(1.5));
    Enemy *s = ready(&run, ENEMY_FIRE_SLIME, FX(6.8), FX(6.0));
    s->hp = 1;
    run.player.facing = DIR_E;
    PlayerInput atk = { .move = DIR_NONE, .attack = true };
    run_update(&run, &atk);
    for (int i = 0; i < PLAYER_ATTACK_FRAMES && EST_DYING != s->state; i++)
        run_update(&run, &idle);
    CHECK_EQ(s->state, EST_DYING);
    CHECK_EQ(zones_by(&run, ENEMY_FIRE_SLIME), 1);

    // A magma brute's charge leaves a trail of fire.
    empty_run(&run);
    Enemy *b = ready(&run, ENEMY_MAGMA_BRUTE, FX(1.5), FX(6.0));
    int trail = 0;
    for (int i = 0; i < 200; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        int n = zones_by(&run, ENEMY_MAGMA_BRUTE);
        trail = n > trail ? n : trail;
    }
    CHECK(b->active);
    CHECK(trail >= 2);

    // An imp throws three fireballs at once.
    empty_run(&run);
    ready(&run, ENEMY_IMP, FX(2.0), FX(6.0));
    int most = 0;
    for (int i = 0; i < 300; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        if (run.events & EV_ENEMY_SHOOT) {
            int n = 0;
            for (int k = 0; k < MAX_PROJECTILES; k++)
                n += run.shots[k].active && ENEMY_IMP == run.shots[k].owner
                  && PROJECTILE_LIFE - 1 == run.shots[k].life;
            most = n > most ? n : most;
        }
    }
    CHECK_EQ(most, 3);
}

static void crust_tests(void)
{
    static Run run;
    empty_run(&run);
    run.room.hazards[0] = (Hazard){ HAZ_CRUST, 0, FX(3.5), FX(6.5), FX(3.5),
                                    FX(6.5) };
    run.room.hazard_count = 1;
    // Whole until stepped on.
    for (int i = 0; i < 100; i++)
        run_update(&run, &idle);
    CHECK_EQ(run.crust[0], 0);
    CHECK_EQ(room_cell(&run.room, 3, 6), CELL_FLOOR);
    // Stepped on: it cracks, opens to lava, then sets again.
    run.player.x = FX(3.5);
    run.player.z = FX(6.5);
    run_update(&run, &idle);
    CHECK(run.crust[0] > 0);
    run.player.x = FX(6.0);   // step off
    run.player.z = FX(6.0);
    bool opened = false;
    for (int i = 0; i < CRUST_CRACK + CRUST_OPEN + 5; i++) {
        run_update(&run, &idle);
        opened |= CELL_PIT == room_cell(&run.room, 3, 6);
    }
    CHECK(opened);
    CHECK_EQ(room_cell(&run.room, 3, 6), CELL_FLOOR);
    CHECK_EQ(run.crust[0], 0);

    // Falling into lava: the cause says so.
    run.player.x = FX(3.5);
    run.player.z = FX(6.5);
    run.room.cells[6][3] = CELL_PIT;
    run.player.iframes = 0;
    run.player.action = PACT_IDLE;
    run.player.safe_x = FX(6.0);
    run.player.safe_z = FX(6.0);
    run_update(&run, &idle);
    CHECK_EQ(run.hurt_by, CAUSE_LAVA);
}

static void lord_tests(void)
{
    static Run run;
    run_start(&run, 77);
    run.depth = 40;
    roomgen_generate(&run.room, 4242, 40, RT_BOSS, SIDE_PZ, 6);
    run_begin_room(&run);
    CHECK_EQ(run.room.area, AREA_MOLTEN);
    Enemy *l = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (run.enemies[i].active && ENEMY_MAGMA_LORD == run.enemies[i].type)
            l = &run.enemies[i];
    CHECK(NULL != l);
    if (!l)
        return;
    bool waves = false, warned = false, flooded = false, called = false;
    for (int i = 0; i < 2400; i++) {
        run.player.iframes = 1000;
        run.player.hp = run.player.max_hp;
        if (600 == i)
            l->hp = l->max_hp * 55 / 100;   // below 60%: the outer ring
        if (1400 == i)
            l->hp = l->max_hp * 25 / 100;   // below 30%: the next ring
        run_update(&run, &idle);
        waves |= zones_by(&run, ENEMY_MAGMA_LORD) >= 3;
        warned |= 0 != (run.events2 & EV2_FLOOD_WARN);
        flooded |= 0 != (run.events2 & EV2_FLOOD);
        called |= 0 != (run.events & EV_SUMMON);
        // He never ends up standing in lava.
        CHECK(!room_over_pit(&run.room, l->x, l->z));
    }
    CHECK(waves && warned && flooded && called);
    CHECK_EQ(run.flooded, 3);
    CHECK_EQ(room_cell(&run.room, 0, 5), CELL_PIT);
    CHECK_EQ(room_cell(&run.room, 1, 5), CELL_PIT);
    CHECK_EQ(room_cell(&run.room, 2, 5), CELL_FLOOR);
    // He falls: the lava cools, the floor is back.
    l->state = EST_DYING;
    l->timer = 1;
    bool cooled = false;
    for (int i = 0; i < 5; i++) {
        run_update(&run, &idle);
        cooled |= 0 != (run.events2 & EV2_COOLED);
    }
    CHECK(cooled);   // at once, even with his fire slimes still about
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            CHECK(CELL_PIT != room_cell(&run.room, x, z));
}

static void generation_tests(void)
{
    static Room r;
    int rivers = 0, crusts = 0, natives = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) {
        roomgen_generate(&r, seed * 2654435761u, 31 + (int)(seed % 9),
                         RT_COMBAT, (WallSide)(seed % SIDE_COUNT),
                         2 + (int)(seed % 8));
        CHECK_EQ(r.area, AREA_MOLTEN);
        int pits = 0;
        for (int z = 0; z < ROOM_SIZE; z++)
            for (int x = 0; x < ROOM_SIZE; x++)
                pits += CELL_PIT == r.cells[z][x];
        rivers += pits >= 14;
        for (int i = 0; i < r.hazard_count; i++)
            crusts += HAZ_CRUST == r.hazards[i].kind;
        for (int i = 0; i < r.enemy_count; i++)
            natives += ENEMY_FIRE_SLIME == r.enemies[i].type
                    || ENEMY_MAGMA_BRUTE == r.enemies[i].type
                    || ENEMY_IMP == r.enemies[i].type;
    }
    CHECK(rivers > 60);
    CHECK(crusts > 100);
    CHECK(natives > 300);
}

void run_molten_tests(void)
{
    fire_tests();
    crust_tests();
    lord_tests();
    generation_tests();
}
