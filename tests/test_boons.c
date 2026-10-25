// Boons.

#include <string.h>

#include "test.h"
#include "boon.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run)
{
    run_start(run, 1);
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

static void offer_tests(void)
{
    static Run run;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        run_start(&run, seed);
        BoonId a[BOON_CHOICES], b[BOON_CHOICES];
        int n = boon_offer(&run, a);
        CHECK_EQ(n, BOON_CHOICES);
        CHECK_EQ(boon_offer(&run, b), n);
        for (int i = 0; i < n; i++) {
            CHECK_EQ(a[i], b[i]);              // seeded
            CHECK(a[i] >= 0 && a[i] < BOON_COUNT);
            for (int j = i + 1; j < n; j++)
                CHECK(a[i] != a[j]);           // distinct
        }
    }
    // Maxed boons are never offered; with everything maxed, no offer.
    run_start(&run, 3);
    for (int i = 0; i < BOON_COUNT; i++)
        while (run.boons[i] < boon_defs[i].max)
            boon_apply(&run, (BoonId)i);
    BoonId out[BOON_CHOICES];
    CHECK_EQ(boon_offer(&run, out), 0);
    run.boons[BOON_GOLD_RUSH]--;
    CHECK_EQ(boon_offer(&run, out), 1);
    CHECK_EQ(out[0], BOON_GOLD_RUSH);
    // Applying past the limit does nothing.
    int before = run.player.max_hp;
    boon_apply(&run, BOON_THICK_SKIN);
    CHECK_EQ(run.player.max_hp, before);
    // Descriptions fit their line on the bottom screen.
    for (int i = 0; i < BOON_COUNT; i++)
        CHECK(strlen(boon_defs[i].desc) <= 26 && strlen(boon_defs[i].name) <= 14);
}

static void stat_tests(void)
{
    static Run run;
    run_start(&run, 5);
    Player *p = &run.player;
    int dmg = p->attack_damage, daggers = p->dagger_max, dash = p->dash_max;
    int hp = p->max_hp, meter = p->meter_max, recharge = p->dagger_recharge_time;
    fx reach = p->attack_radius, blast_r = p->blast_radius;
    boon_apply(&run, BOON_KEEN_BLADE);
    boon_apply(&run, BOON_TWIN_DAGGERS);
    boon_apply(&run, BOON_EXTRA_DASH);
    boon_apply(&run, BOON_THICK_SKIN);
    boon_apply(&run, BOON_OVERCHARGE);
    boon_apply(&run, BOON_QUICK_DRAW);
    boon_apply(&run, BOON_WIDE_ARC);
    boon_apply(&run, BOON_BIG_BANG);
    boon_apply(&run, BOON_PIERCING);
    CHECK_EQ(p->attack_damage, dmg + 1);
    CHECK_EQ(p->dagger_max, daggers + 1);
    CHECK_EQ(p->daggers, daggers + 1);
    CHECK_EQ(p->dash_max, dash + 1);
    CHECK_EQ(p->max_hp, hp + 1);
    CHECK_EQ(p->hp, p->max_hp);
    CHECK_EQ(p->meter_max, meter - 3);
    CHECK(p->dagger_recharge_time < recharge);
    CHECK(p->attack_radius > reach);
    CHECK(p->blast_radius > blast_r);
    CHECK_EQ(p->dagger_pierce, DAGGER_PIERCE + 1);
    CHECK_EQ(boon_gold(&run, 10), 10);
    boon_apply(&run, BOON_GOLD_RUSH);
    CHECK_EQ(boon_gold(&run, 10), 15);
}

static void dash_tests(void)
{
    static Run run;
    // Flame Dash: a trail that burns enemies standing in it, once each.
    empty_run(&run);
    boon_apply(&run, BOON_FLAME_DASH);
    Enemy *e = dummy(&run, FX(8.0), FX(6.0), 9);
    PlayerInput dash = { .move = DIR_SE, .dash = true };
    run_update(&run, &dash);
    step_n(&run, &idle, PLAYER_DASH_FRAMES + 2);
    int flames = 0;
    for (int i = 0; i < MAX_FLAMES; i++)
        flames += run.flames[i].active;
    CHECK(flames >= 2);
    e->x = FX(7.0);
    e->z = FX(6.0);
    step_n(&run, &idle, 10);
    CHECK(e->hp < 9);
    int hp = e->hp;
    step_n(&run, &idle, 10);
    // Several flames may overlap it, but each burns only once.
    CHECK(e->hp >= 9 - flames);
    CHECK(e->hp <= hp);
    step_n(&run, &idle, FLAME_LIFE + 5);
    for (int i = 0; i < MAX_FLAMES; i++)
        CHECK(!run.flames[i].active);

    // Shock Dash: the end of a dash knocks enemies away.
    empty_run(&run);
    boon_apply(&run, BOON_SHOCK_DASH);
    Enemy *s = dummy(&run, FX(9.5), FX(6.0), 9);
    run_update(&run, &dash);
    bool shocked = false;
    for (int i = 0; i < PLAYER_DASH_FRAMES + 3; i++) {
        run_update(&run, &idle);
        shocked |= 0 != (run.events2 & EV2_SHOCK);
    }
    CHECK(shocked);
    CHECK_EQ(s->hp, 9 - SHOCK_DAMAGE);
}

static void vampiric_tests(void)
{
    static Run run;
    int hearts = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        empty_run(&run);
        run.seed = seed;
        boon_apply(&run, BOON_VAMPIRIC);
        boon_apply(&run, BOON_VAMPIRIC);
        dummy(&run, FX(0.5), FX(11.5), 99);
        Enemy *e = dummy(&run, FX(6.7), FX(6.0), 1);
        PlayerInput atk = { .move = DIR_SE, .attack = true };
        run_update(&run, &atk);
        step_n(&run, &idle, PLAYER_ATTACK_FRAMES);
        CHECK(EST_DYING == e->state || !e->active);
        for (int i = 0; i < MAX_PICKUPS; i++)
            hearts += run.pickups[i].active && PICK_HEART == run.pickups[i].kind;
    }
    // 20% with two stacks.
    CHECK(hearts > 30 && hearts < 100);
}

void run_boons_tests(void)
{
    offer_tests();
    stat_tests();
    dash_tests();
    vampiric_tests();
}
