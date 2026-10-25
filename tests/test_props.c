// Destructible props, explosions, pickups and traps.

#include <string.h>

#include "test.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run)
{
    run_start(run, 1);
    room_clear(&run->room);
    run_reset_room(run);
    run->decor_broken = 0;
    memset(run->fuse, 0, sizeof(run->fuse));
    memset(run->crate_broken, 0, sizeof(run->crate_broken));
    for (int i = 0; i < MAX_DECOR; i++)
        run->decor_hp[i] = BARREL_HP;
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            run->crate_hp[z][x] = CRATE_HP;
    run->player.x = FX(6.0);
    run->player.z = FX(6.0);
}

static Enemy *dummy(Run *run, fx x, fx z, int hp)
{
    Enemy *e = run_spawn_enemy(run, ENEMY_CHASER, x, z);
    e->state = EST_RECOVER;
    e->timer = 100000;
    e->hp = e->max_hp = hp;
    return e;
}

static int add_decor(Run *run, DecorKind kind, fx x, fx z)
{
    Room *r = &run->room;
    Decor *d = &r->decor[r->decor_count];
    memset(d, 0, sizeof(*d));
    d->kind = (uint8_t)kind;
    d->wall = SIDE_NONE;
    d->x = x;
    d->z = z;
    return r->decor_count++;
}

static void step_n(Run *run, const PlayerInput *in, int n)
{
    for (int i = 0; i < n; i++)
        run_update(run, in);
}

// One sword swing towards +x (DIR_SE), then let it finish.
static void swing(Run *run)
{
    PlayerInput atk = { .move = DIR_SE, .attack = true };
    run_update(run, &atk);
    step_n(run, &idle, PLAYER_ATTACK_FRAMES + 1);
}

static int count_pickups(const Run *run, PickupKind kind)
{
    int n = 0;
    for (int i = 0; i < MAX_PICKUPS; i++)
        n += run->pickups[i].active && run->pickups[i].kind == kind;
    return n;
}

static void barrel_tests(void)
{
    static Run run;
    int hearts = 0, coins = 0, empty = 0;
    for (uint32_t seed = 1; seed <= 120; seed++) {
        empty_run(&run);
        run.room.seed = seed * 977u;
        dummy(&run, FX(0.5), FX(11.5), 99);   // keeps the room in a fight
        int b = add_decor(&run, DECOR_BARREL, FX(6.7), FX(6.0));
        swing(&run);
        CHECK(!(run.decor_broken & (1u << b)));   // two hits
        CHECK_EQ(run.decor_hp[b], BARREL_HP - 1);
        swing(&run);
        CHECK(run.decor_broken & (1u << b));
        int h = count_pickups(&run, PICK_HEART), c = count_pickups(&run, PICK_COIN);
        CHECK(h + c <= 1);
        hearts += h;
        coins += c;
        empty += !(h + c);
    }
    // Roughly 15% hearts, 30% coins, the rest nothing.
    CHECK(hearts > 5 && hearts < 35);
    CHECK(coins > 20 && coins < 60);
    CHECK(empty > 50);
}

static void crate_tests(void)
{
    static Run run;
    empty_run(&run);
    dummy(&run, FX(0.5), FX(11.5), 99);
    run.room.cells[6][7] = CELL_CRATE;
    for (int i = 0; i < CRATE_HP - 1; i++) {
        swing(&run);
        CHECK_EQ(room_cell(&run.room, 7, 6), CELL_CRATE);
    }
    swing(&run);
    CHECK_EQ(room_cell(&run.room, 7, 6), CELL_FLOOR);
    CHECK(run.crate_broken[6][7]);

    // Daggers chip crates too.
    empty_run(&run);
    dummy(&run, FX(0.5), FX(11.5), 99);
    run.room.cells[6][9] = CELL_CRATE;
    run.player.z = FX(6.5);   // in line with the crate's middle
    PlayerInput thr = { .move = DIR_SE, .throw_dagger = true };
    run_update(&run, &thr);
    step_n(&run, &idle, 40);
    CHECK_EQ(run.crate_hp[6][9], CRATE_HP - DAGGER_DAMAGE);
}

static void explosive_tests(void)
{
    static Run run;
    empty_run(&run);
    Enemy *e = dummy(&run, FX(9.5), FX(6.0), 9);       // next to the barrel
    Enemy *far = dummy(&run, FX(0.5), FX(11.5), 9);
    int a = add_decor(&run, DECOR_EXPLOSIVE, FX(8.5), FX(6.0));
    int b = add_decor(&run, DECOR_EXPLOSIVE, FX(10.0), FX(6.6));  // in range
    int pot = add_decor(&run, DECOR_POT, FX(8.5), FX(7.0));

    // A dagger lights it; nothing happens until the fuse burns down.
    PlayerInput thr = { .move = DIR_SE, .throw_dagger = true };
    run_update(&run, &thr);
    bool lit = false;
    int frames = 0;
    while (!(run.decor_broken & (1u << a)) && frames < 200) {
        run_update(&run, &idle);
        lit |= 0 != (run.events & EV_FUSE_LIT);
        frames++;
    }
    CHECK(lit);
    CHECK(frames >= EXPLOSIVE_FUSE);
    CHECK_EQ(e->hp, 9 - DAGGER_DAMAGE * 0 - EXPLODE_DAMAGE);
    CHECK_EQ(far->hp, 9);
    CHECK(run.decor_broken & (1u << pot));
    // The second barrel goes off shortly after (a chain).
    CHECK(run.fuse[b] > 0 && run.fuse[b] <= EXPLOSIVE_CHAIN_FUSE);
    step_n(&run, &idle, EXPLOSIVE_CHAIN_FUSE + 1);
    CHECK(run.decor_broken & (1u << b));

    // Friendly fire: standing next to it hurts.
    empty_run(&run);
    dummy(&run, FX(0.5), FX(11.5), 99);
    int c = add_decor(&run, DECOR_EXPLOSIVE, FX(6.7), FX(6.0));
    swing(&run);
    CHECK(run.fuse[c] > 0);
    run.player.iframes = 0;
    step_n(&run, &idle, EXPLOSIVE_FUSE + 2);
    CHECK_EQ(run.player.hp, PLAYER_START_HP - EXPLODE_PLAYER_DAMAGE);
}

