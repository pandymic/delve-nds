// The camp between runs.

#include "test.h"
#include "roomgen.h"
#include "run.h"

void run_hub_tests(void)
{
    static Run run;
    run_start_hub(&run);
    const PlayerInput idle = { .move = DIR_NONE };
    CHECK(run.room.hub);
    CHECK_EQ(run.depth, 0);
    CHECK_EQ(run.room.door_count, 1);
    CHECK_EQ(run.room.doors[0].side, SIDE_NZ);

    // The camp is already "clear": no enemies but the dummy, no boon, and
    // the fire does not heal.
    int hp = run.player.hp;
    run_update(&run, &idle);
    CHECK_EQ(run.status, ROOM_CLEAR);
    CHECK(!run.boon_pending);
    CHECK_EQ(run_enemies_alive(&run), 0);
    CHECK_EQ(run.player.hp, hp);

    // The dummy takes hits forever and fills the meter.
    Enemy *dummy = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (run.enemies[i].active && ENEMY_DUMMY == run.enemies[i].type)
            dummy = &run.enemies[i];
    CHECK(NULL != dummy);
    if (!dummy)
        return;
    for (int i = 0; i < 60; i++)
        run_update(&run, &idle);   // let it finish spawning
    PlayerInput atk = { .move = DIR_SE, .attack = true };
    int hits = 0;
    for (int n = 0; n < 15; n++) {
        run.player.x = dummy->x - FX(0.6);
        run.player.z = dummy->z;
        run_update(&run, &atk);
        for (int t = 0; t < PLAYER_ATTACK_FRAMES + 1; t++) {
            run_update(&run, &idle);
            hits += 0 != (run.events & EV_ENEMY_HIT);
        }
    }
    CHECK(hits >= 15);
    CHECK(dummy->active);
    CHECK_EQ(dummy->hp, dummy->max_hp);
    CHECK_EQ(run.player.meter, run.player.meter_max);
    CHECK_EQ(run.kills, 0);

    // The shrine is found by standing next to it.
    run.player.x = HUB_SHRINE_X + FX(0.6);
    run.player.z = HUB_SHRINE_Z;
    CHECK(run_at_shrine(&run));
    run.player.x = FX(9.0);
    CHECK(!run_at_shrine(&run));

    // The portal: walking into the door raises EV_DOOR_ENTER at once.
    fx cx, cz, nx, nz;
    door_center(SIDE_NZ, HUB_PORTAL_POS, &cx, &cz);
    side_inward(SIDE_NZ, &nx, &nz);
    run.player.x = cx + fx_mul(nx, PLAYER_HALF);
    run.player.z = cz + fx_mul(nz, PLAYER_HALF);
    run_update(&run, &idle);
    CHECK(run.events & EV_DOOR_ENTER);

    // A normal run never has the hub flag.
    run_start(&run, 9);
    CHECK(!run.room.hub);
}
