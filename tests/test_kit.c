// Dagger, blast, auto-aim.

#include <string.h>

#include "test.h"
#include "fx.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run)
{
    run_start(run, 1);
    room_clear(&run->room);
    run_reset_room(run);
    run->player.x = FX(6.0);
    run->player.z = FX(6.0);
}

// A still enemy that cannot hurt anyone (stuck in a long recovery).
static Enemy *dummy(Run *run, EnemyType type, fx x, fx z, int hp)
{
    Enemy *e = run_spawn_enemy(run, type, x, z);
    e->state = EST_RECOVER;
    e->timer = 100000;
    e->hp = e->max_hp = hp;
    return e;
}

static void step_n(Run *run, const PlayerInput *in, int n)
{
    for (int i = 0; i < n; i++)
        run_update(run, in);
}

static void direction_tests(void)
{
    // dir8_towards inverts dir8_vector for every direction.
    for (int d = 0; d < DIR_COUNT; d++) {
        fx x, z;
        dir8_vector((Dir8)d, &x, &z);
        CHECK_EQ(dir8_towards(x, z), d);
        CHECK_EQ(dir8_towards(x * 3, z * 3), d);
    }
    CHECK_EQ(dir8_towards(0, 0), DIR_S);
}

static void dagger_tests(void)
{
    static Run run;
    empty_run(&run);
    Player *p = &run.player;
    CHECK_EQ(p->daggers, DAGGER_CHARGES);

    // A throw east hits an enemy in line, fills the meter by one.
    Enemy *e = dummy(&run, ENEMY_CHASER, FX(6.0) + FX(2.12), FX(6.0) - FX(2.12),
                     5);   // DIR_E is (+x, -z) on the ground
    PlayerInput thr = { .move = DIR_E, .throw_dagger = true };
    run_update(&run, &thr);
    CHECK_EQ(p->action, PACT_THROW);
    CHECK_EQ(p->daggers, DAGGER_CHARGES - 1);
    bool thrown = false;
    for (int i = 0; i < 40 && 5 == e->hp; i++) {
        run_update(&run, &idle);
        thrown |= 0 != (run.events & EV_DAGGER_THROW);
    }
    CHECK(thrown);
    CHECK_EQ(e->hp, 5 - DAGGER_DAMAGE);
    CHECK_EQ(p->meter, 1);
    CHECK(run.hit_count >= 0);

    // Charges run out, then refill one at a time.
    empty_run(&run);
    p = &run.player;
    dummy(&run, ENEMY_CHASER, FX(0.5), FX(11.5), 99);  // keeps the room open
    for (int i = 0; i < DAGGER_CHARGES; i++) {
        run_update(&run, &thr);
        step_n(&run, &idle, DAGGER_THROW_FRAMES);
    }
    CHECK_EQ(p->daggers, 0);
    run_update(&run, &thr);
    CHECK(PACT_THROW != p->action);
    step_n(&run, &idle, DAGGER_RECHARGE);
    CHECK_EQ(p->daggers, 1);

    // Piercing: two enemies in a row are hit, a third is not.
    empty_run(&run);
    p = &run.player;
    p->x = FX(1.5);
    p->z = FX(6.0);
    PlayerInput thr_se = { .move = DIR_SE, .throw_dagger = true };  // +x
    Enemy *a = dummy(&run, ENEMY_CHASER, FX(3.5), FX(6.0), 9);
    Enemy *b = dummy(&run, ENEMY_CHASER, FX(5.0), FX(6.0), 9);
    Enemy *c = dummy(&run, ENEMY_CHASER, FX(6.5), FX(6.0), 9);
    run_update(&run, &thr_se);
    step_n(&run, &idle, 60);
    CHECK_EQ(a->hp, 8);
    CHECK_EQ(b->hp, 8);
    CHECK_EQ(c->hp, 9);

    // Into a wall: the dagger stops and reports where.
    empty_run(&run);
    dummy(&run, ENEMY_CHASER, FX(0.5), FX(11.5), 99);
    run.player.x = FX(10.5);
    run.player.z = FX(6.0);
    bool wall = false;
    run_update(&run, &thr_se);
    for (int i = 0; i < 30; i++) {
        run_update(&run, &idle);
        if (run.events & EV_DAGGER_WALL) {
            wall = true;
            CHECK(run.hit_count >= 1);
        }
    }
    CHECK(wall);
    for (int i = 0; i < MAX_DAGGERS; i++)
        CHECK(!run.daggers[i].active);

    // A dagger cancels an enemy projectile flying at the player.
    empty_run(&run);
    dummy(&run, ENEMY_CHASER, FX(0.5), FX(11.5), 99);
    run.player.x = FX(2.0);
    run.player.z = FX(6.0);
    run_spawn_projectile(&run, PROJ_FIREBALL, FX(9.0), FX(6.0), FX(-0.07), 0, 1);
    run_update(&run, &thr_se);
    step_n(&run, &idle, 40);
    CHECK_EQ(run.player.hp, PLAYER_START_HP);
    for (int i = 0; i < MAX_PROJECTILES; i++)
        CHECK(!run.shots[i].active);
}

