// Enemies, attacks, damage and room clearing.

#include <string.h>

#include "test.h"
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

// An enemy that stands still in a corner so the room is not "cleared"
// (clearing a room removes leftover projectiles).
static void add_dormant_enemy(Run *run)
{
    Enemy *e = run_spawn_enemy(run, ENEMY_CHASER, FX(0.5), FX(11.5));
    e->state = EST_RECOVER;
    e->timer = 100000;
}

static void step_n(Run *run, const PlayerInput *in, int n)
{
    for (int i = 0; i < n; i++)
        run_update(run, in);
}

void run_combat_tests(void)
{
    Run run;

    // A chaser walks up, telegraphs, and lands one hit.
    empty_run(&run);
    Enemy *c = run_spawn_enemy(&run, ENEMY_CHASER, FX(6.0), FX(9.0));
    CHECK(NULL != c);
    bool saw_windup = false;
    int frames = 0;
    while (PLAYER_START_HP == run.player.hp && frames < 600) {
        run_update(&run, &idle);
        saw_windup |= EST_WINDUP == c->state;
        frames++;
    }
    CHECK(saw_windup);
    CHECK_EQ(run.player.hp, PLAYER_START_HP - 1);
    CHECK(run.player.iframes > 0);

    // Killing the only enemy clears the room.
    empty_run(&run);
    c = run_spawn_enemy(&run, ENEMY_CHASER, FX(7.0), FX(6.0));
    step_n(&run, &idle, ENEMY_SPAWN_FRAMES); // let it finish spawning
    CHECK_EQ(run.status, ROOM_FIGHT);
    PlayerInput atk = { .move = DIR_SE, .attack = true }; // SE = +x
    for (int i = 0; i < 200 && ROOM_FIGHT == run.status; i++) {
        run.player.iframes = 10; // keep the test about our damage only
        c->x = FX(6.8);
        c->z = FX(6.0);
        run_update(&run, 0 == (i % (PLAYER_ATTACK_FRAMES + 1)) ? &atk : &idle);
    }
    CHECK_EQ(run.kills, 1);
    CHECK_EQ(run.status, ROOM_CLEAR);
    CHECK_EQ(run_enemies_alive(&run), 0);

    // A projectile hits a standing player.
    empty_run(&run);
    add_dormant_enemy(&run);
    run_spawn_projectile(&run, PROJ_FIREBALL, FX(6.0), FX(3.0), 0, FX(0.07), 1);
    step_n(&run, &idle, 80);
    CHECK_EQ(run.player.hp, PLAYER_START_HP - 1);

    // Dash i-frames let a projectile pass through.
    empty_run(&run);
    add_dormant_enemy(&run);
    run_spawn_projectile(&run, PROJ_FIREBALL, FX(6.0), FX(5.0), 0, FX(0.07), 1);
    PlayerInput dash = { .move = DIR_N, .dash = true };
    // Dash towards the shot at the last moment.
    step_n(&run, &idle, 8);
    run_update(&run, &dash);
    step_n(&run, &idle, 40);
    CHECK_EQ(run.player.hp, PLAYER_START_HP);

    // Projectiles stop at walls and obstacles.
    empty_run(&run);
    room_make_test(&run.room);
    add_dormant_enemy(&run);
    Projectile *s = run_spawn_projectile(&run, PROJ_FIREBALL, FX(3.5), FX(1.0), 0, FX(0.1), 1);
    step_n(&run, &idle, 10);
    CHECK(s->active);         // still flying before the pillar
    step_n(&run, &idle, 30);  // pillar at (3,3)
    CHECK(!s->active);
    CHECK(s->z < FX(3.2));

    // Pools are bounded.
    empty_run(&run);
    int spawned = 0;
    for (int i = 0; i < MAX_ENEMIES + 3; i++)
        spawned += NULL != run_spawn_enemy(&run, ENEMY_SHOOTER, FX(1.0), FX(1.0));
    CHECK_EQ(spawned, MAX_ENEMIES);

    // Death.
    empty_run(&run);
    run_spawn_enemy(&run, ENEMY_CHASER, FX(6.0), FX(8.0));
    run.player.hp = 1;
    step_n(&run, &idle, 600);
    CHECK_EQ(run.status, ROOM_DEAD);
    CHECK_EQ(run.player.hp, 0);

    // Determinism: same start, same inputs, same result.
    static Run a, b;
    run_start(&a, 99);
    run_load_test_room(&a);
    memcpy(&b, &a, sizeof(a));
    for (int i = 0; i < 900; i++) {
        PlayerInput in = { .move = (Dir8)((i / 40) % 8), .attack = 0 == (i % 23),
                           .dash = 0 == (i % 97) };
        run_update(&a, &in);
        run_update(&b, &in);
    }
    CHECK(0 == memcmp(&a, &b, sizeof(a)));
}
