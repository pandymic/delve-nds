// The Hunter's and the Warden's upgrade paths.

#include <string.h>

#include "test.h"
#include "meta.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run, uint32_t upgrades)
{
    run_start(run, 1);
    run_apply_upgrades(run, upgrades);
    room_clear(&run->room);
    run_reset_room(run);
    run->player.x = run->player.safe_x = FX(6.0);
    run->player.z = run->player.safe_z = FX(6.0);
}

static Enemy *dummy(Run *run, fx x, fx z, int hp)
{
    Enemy *e = run_spawn_enemy(run, ENEMY_CHASER, x, z);
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

// Three dummies in a row, one dagger thrown down it: their hp after.
static void throw_down_row(uint32_t upgrades, int hp[3])
{
    static Run run;
    empty_run(&run, upgrades);
    run.player.x = FX(1.5);
    Enemy *e[3];
    for (int i = 0; i < 3; i++)
        e[i] = dummy(&run, FX(3.5) + i * FX(1.5), FX(6.0), 9);
    PlayerInput thr = { .move = DIR_SE, .throw_dagger = true };  // +x
    run_update(&run, &thr);
    step_n(&run, &idle, 60);
    for (int i = 0; i < 3; i++)
        hp[i] = e[i]->hp;
}

static void chain_tests(void)
{
    // Each step needs the one before it.
    static const UpgradeId paths[2][3] = {
        { UPG_FLETCHING, UPG_BARBED_TIPS, UPG_HUNTERS_MARK },
        { UPG_IRON_HIDE, UPG_THORNS, UPG_MENDING },
    };
    for (int p = 0; p < 2; p++) {
        MetaProgress m = { .currency = 10000 };
        CHECK_EQ(meta_buy(&m, paths[p][2]), BUY_LOCKED);
        CHECK_EQ(meta_buy(&m, paths[p][1]), BUY_LOCKED);
        CHECK_EQ(meta_buy(&m, paths[p][0]), BUY_OK);
        CHECK_EQ(meta_buy(&m, paths[p][2]), BUY_LOCKED);
        CHECK_EQ(meta_buy(&m, paths[p][1]), BUY_OK);
        CHECK_EQ(meta_buy(&m, paths[p][2]), BUY_OK);
        CHECK(upgrade_defs[paths[p][0]].cost < upgrade_defs[paths[p][1]].cost);
        CHECK(upgrade_defs[paths[p][1]].cost < upgrade_defs[paths[p][2]].cost);
    }
    // Every upgrade has a name the shop's column fits, and a description.
    for (int i = 0; i < UPG_COUNT; i++) {
        CHECK(strlen(upgrade_defs[i].name) <= 16);
        CHECK(strlen(upgrade_defs[i].desc) > 0);
    }
    // The bitfield still fits the save's 32 bits.
    CHECK(UPG_COUNT <= 32);
}

static void hunter_tests(void)
{
    static Run run;
    empty_run(&run, 1u << UPG_FLETCHING);
    CHECK_EQ(run.player.dagger_recharge_time,
             DAGGER_RECHARGE * FLETCHING_PCT / 100);

    // A dagger goes through one enemy; Barbed Tips, two; Hunter's Mark
    // makes each hit count double.
    int hp[3];
    throw_down_row(0, hp);
    CHECK(8 == hp[0] && 8 == hp[1] && 9 == hp[2]);
    throw_down_row(1u << UPG_BARBED_TIPS, hp);
    CHECK(8 == hp[0] && 8 == hp[1] && 8 == hp[2]);
    throw_down_row((1u << UPG_BARBED_TIPS) | (1u << UPG_HUNTERS_MARK), hp);
    CHECK(7 == hp[0] && 7 == hp[1] && 7 == hp[2]);
}

static void warden_tests(void)
{
    static Run run;

    // Iron Hide: longer invulnerability after a hit.
    empty_run(&run, 0);
    CHECK(run_hurt_player(&run, &run.player, 1, FX(7.0), FX(6.0), CAUSE_SAW));
    int plain = run.player.iframes;
    empty_run(&run, 1u << UPG_IRON_HIDE);
    CHECK(run_hurt_player(&run, &run.player, 1, FX(7.0), FX(6.0), CAUSE_SAW));
    CHECK_EQ(run.player.iframes, plain * IRON_HIDE_PCT / 100);

    // Thorns: a hit lashes out at enemies close by, not far ones.
    for (int thorns = 0; thorns < 2; thorns++) {
        empty_run(&run, thorns ? 1u << UPG_THORNS : 0);
        Enemy *near = dummy(&run, FX(7.0), FX(6.0), 5);
        Enemy *far = dummy(&run, FX(10.5), FX(6.0), 5);
        step_n(&run, &idle, ENEMY_SPAWN_FRAMES);
        run_hurt_player(&run, &run.player, 1, near->x, near->z, CAUSE_SAW);
        run_update(&run, &idle);
        CHECK_EQ(near->hp, thorns ? 5 - THORNS_DAMAGE : 5);
        CHECK_EQ(far->hp, 5);
        CHECK(!run.thorns_due);
    }

    // Mending: every MENDING_FIGHTS fights cleared heal a heart (not past
    // the maximum).
    for (int mending = 0; mending < 2; mending++) {
        empty_run(&run, mending ? 1u << UPG_MENDING : 0);
        Player *p = &run.player;
        p->hp = 1;
        int healed = 0;
        for (int fight = 1; fight <= MENDING_FIGHTS * 2; fight++) {
            run.room.type = RT_COMBAT;
            run.room.enemy_count = 1;
            run.status = ROOM_FIGHT;
            run_update(&run, &idle);
            CHECK_EQ(run.status, ROOM_CLEAR);
            healed += 0 != (run.events2 & EV2_MENDED);
        }
        CHECK_EQ(healed, mending ? 2 : 0);
        CHECK_EQ(p->hp, mending ? 3 : 1);
        p->hp = p->max_hp;
        for (int fight = 0; fight < MENDING_FIGHTS; fight++) {
            run.status = ROOM_FIGHT;
            run_update(&run, &idle);
        }
        CHECK_EQ(p->hp, p->max_hp);
    }
}

void run_paths_tests(void)
{
    chain_tests();
    hunter_tests();
    warden_tests();
}