static void blast_tests(void)
{
    static Run run;
    empty_run(&run);
    Player *p = &run.player;
    Enemy *near = dummy(&run, ENEMY_CHASER, FX(7.5), FX(6.0), 9);
    Enemy *far = dummy(&run, ENEMY_CHASER, FX(10.5), FX(6.0), 9);
    PlayerInput bl = { .move = DIR_NONE, .blast = true };

    // Not before the meter is full.
    run_update(&run, &bl);
    CHECK(PACT_BLAST != p->action);
    CHECK_EQ(near->hp, 9);

    player_add_meter(p, 100);
    CHECK_EQ(p->meter, BLAST_METER_MAX);
    run_spawn_projectile(&run, PROJ_FIREBALL, FX(6.0), FX(7.5), 0, FX(-0.01), 1);
    run_update(&run, &bl);
    CHECK_EQ(p->action, PACT_BLAST);
    CHECK_EQ(p->meter, 0);
    CHECK(p->iframes >= BLAST_FRAMES);
    bool went_off = false;
    for (int i = 0; i < BLAST_FRAMES; i++) {
        run_update(&run, &idle);
        went_off |= 0 != (run.events & EV_BLAST);
    }
    CHECK(went_off);
    CHECK_EQ(near->hp, 9 - BLAST_DAMAGE);
    CHECK(near->x > FX(7.5));          // knocked away from the player
    CHECK_EQ(far->hp, 9);
    CHECK_EQ(p->meter, 0);             // the blast does not refill itself
    for (int i = 0; i < MAX_PROJECTILES; i++)
        CHECK(!run.shots[i].active);

    // Sword hits fill the meter; it caps and reports filling once.
    empty_run(&run);
    p = &run.player;
    Enemy *bag = dummy(&run, ENEMY_CHASER, FX(6.0) + FX(0.7), FX(6.0), 99);
    PlayerInput atk = { .move = DIR_SE, .attack = true };
    int fills = 0;
    for (int i = 0; i < BLAST_METER_MAX + 3; i++) {
        bag->x = FX(6.7);
        bag->z = FX(6.0);
        run_update(&run, &atk);
        fills += 0 != (run.events & EV_METER_FULL);
        for (int t = 0; t < PLAYER_ATTACK_FRAMES + 1; t++) {
            run_update(&run, &idle);
            fills += 0 != (run.events & EV_METER_FULL);
        }
    }
    CHECK_EQ(p->meter, BLAST_METER_MAX);
    CHECK_EQ(fills, 1);
}

