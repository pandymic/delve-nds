// Bosses and areas.

#include <string.h>

#include "test.h"
#include "roomgen.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void area_tests(void)
{
    CHECK_EQ(room_area(1), AREA_DUNGEON);
    CHECK_EQ(room_area(10), AREA_DUNGEON);
    CHECK_EQ(room_area(11), AREA_SEWERS);
    CHECK_EQ(room_area(20), AREA_SEWERS);
    CHECK_EQ(room_area(21), AREA_CAVERNS);
    CHECK_EQ(room_area(AREA_COUNT * AREA_DEPTHS + 1), AREA_DUNGEON);
    CHECK(depth_is_boss(10) && depth_is_boss(20) && !depth_is_boss(9));

    static Room r;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        // Before a boss: one door, into the arena.
        roomgen_generate(&r, seed * 7u, 9, RT_COMBAT, SIDE_PZ, 6);
        CHECK_EQ(r.door_count, 1);
        CHECK_EQ(r.doors[0].leads_to, RT_BOSS);
        // The arena: square, no traps, the area's boss and nobody else.
        int depth = seed & 1 ? 10 : 20;   // (the Caverns: test_caverns.c)
        roomgen_generate(&r, seed * 13u, depth, RT_BOSS,
                         (WallSide)(seed % SIDE_COUNT), 2 + (int)(seed % 8));
        CHECK_EQ(r.hazard_count, 0);
        CHECK_EQ(r.enemy_count, 1);
        CHECK_EQ(r.enemies[0].type,
                 10 == depth ? ENEMY_WARLORD : ENEMY_SLIME_KING);
        for (int z = 0; z < ROOM_SIZE; z++)
            for (int x = 0; x < ROOM_SIZE; x++)
                CHECK(CELL_VOID != room_cell(&r, x, z)
                      && CELL_PIT != room_cell(&r, x, z));
        CHECK(room_is_open(&r, FX_TO_INT(r.enemies[0].x),
                           FX_TO_INT(r.enemies[0].z)));
    }
    // Sewer rooms come out with pits or gaps more often than dungeon ones.
    int dungeon_holes = 0, sewer_holes = 0;
    for (uint32_t seed = 1; seed <= 600; seed++) {
        for (int pass = 0; pass < 2; pass++) {
            roomgen_generate(&r, seed * 31u, pass ? 15 : 5, RT_COMBAT, SIDE_PZ,
                             6);
            bool holes = false;
            for (int z = 0; z < ROOM_SIZE; z++)
                for (int x = 0; x < ROOM_SIZE; x++)
                    holes |= CELL_PIT == room_cell(&r, x, z);
            if (pass)
                sewer_holes += holes;
            else
                dungeon_holes += holes;
        }
    }
    CHECK(sewer_holes > dungeon_holes);
}

static void boss_run(Run *run, int depth)
{
    run_start(run, 77);
    run->depth = depth;
    roomgen_generate(&run->room, 4242, depth, RT_BOSS, SIDE_PZ, 6);
    run_begin_room(run);
}

static Enemy *the_boss(Run *run)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (run->enemies[i].active && enemy_is_boss(run->enemies[i].type))
            return &run->enemies[i];
    return NULL;
}

static void warlord_tests(void)
{
    static Run run;
    boss_run(&run, 10);
    Enemy *b = the_boss(&run);
    CHECK(NULL != b);
    if (!b)
        return;
    CHECK_EQ(b->type, ENEMY_WARLORD);
    bool charged = false;
    int ring = 0, summoned = 0;
    for (int i = 0; i < 2400; i++) {
        run.player.iframes = 1000;
        run.player.hp = run.player.max_hp;
        if (1200 == i)
            b->hp = b->max_hp / 2;   // enraged
        run_update(&run, &idle);
        charged |= 0 != (run.events & EV_CHARGE);
        int shots = 0;
        for (int k = 0; k < MAX_PROJECTILES; k++)
            shots += run.shots[k].active;
        if (shots >= 8)
            ring++;
        if (run.events & EV_SUMMON)
            summoned++;
    }
    CHECK(charged);
    CHECK(ring > 0);
    CHECK(summoned > 0);
    CHECK(b->active);   // the player never hit it
}

static void king_tests(void)
{
    static Run run;
    boss_run(&run, 20);
    Enemy *b = the_boss(&run);
    CHECK(NULL != b);
    if (!b)
        return;
    CHECK_EQ(b->type, ENEMY_SLIME_KING);
    // Mid-leap he cannot be hit; landings burst.
    bool leapt = false, landed = false, immune = false;
    for (int i = 0; i < 1200 && !(leapt && landed); i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        if (EST_ATTACK == b->state) {
            leapt = true;
            int hp = b->hp;
            // A dagger thrown straight at him passes harmlessly.
            run.player.x = b->x - FX(0.8);
            run.player.z = b->z;
            PlayerInput thr = { .move = DIR_SE, .throw_dagger = true };
            run_update(&run, &thr);
            immune |= b->hp == hp && EST_ATTACK == b->state;
        }
        landed |= 0 != (run.events & EV_SLAM);
    }
    CHECK(leapt);
    CHECK(landed);
    CHECK(immune);
    // Losing health splits slimes off.
    b->state = EST_MOVE;
    b->cooldown = 1000;
    b->hp = b->max_hp * 3 / 5;   // two fifths lost
    run_update(&run, &idle);
    int slimes = 0;
    for (int k = 0; k < MAX_ENEMIES; k++)
        slimes += run.enemies[k].active && ENEMY_SLIME == run.enemies[k].type;
    CHECK(slimes >= 2);
}

static void reward_tests(void)
{
    static Run run;
    boss_run(&run, 10);
    Enemy *b = the_boss(&run);
    if (!b)
        return;
    int gold = run.gold;
    b->state = EST_DYING;
    b->timer = 1;
    bool down = false;
    for (int i = 0; i < 5; i++) {
        run_update(&run, &idle);
        down |= 0 != (run.events2 & EV2_BOSS_DOWN);
    }
    CHECK(down);
    CHECK_EQ(run.status, ROOM_CLEAR);
    CHECK_EQ(run.gold - gold, REWARD_BOSS_GOLD(10));
    CHECK(run.boon_pending);
    int hearts = 0;
    for (int i = 0; i < MAX_PICKUPS; i++)
        hearts += run.pickups[i].active && PICK_HEART == run.pickups[i].kind;
    CHECK_EQ(hearts, 1);
}

void run_bosses_tests(void)
{
    area_tests();
    warlord_tests();
    king_tests();
    reward_tests();
}
