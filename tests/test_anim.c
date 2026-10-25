// Animation timing against gameplay timing, and enemy frames.

#include "test.h"
#include "anim.h"
#include "art_layout.h"
#include "run.h"

static void timing_tests(void)
{
    static const uint8_t ticks[3] = { 2, 3, 4 };
    AnimDef a = { 10, 3, 0, false, ticks };
    CHECK_EQ(anim_length(&a), 9);
    CHECK_EQ(anim_offset(&a, 0), 0);
    CHECK_EQ(anim_offset(&a, 1), 0);
    CHECK_EQ(anim_offset(&a, 2), 1);
    CHECK_EQ(anim_offset(&a, 5), 2);
    CHECK_EQ(anim_offset(&a, 100), 2);  // holds the last frame
    CHECK_EQ(anim_offset(&a, -5), 0);
    CHECK_EQ(anim_start_of(&a, 2), 5);
    a.loop = true;
    CHECK_EQ(anim_offset(&a, 9), 0);    // wraps
    CHECK_EQ(anim_offset(&a, 11), 1);

    AnimDef b = { 0, 4, 7, true, NULL };
    CHECK_EQ(anim_length(&b), 28);
    CHECK_EQ(anim_offset(&b, 27), 3);
    CHECK_EQ(anim_offset(&b, 28), 0);
}

static void player_tests(void)
{
    // The sword's arc frame (third) starts on the frame the swing lands.
    const AnimDef *sword = &player_anims[PANIM_SWORD];
    CHECK_EQ(anim_start_of(sword, 2), PLAYER_ATTACK_WINDUP);
    CHECK(anim_length(sword) >= PLAYER_ATTACK_FRAMES);

    // Every animation stays inside its direction's slots.
    for (int i = 0; i < PANIM_COUNT; i++)
        CHECK(player_anims[i].first + player_anims[i].count
              <= PF_FRAMES_PER_DIR);

    // Directions: rows 0-4, left-facing mirrored.
    int row;
    bool mirror;
    anim_dir_row(DIR_S, &row, &mirror);
    CHECK(0 == row && !mirror);
    anim_dir_row(DIR_W, &row, &mirror);
    CHECK(2 == row && mirror);
    anim_dir_row(DIR_NE, &row, &mirror);
    CHECK(3 == row && !mirror);
    anim_dir_row(DIR_N, &row, &mirror);
    CHECK(4 == row && !mirror);

    // Selection follows the simulation, and the hit frame of a swing shows
    // the arc.
    static Run run;
    run_start(&run, 7);
    Player *p = &run.player;
    p->facing = DIR_E;
    AnimTrack track = { PANIM_IDLE, 0 };
    CHECK_EQ(player_anim_select(p), PANIM_IDLE);
    SpriteFrame f = player_anim_frame(&track, p, 0);
    CHECK_EQ(f.slot, 2 * PF_FRAMES_PER_DIR + PF_IDLE);

    PlayerInput in = { .move = DIR_NONE, .attack = true };
    run_update(&run, &in);
    in.attack = false;
    bool saw_arc = false;
    for (int t = 1; t < 40 && PACT_ATTACK == p->action; t++) {
        f = player_anim_frame(&track, p, (uint32_t)t);
        if (player_attack_hits_now(p)) {
            CHECK_EQ(f.slot, 2 * PF_FRAMES_PER_DIR + PF_SWORD + 2);
            saw_arc = true;
        }
        run_update(&run, &in);
    }
    CHECK(saw_arc);

    // Hurt pose right after a hit, then back to idle; death when out of hp.
    player_hurt(p, 1, p->x + FX(1.0), p->z);
    CHECK_EQ(player_anim_select(p), PANIM_HURT);
    p->hurt_frames = PLAYER_IFRAMES_HURT - PLAYER_HURT_POSE;
    CHECK_EQ(player_anim_select(p), PANIM_IDLE);
    // Blast and dash invulnerability never show the hurt pose.
    p->hurt_frames = 0;
    p->iframes = PLAYER_IFRAMES_HURT;
    CHECK_EQ(player_anim_select(p), PANIM_IDLE);
    p->hp = 0;
    CHECK_EQ(player_anim_select(p), PANIM_DEATH);
    f = player_anim_frame(&track, p, 1000);
    f = player_anim_frame(&track, p, 5000);
    CHECK_EQ(f.slot % PF_FRAMES_PER_DIR, PF_DEATH + PF_DEATH_COUNT - 1);
}

static void facing_tests(void)
{
    // Ground offsets to screen directions (camera.h: +x is down-right,
    // +z down-left on screen).
    CHECK_EQ(anim_facing_towards(FX(1.0), 0), DIR_SE);
    CHECK_EQ(anim_facing_towards(0, FX(1.0)), DIR_SW);
    CHECK_EQ(anim_facing_towards(FX(1.0), FX(1.0)), DIR_S);
    CHECK_EQ(anim_facing_towards(-FX(1.0), -FX(1.0)), DIR_N);
    CHECK_EQ(anim_facing_towards(FX(1.0), -FX(1.0)), DIR_E);
    CHECK_EQ(anim_facing_towards(-FX(1.0), FX(1.0)), DIR_W);
    CHECK_EQ(anim_facing_towards(0, 0), DIR_S);

    // Every enemy state maps into its sheet.
    for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
        int per_dir = enemy_frames_per_dir((EnemyType)type);
        int dirs = enemy_dirs((EnemyType)type);
        for (int st = EST_SPAWN; st <= EST_DYING; st++) {
            for (int timer = 0; timer <= 60; timer += 3) {
                Enemy e = { .active = true, .type = (EnemyType)type,
                            .state = (EnemyState)st, .timer = timer,
                            .anim = timer * 7 };
                for (int d = 0; d < DIR_COUNT; d++) {
                    SpriteFrame f = enemy_anim_frame(&e, (Dir8)d);
                    CHECK(f.slot >= 0 && f.slot < dirs * per_dir);
                }
            }
        }
    }
}

void run_anim_tests(void)
{
    timing_tests();
    player_tests();
    facing_tests();
}