static void aim_tests(void)
{
    static Run run;
    empty_run(&run);
    Player *p = &run.player;
    p->facing = DIR_N;
    dummy(&run, ENEMY_CHASER, FX(6.0), FX(8.0), 9);   // +z: down-left (SW)
    dummy(&run, ENEMY_CHASER, FX(0.5), FX(0.5), 9);   // far away
    PlayerInput atk = { .move = DIR_NONE, .attack = true };
    run_update(&run, &atk);
    CHECK_EQ(p->facing, DIR_SW);

    // A held direction always wins.
    empty_run(&run);
    p = &run.player;
    dummy(&run, ENEMY_CHASER, FX(6.0), FX(8.0), 9);
    PlayerInput atk_e = { .move = DIR_E, .attack = true };
    run_update(&run, &atk_e);
    CHECK_EQ(p->facing, DIR_E);

    // Nothing in range: the facing stays.
    empty_run(&run);
    p = &run.player;
    p->facing = DIR_NE;
    dummy(&run, ENEMY_CHASER, FX(0.5), FX(0.5), 9);
    run_update(&run, &atk);
    CHECK_EQ(p->facing, DIR_NE);
}

static void fx_tests(void)
{
    static Fx f;
    fx_init(&f, 9);
    fx_burst(&f, PART_SHARD, 10, FX(3.0), FX(0.5), FX(3.0), FX(0.05));
    CHECK_EQ(fx_active_count(&f), 10);
    // Overfilling recycles instead of failing.
    fx_burst(&f, PART_SPARK, MAX_PARTICLES * 2, 0, FX(1.0), 0, FX(0.05));
    CHECK_EQ(fx_active_count(&f), MAX_PARTICLES);

    // Debris falls and settles on the floor, never below it.
    fx_init(&f, 9);
    fx_emit(&f, PART_SHARD, FX(1.0), FX(1.0), FX(1.0), FX(0.02), FX(0.05),
            0, 200);
    for (int i = 0; i < 150; i++) {
        fx_update(&f);
        CHECK(f.parts[0].y >= 0);
    }
    CHECK(f.parts[0].active);
    CHECK_EQ(f.parts[0].y, 0);
    CHECK_EQ(f.parts[0].vy, 0);
    for (int i = 0; i < 60; i++)
        fx_update(&f);
    CHECK(!f.parts[0].active);

    // Smoke rises.
    fx_init(&f, 9);
    fx_burst(&f, PART_SMOKE, 4, 0, FX(0.5), 0, FX(0.03));
    for (int i = 0; i < 10; i++)
        fx_update(&f);
    for (int i = 0; i < 4; i++)
        CHECK(f.parts[i].y > FX(0.5));

    // Fire and smoke (cinders, flame licks, wisps) float up, never fall,
    // and stay near where they started; then they die out.
    static const ParticleKind fire[] = { PART_CINDER, PART_FLAME, PART_WISP };
    for (unsigned k = 0; k < sizeof(fire) / sizeof(fire[0]); k++) {
        CHECK(fx_floats(fire[k]));
        fx_init(&f, 9 + k);
        fx_emit(&f, fire[k], FX(3.0), FX(1.0), FX(3.0), 0, FX(0.02), 0, 40);
        fx y = FX(1.0);
        for (int i = 0; i < 39; i++) {
            fx_update(&f);
            CHECK(f.parts[0].active);
            CHECK(f.parts[0].y >= y);
            y = f.parts[0].y;
        }
        CHECK(y > FX(1.0));
        CHECK(f.parts[0].x > FX(2.5) && f.parts[0].x < FX(3.5));
        fx_update(&f);
        CHECK(!f.parts[0].active);
    }
    CHECK(!fx_floats(PART_SHARD));

    // Damage numbers rise and expire; the pool reuses the oldest.
    fx_init(&f, 9);
    for (int i = 0; i < MAX_FLOATERS + 3; i++)
        fx_number(&f, i, FX(2.0), FX(2.0), false);
    int n = 0;
    for (int i = 0; i < MAX_FLOATERS; i++)
        n += f.floaters[i].active;
    CHECK_EQ(n, MAX_FLOATERS);
    for (int i = 0; i < FLOATER_LIFE; i++)
        fx_update(&f);
    for (int i = 0; i < MAX_FLOATERS; i++)
        CHECK(!f.floaters[i].active);
}

void run_kit_tests(void)
{
    fx_tests();
    direction_tests();
    dagger_tests();
    blast_tests();
    aim_tests();
}
