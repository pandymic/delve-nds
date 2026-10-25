// The enemy roster.

#include <string.h>

#include "test.h"
#include "difficulty.h"
#include "roomgen.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run)
{
    run_start(run, 1);
    room_clear(&run->room);
    run_reset_room(run);
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

static void step_n(Run *run, const PlayerInput *in, int n)
{
    for (int i = 0; i < n; i++)
        run_update(run, in);
}

static void brute_tests(void)
{
    static Run run;
    // A charge towards the player that misses (the player dodges by
    // being invulnerable and moved aside) ends against the wall.
    empty_run(&run);
    run.player.x = FX(9.0);
    run.player.z = FX(6.0);
    Enemy *b = ready(&run, ENEMY_BRUTE, FX(5.0), FX(6.0));
    bool charged = false, slammed = false;
    for (int i = 0; i < 300 && !slammed; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        charged |= 0 != (run.events & EV_CHARGE);
        slammed |= 0 != (run.events & EV_SLAM);
        if (charged)
            run.player.z = FX(1.0);   // step out of the way
    }
    CHECK(charged);
    CHECK(slammed);
    CHECK(b->stunned);
    CHECK_EQ(b->state, EST_RECOVER);
    // Dazed: double damage.
    int hp = b->hp;
    run.player.x = b->x - FX(0.6);
    run.player.z = b->z;
    PlayerInput atk = { .move = DIR_SE, .attack = true };
    run_update(&run, &atk);
    step_n(&run, &idle, PLAYER_ATTACK_FRAMES);
    CHECK_EQ(b->hp, hp - 2 * run.player.attack_damage);

    // Charging over a pit is the end of it.
    empty_run(&run);
    run.player.x = FX(10.0);
    run.player.z = FX(6.5);
    for (int x = 6; x <= 7; x++)
        for (int z = 0; z < ROOM_SIZE; z++)
            run.room.cells[z][x] = CELL_PIT;
    room_path_map(&run.room, 10, 6, run.path);
    Enemy *c = ready(&run, ENEMY_BRUTE, FX(3.0), FX(6.5));
    Enemy *keep = run_spawn_enemy(&run, ENEMY_CHASER, FX(11.5), FX(11.5));
    keep->state = EST_RECOVER;
    keep->timer = 100000;
    bool fell = false;
    for (int i = 0; i < 300 && c->active; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        fell |= 0 != (run.events & EV_ENEMY_FELL);
    }
    CHECK(fell);
    CHECK(!c->active);
}

static void archer_tests(void)
{
    static Run run;
    empty_run(&run);
    Enemy *a = ready(&run, ENEMY_ARCHER, FX(2.0), FX(2.0));
    int arrows = 0;
    fx closest = FX(99.0);
    for (int i = 0; i < 900; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        if (run.events & EV_ENEMY_SHOOT) {
            for (int k = 0; k < MAX_PROJECTILES; k++)
                if (run.shots[k].active && PROJ_ARROW == run.shots[k].kind)
                    arrows++;
        }
        if (i > 300) {
            fx dx = a->x - run.player.x, dz = a->z - run.player.z;
            fx d = fx_sqrt(fx_mul(dx, dx) + fx_mul(dz, dz));
            if (d < closest)
                closest = d;
        }
    }
    CHECK(arrows > 0);
    CHECK(closest > ARCHER_NEAR - FX(1.0));   // keeps its distance
}

static void caster_tests(void)
{
    static Run run;
    empty_run(&run);
    ready(&run, ENEMY_CASTER, FX(1.0), FX(1.0));
    bool marked = false;
    for (int i = 0; i < 200 && !marked; i++) {
        run_update(&run, &idle);
        marked = 0 != (run.events & EV_ZONE_MARKED);
    }
    CHECK(marked);
    const Zone *z = NULL;
    for (int i = 0; i < MAX_ZONES; i++)
        if (run.zones[i].active)
            z = &run.zones[i];
    CHECK(NULL != z);
    if (!z)
        return;
    CHECK_EQ(z->x, run.player.x);
    CHECK_EQ(z->z, run.player.z);
    // Standing still in the mark hurts.
    int hp = run.player.hp;
    run.player.iframes = 0;
    bool burst = false;
    for (int i = 0; i < ZONE_TELEGRAPH + 2 && !burst; i++) {
        run_update(&run, &idle);
        burst = 0 != (run.events & EV_ZONE_BURST);
    }
    CHECK(burst);
    CHECK_EQ(run.player.hp, hp - ZONE_DAMAGE);

    // Stepping out of it is safe.
    empty_run(&run);
    run_spawn_zone(&run, FX(6.0), FX(6.0));
    Enemy *keep = run_spawn_enemy(&run, ENEMY_CHASER, FX(11.5), FX(11.5));
    keep->state = EST_RECOVER;
    keep->timer = 100000;
    run.player.x = FX(9.0);
    step_n(&run, &idle, ZONE_TELEGRAPH + 2);
    CHECK_EQ(run.player.hp, PLAYER_START_HP);
}

static void shaman_tests(void)
{
    static Run run;
    empty_run(&run);
    Enemy *s = ready(&run, ENEMY_SHAMAN, FX(2.0), FX(2.0));
    int self = (int)(s - run.enemies);
    for (int i = 0; i < 2000; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        int mine = 0;
        for (int k = 0; k < MAX_ENEMIES; k++) {
            const Enemy *e = &run.enemies[k];
            if (e->active && ENEMY_SLIME == e->type && EST_DYING != e->state) {
                CHECK_EQ(e->owner, self);
                mine++;
            }
        }
        CHECK(mine <= SHAMAN_MAX_SLIMES);
    }
    int slimes = 0;
    for (int k = 0; k < MAX_ENEMIES; k++)
        slimes += run.enemies[k].active && ENEMY_SLIME == run.enemies[k].type;
    CHECK(slimes > 0);

    // Slimes hurt by touch.
    empty_run(&run);
    ready(&run, ENEMY_SLIME, FX(7.0), FX(6.0));
    run.player.iframes = 0;
    step_n(&run, &idle, 120);
    CHECK(run.player.hp < PLAYER_START_HP);
}

static void turret_tests(void)
{
    static Run run;
    empty_run(&run);
    Enemy *t = ready(&run, ENEMY_TURRET, FX(3.5), FX(3.5));
    fx x = t->x, z = t->z;
    int volleys = 0, shots = 0;
    for (int i = 0; i < 600; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        if (run.events & EV_ENEMY_SHOOT) {
            volleys++;
            int n = 0;
            for (int k = 0; k < MAX_PROJECTILES; k++)
                n += run.shots[k].active;
            shots = n;
        }
        // Knock it about: it does not move.
        if (100 == i) {
            run.player.x = FX(4.3);
            run.player.z = FX(3.5);
            PlayerInput atk = { .move = DIR_NW, .attack = true };
            run_update(&run, &atk);
        }
    }
    CHECK(volleys >= 3);
    CHECK(shots >= 4);
    CHECK_EQ(t->x, x);
    CHECK_EQ(t->z, z);
}

static void generation_tests(void)
{
    static Room r;
    for (uint32_t seed = 1; seed <= 3000; seed++) {
        int depth = 1 + (int)(seed % 25);
        roomgen_generate(&r, seed * 2654435761u, depth, RT_COMBAT,
                         (WallSide)(seed % SIDE_COUNT), 2 + (int)(seed % 8));
        Difficulty d = difficulty(depth);
        int turrets = 0, shamans = 0;
        for (int i = 0; i < r.enemy_count; i++) {
            int t = r.enemies[i].type;
            // Only unlocked types, or the area's own (area.c).
            const AreaDef *a = &area_defs[r.area];
            CHECK(d.weights[t] * a->pct[t] / 100 + a->native[t] > 0);
            CHECK(ENEMY_SLIME != t);
            turrets += ENEMY_TURRET == t;
            shamans += ENEMY_SHAMAN == t;
        }
        CHECK(turrets <= 2 && shamans <= 2);
    }
}

void run_roster_tests(void)
{
    brute_tests();
    archer_tests();
    caster_tests();
    shaman_tests();
    turret_tests();
    generation_tests();
}
