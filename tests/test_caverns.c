// The Crystal Caverns.

#include <string.h>

#include "test.h"
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

static void bat_tests(void)
{
    static Run run;
    empty_run(&run);
    // A pit between the bat and the player: it flies across, and dives.
    for (int z = 0; z < ROOM_SIZE; z++)
        run.room.cells[z][3] = CELL_PIT;
    Enemy *b = ready(&run, ENEMY_BAT, FX(1.5), FX(6.0));
    bool crossed = false, dived = false, hurt = false;
    for (int i = 0; i < 400; i++) {
        run_update(&run, &idle);
        crossed |= b->x > FX(4.0);
        dived |= EST_ATTACK == b->state;
        hurt |= 0 != (run.events & EV_PLAYER_HURT);
        run.player.iframes = run.player.iframes > 0 ? run.player.iframes : 0;
        run.player.hp = run.player.max_hp;
    }
    CHECK(b->active);   // never fell
    CHECK(crossed);
    CHECK(dived);
    CHECK(hurt);
}

static void digger_tests(void)
{
    static Run run;
    empty_run(&run);
    Enemy *d = ready(&run, ENEMY_DIGGER, FX(2.0), FX(2.0));
    // Underground it cannot be hit: a sword swing right on it does nothing.
    run.player.x = d->x + FX(0.5);
    run.player.z = d->z;
    run.player.facing = DIR_W;
    CHECK(enemy_buried(d));
    int hp = d->hp;
    PlayerInput atk = { .move = DIR_NONE, .attack = true };
    run_update(&run, &atk);
    for (int i = 0; i < PLAYER_ATTACK_FRAMES; i++)
        run_update(&run, &idle);
    CHECK_EQ(d->hp, hp);
    // It surfaces by the player and bursts out.
    bool burst = false;
    run.player.hp = run.player.max_hp;
    run.player.iframes = 0;
    for (int i = 0; i < 120 && !burst; i++) {
        run_update(&run, &idle);
        burst |= EST_ATTACK == d->state;
    }
    CHECK(burst);
    CHECK(run.player.hp < run.player.max_hp);   // too close: hurt
    // Above ground now: it can be hit; later it burrows again.
    CHECK(!enemy_buried(d));
    int ticks = 0;
    while (!enemy_buried(d) && ticks++ < 200) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
    }
    CHECK(enemy_buried(d));
}

static void crystal_tests(void)
{
    static Run run;
    empty_run(&run);
    Enemy *c = ready(&run, ENEMY_CRYSTAL, FX(3.5), FX(3.5));
    fx x = c->x, z = c->z;
    int volleys = 0;
    fx first_vx = 0;
    bool turned = false;
    for (int i = 0; i < 500; i++) {
        run.player.iframes = 1000;
        run_update(&run, &idle);
        if (!(run.events & EV_ENEMY_SHOOT))
            continue;
        int shards = 0;
        fx vx = 0;
        for (int k = 0; k < MAX_PROJECTILES; k++) {
            const Projectile *s = &run.shots[k];
            if (s->active && PROJ_SHARD == s->kind
                && PROJECTILE_LIFE - 1 == s->life) {   // moved once already
                shards++;
                if (!vx)
                    vx = s->vx;
            }
        }
        CHECK_EQ(shards, 6);
        if (0 == volleys++)
            first_vx = vx;
        else
            turned |= vx != first_vx;
        // Hitting it does not move it.
        run.player.x = c->x + FX(0.6);
        run.player.z = c->z;
    }
    CHECK(volleys >= 3);
    CHECK(turned);
    CHECK_EQ(c->x, x);
    CHECK_EQ(c->z, z);
}

static void golem_tests(void)
{
    static Run run;
    run_start(&run, 77);
    run.depth = 30;
    roomgen_generate(&run.room, 4242, 30, RT_BOSS, SIDE_PZ, 6);
    run_begin_room(&run);
    Enemy *g = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (run.enemies[i].active && ENEMY_GOLEM == run.enemies[i].type)
            g = &run.enemies[i];
    CHECK(NULL != g);
    if (!g)
        return;
    bool hop = false, slam = false, rain = false, called = false, shards = false;
    for (int i = 0; i < 2400; i++) {
        run.player.iframes = 1000;
        run.player.hp = run.player.max_hp;
        if (1200 == i)
            g->hp = g->max_hp / 2;   // enraged: calls crystals
        run_update(&run, &idle);
        hop |= EST_ATTACK == g->state;
        slam |= 0 != (run.events & EV_SLAM);
        rain |= 0 != (run.events & EV_ZONE_MARKED);
        called |= 0 != (run.events & EV_SUMMON);
        for (int k = 0; k < MAX_PROJECTILES; k++)
            shards |= run.shots[k].active && PROJ_SHARD == run.shots[k].kind;
    }
    CHECK(hop && slam && rain && called && shards);
}

static void rockfall_tests(void)
{
    static Run run;
    empty_run(&run);
    Hazard *h = &run.room.hazards[0];
    *h = (Hazard){ HAZ_ROCKFALL, 0, FX(6.5), FX(6.5), FX(6.5), FX(6.5) };
    run.room.hazard_count = 1;
    run.player.x = FX(6.5);
    run.player.z = FX(6.5);
    // Standing in the shadow costs a heart, once, when the rock lands.
    int hits = 0;
    for (uint32_t f = 0; f < ROCK_CYCLE; f++) {
        run.frames = f;
        int t;
        TrapState st = hazard_state(h, f, &t);
        if (f < ROCK_WARN)
            CHECK_EQ(st, TRAP_IDLE);
        else if (f < ROCK_HIT)
            CHECK_EQ(st, TRAP_WARN);
        int hp = run.player.hp;
        run_update(&run, &idle);
        if (run.player.hp < hp) {
            hits++;
            CHECK_EQ(run.hurt_by, CAUSE_ROCKFALL);
        }
    }
    CHECK_EQ(hits, 1);
}

static void generation_tests(void)
{
    static Room r;
    int caves = 0, crystals = 0, torches = 0, rockfalls = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) {
        roomgen_generate(&r, seed * 2654435761u, 21 + (int)(seed % 9),
                         RT_COMBAT, (WallSide)(seed % SIDE_COUNT),
                         2 + (int)(seed % 8));
        CHECK_EQ(r.area, AREA_CAVERNS);
        int voids = 0;
        for (int z = 0; z < ROOM_SIZE; z++)
            for (int x = 0; x < ROOM_SIZE; x++)
                voids += CELL_VOID == r.cells[z][x];
        caves += voids > 8;
        for (int i = 0; i < r.decor_count; i++) {
            crystals += DECOR_CRYSTAL == r.decor[i].kind;
            torches += DECOR_TORCH == r.decor[i].kind
                    || DECOR_BANNER == r.decor[i].kind
                    || DECOR_GRATE == r.decor[i].kind;
        }
        for (int i = 0; i < r.hazard_count; i++)
            rockfalls += HAZ_ROCKFALL == r.hazards[i].kind;
    }
    CHECK(caves > 80);       // most rooms are ragged caves
    CHECK(crystals > 200);   // lit by crystals...
    CHECK_EQ(torches, 0);    // ...not torches, banners or grates
    CHECK(rockfalls > 100);
}

void run_caverns_tests(void)
{
    bat_tests();
    digger_tests();
    crystal_tests();
    golem_tests();
    rockfall_tests();
    generation_tests();
}
