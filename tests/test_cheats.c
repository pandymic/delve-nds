// Code Genie's cheat codes and what each does to a run.

#include <string.h>

#include "test.h"
#include "boon.h"
#include "cheat.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run, uint32_t cheats)
{
    run_start(run, 1);
    room_clear(&run->room);
    run_reset_room(run);
    run->player.x = FX(6.0);
    run->player.z = FX(6.0);
    run->cheats = cheats;
}

// A still enemy that cannot hurt anyone (stuck in a long recovery).
static Enemy *still(Run *run, EnemyType type, fx x, fx z, int hp)
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

static void code_tests(void)
{
    for (int i = 0; i < CHEAT_COUNT; i++) {
        CHECK(0 != cheat_defs[i].code && CODE_BUG_REPORT != cheat_defs[i].code);
        CHECK(strlen(cheat_defs[i].name) <= 15);
        for (int k = i + 1; k < CHEAT_COUNT; k++)
            CHECK(cheat_defs[i].code != cheat_defs[k].code);
    }

    // On, then off again; several stack.
    uint32_t on = 0;
    CheatId id = CHEAT_COUNT;
    CHECK_EQ(cheat_enter(&on, 0x5AFE5AFEu, &id), CODE_ON);
    CHECK_EQ(id, CHEAT_GOD);
    CHECK_EQ(cheat_enter(&on, 0xDEADBEEFu, &id), CODE_ON);
    CHECK_EQ(on, CHEAT_BIT(CHEAT_GOD) | CHEAT_BIT(CHEAT_ONE_HIT));
    CHECK_EQ(cheat_enter(&on, 0x5AFE5AFEu, &id), CODE_OFF);
    CHECK_EQ(id, CHEAT_GOD);
    CHECK_EQ(on, CHEAT_BIT(CHEAT_ONE_HIT));
    // Unknown codes and the bug report change nothing.
    CHECK_EQ(cheat_enter(&on, 0x12345678u, &id), CODE_UNKNOWN);
    CHECK_EQ(cheat_enter(&on, CODE_BUG_REPORT, &id), CODE_REPORT);
    CHECK_EQ(on, CHEAT_BIT(CHEAT_ONE_HIT));

    // One start area at a time.
    CHECK_EQ(cheat_start_area(on), 0);
    cheat_enter(&on, 0xDE5CE002u, &id);
    CHECK_EQ(cheat_start_area(on), 1);
    cheat_enter(&on, 0xDE5CE004u, &id);
    CHECK_EQ(cheat_start_area(on), 3);
    CHECK(!cheat_on(on, CHEAT_SEWERS));
    CHECK(cheat_on(on, CHEAT_ONE_HIT));
    cheat_enter(&on, 0xDE5CE004u, &id);
    CHECK_EQ(cheat_start_area(on), 0);

    // All upgrades: every bit while on, the real ones otherwise.
    CHECK_EQ(cheat_upgrades(0, 0x15), 0x15);
    CHECK_EQ(cheat_upgrades(CHEAT_BIT(CHEAT_UPGRADES), 0x15), 0xFFFFFFFFu);

    char list[40];
    cheat_list(CHEAT_BIT(CHEAT_GOD) | CHEAT_BIT(CHEAT_GOLD), list, sizeof(list));
    CHECK(0 == strcmp(list, "God mode, Gold x5"));
    cheat_list(0xFFFFFFFFu, list, 20);   // cut to fit, never mid-name
    CHECK(0 == strcmp(list, "God mode"));
}

static void damage_tests(void)
{
    static Run run;
    Player *p = &run.player;

    // God mode: no damage from anything, pits included (still put back on
    // the edge).
    empty_run(&run, CHEAT_BIT(CHEAT_GOD));
    CHECK(!run_hurt_player(&run, p, 3, 0, 0, ENEMY_CHASER));
    CHECK_EQ(p->hp, p->max_hp);
    p->safe_x = FX(3.5);
    p->safe_z = FX(3.5);
    p->x = FX(8.5);
    p->z = FX(8.5);
    run.room.cells[8][8] = CELL_PIT;
    run_update(&run, &idle);
    CHECK(run.events & EV_PLAYER_FELL);
    CHECK_EQ(p->hp, p->max_hp);
    CHECK_EQ(p->x, FX(3.5));

    // Hard mode doubles it.
    empty_run(&run, CHEAT_BIT(CHEAT_HARD));
    CHECK(run_hurt_player(&run, p, 1, 0, 0, ENEMY_CHASER));
    CHECK_EQ(p->hp, p->max_hp - 2);

    // Without cheats, nothing changes.
    empty_run(&run, 0);
    CHECK(run_hurt_player(&run, p, 1, 0, 0, ENEMY_CHASER));
    CHECK_EQ(p->hp, p->max_hp - 1);
}