static void pickup_tests(void)
{
    static Run run;
    empty_run(&run);
    dummy(&run, FX(0.5), FX(11.5), 99);
    Player *p = &run.player;
    run_spawn_pickup(&run, PICK_COIN, 5, FX(6.0), FX(6.0));
    run_update(&run, &idle);
    CHECK_EQ(run.gold, 0);              // still popping up
    step_n(&run, &idle, PICKUP_SETTLE + 2);
    CHECK_EQ(run.gold, 5);
    CHECK_EQ(count_pickups(&run, PICK_COIN), 0);

    // Hearts wait for a hurt player.
    run_spawn_pickup(&run, PICK_HEART, 1, FX(6.0), FX(6.3));
    step_n(&run, &idle, 60);
    CHECK_EQ(count_pickups(&run, PICK_HEART), 1);
    p->hp--;
    step_n(&run, &idle, 10);
    CHECK_EQ(p->hp, PLAYER_START_HP);
    CHECK_EQ(count_pickups(&run, PICK_HEART), 0);

    // Far away: stays put during the fight, flies in once the room clears.
    empty_run(&run);
    Enemy *last = dummy(&run, FX(0.5), FX(11.5), 1);
    run_spawn_pickup(&run, PICK_COIN, 3, FX(11.0), FX(1.0));
    step_n(&run, &idle, 60);
    CHECK_EQ(run.gold, 0);
    last->state = EST_DYING;
    last->timer = 1;
    step_n(&run, &idle, 200);
    CHECK_EQ(run.status, ROOM_CLEAR);
    CHECK(run.gold >= 3);
}

static void trap_tests(void)
{
    // Spike cycle: idle, warning, out.
    Hazard h = { .kind = HAZ_SPIKES, .phase = 0 };
    int t;
    CHECK_EQ(hazard_state(&h, 0, &t), TRAP_IDLE);
    CHECK_EQ(hazard_state(&h, SPIKE_WARN, &t), TRAP_WARN);
    CHECK_EQ(t, 0);
    CHECK_EQ(hazard_state(&h, SPIKE_UP + 3, &t), TRAP_ACTIVE);
    CHECK_EQ(t, 3);
    CHECK_EQ(hazard_state(&h, SPIKE_CYCLE, &t), TRAP_IDLE);

    // Saws stay on their track and reach both ends.
    Hazard saw = { .kind = HAZ_SAW, .phase = 7, .x = FX(2.5), .z = FX(4.5),
                   .x2 = FX(6.5), .z2 = FX(4.5) };
    bool at_start = false, at_end = false;
    for (uint32_t f = 0; f < 800; f++) {
        fx x, z;
        hazard_saw_pos(&saw, f, &x, &z);
        CHECK(x >= FX(2.5) - 4 && x <= FX(6.5) + 4);
        CHECK_EQ(z, FX(4.5));
        at_start |= x < FX(2.6);
        at_end |= x > FX(6.4);
    }
    CHECK(at_start && at_end);

    // Standing on spikes: hurt only while they are out; an enemy on them is
    // hit once per cycle.
    static Run run;
    empty_run(&run);
    Hazard *sp = &run.room.hazards[run.room.hazard_count++];
    *sp = (Hazard){ .kind = HAZ_SPIKES, .phase = 0, .x = FX(6.5), .z = FX(6.5),
                    .x2 = FX(6.5), .z2 = FX(6.5) };
    run.player.x = FX(6.5);
    run.player.z = FX(6.5);
    Enemy *e = dummy(&run, FX(6.5), FX(6.5), 20);
    Enemy *keep = dummy(&run, FX(0.5), FX(11.5), 99);
    (void)keep;
    // run.frames counts from the first update.
    step_n(&run, &idle, SPIKE_UP - 2);
    CHECK_EQ(run.player.hp, PLAYER_START_HP);
    CHECK_EQ(e->hp, 20);
    step_n(&run, &idle, 4);
    CHECK_EQ(run.player.hp, PLAYER_START_HP - TRAP_DAMAGE);
    CHECK_EQ(e->hp, 20 - TRAP_DAMAGE);
    e->x = FX(6.5);   // knockback moved it; put it back
    e->z = FX(6.5);
    step_n(&run, &idle, 20);
    CHECK_EQ(e->hp, 20 - TRAP_DAMAGE);   // once per active phase
    step_n(&run, &idle, SPIKE_CYCLE);
    e->x = FX(6.5);
    e->z = FX(6.5);
    step_n(&run, &idle, SPIKE_CYCLE);
    CHECK(e->hp < 20 - TRAP_DAMAGE);
}

void run_props_tests(void)
{
    barrel_tests();
    crate_tests();
    explosive_tests();
    pickup_tests();
    trap_tests();
}