static void one_hit_tests(void)
{
    static Run run;
    PlayerInput atk = { .move = DIR_NONE, .attack = true };

    // A boss falls to one swing.
    empty_run(&run, CHEAT_BIT(CHEAT_ONE_HIT));
    Enemy *b = still(&run, ENEMY_WARLORD, FX(6.6), FX(6.6), 200);
    run_update(&run, &atk);
    step_n(&run, &idle, PLAYER_ATTACK_WINDUP + 1);
    CHECK_EQ(b->state, EST_DYING);
    CHECK_EQ(run.kills, 1);

    // ...but not without the cheat.
    empty_run(&run, 0);
    b = still(&run, ENEMY_WARLORD, FX(6.6), FX(6.6), 200);
    run_update(&run, &atk);
    step_n(&run, &idle, PLAYER_ATTACK_WINDUP + 1);
    CHECK(EST_DYING != b->state && b->hp < 200);

    // The training dummy still stands.
    empty_run(&run, CHEAT_BIT(CHEAT_ONE_HIT));
    Enemy *d = still(&run, ENEMY_DUMMY, FX(6.6), FX(6.6), 5);
    run_update(&run, &atk);
    step_n(&run, &idle, PLAYER_ATTACK_WINDUP + 1);
    CHECK(EST_DYING != d->state);
}

static void kit_tests(void)
{
    static Run run;
    Player *p = &run.player;
    uint32_t endless = CHEAT_BIT(CHEAT_BLAST) | CHEAT_BIT(CHEAT_STAMINA)
                     | CHEAT_BIT(CHEAT_DAGGERS);
    empty_run(&run, endless);
    PlayerInput dash = { .move = DIR_E, .dash = true };
    PlayerInput thr = { .move = DIR_E, .throw_dagger = true };
    PlayerInput bl = { .move = DIR_NONE, .blast = true };
    for (int i = 0; i < 6; i++) {
        run_update(&run, &dash);
        CHECK_EQ(p->dash_charges, p->dash_max);
        step_n(&run, &idle, PLAYER_DASH_GAP + 2);
    }
    for (int i = 0; i < 5; i++) {
        run_update(&run, &thr);
        CHECK_EQ(p->daggers, p->dagger_max);
        step_n(&run, &idle, DAGGER_THROW_FRAMES + 1);
    }
    CHECK_EQ(p->meter, p->meter_max);   // full from the start
    run_update(&run, &bl);
    CHECK_EQ(p->action, PACT_BLAST);
    CHECK_EQ(p->meter, p->meter_max);

    // Without them, each runs down.
    empty_run(&run, 0);
    run_update(&run, &dash);
    CHECK_EQ(p->dash_charges, p->dash_max - 1);
    CHECK_EQ(p->meter, 0);
}

static void wipe_tests(void)
{
    static Run run;
    PlayerInput bl = { .move = DIR_NONE, .blast = true };
    for (int cheat = 0; cheat < 2; cheat++) {
        empty_run(&run, cheat ? CHEAT_BIT(CHEAT_WIPE) : 0);
        run.player.meter = run.player.meter_max;
        Enemy *near = still(&run, ENEMY_CHASER, FX(6.8), FX(6.8), 1);
        Enemy *far = still(&run, ENEMY_WARLORD, FX(1.5), FX(1.5), 200);
        Enemy *far2 = still(&run, ENEMY_SHOOTER, FX(10.5), FX(10.5), 9);
        Projectile *s = run_spawn_projectile(&run, PROJ_FIREBALL, FX(1.5),
                                             FX(10.5), 0, 0, 1);
        run_update(&run, &bl);
        step_n(&run, &idle, BLAST_WINDUP + 1);
        CHECK_EQ(near->state, EST_DYING);
        CHECK_EQ(EST_DYING == far->state, cheat);
        CHECK_EQ(EST_DYING == far2->state, cheat);
        CHECK_EQ(s->active, !cheat);
    }
}

static void gold_tests(void)
{
    static Run run;
    empty_run(&run, 0);
    CHECK_EQ(boon_gold(&run, 10), 10);
    run.cheats = CHEAT_BIT(CHEAT_GOLD);
    CHECK_EQ(boon_gold(&run, 10), 10 * CHEAT_GOLD_MULT);
    run.boons[BOON_GOLD_RUSH] = 1;   // multiplied after the boons
    CHECK_EQ(boon_gold(&run, 10),
             (10 + 10 * GOLD_RUSH_PCT / 100) * CHEAT_GOLD_MULT);
}

void run_cheats_tests(void)
{
    code_tests();
    damage_tests();
    one_hit_tests();
    kit_tests();
    wipe_tests();
    gold_tests();
}
