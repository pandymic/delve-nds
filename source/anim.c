// Animation timing and frame choice for the player and enemies (anim.h).

#include <stddef.h>

#include "anim.h"
#include "art_layout.h"

int anim_length(const AnimDef *a)
{
    if (!a->ticks)
        return a->count * a->tick;
    int n = 0;
    for (int i = 0; i < a->count; i++)
        n += a->ticks[i];
    return n;
}

int anim_start_of(const AnimDef *a, int k)
{
    if (!a->ticks)
        return k * a->tick;
    int n = 0;
    for (int i = 0; i < k; i++)
        n += a->ticks[i];
    return n;
}

int anim_offset(const AnimDef *a, int t)
{
    if (t < 0)
        t = 0;
    int len = anim_length(a);
    if (a->loop && len > 0)
        t %= len;
    for (int i = 0; i < a->count; i++) {
        int d = a->ticks ? a->ticks[i] : a->tick;
        if (t < d)
            return i;
        t -= d;
    }
    return a->count - 1; // finished: hold the last frame
}

// Left-facing directions reuse the right-facing rows, mirrored.
static bool dir_is_left(Dir8 facing)
{
    return DIR_SW == facing || DIR_W == facing || DIR_NW == facing;
}

void anim_dir_row(Dir8 facing, int *row, bool *mirror)
{
    // Sheet rows: 0 down, 1 down-right, 2 right, 3 up-right, 4 up.
    static const int8_t rows[DIR_COUNT] = {
        [DIR_E] = 2, [DIR_SE] = 1, [DIR_S] = 0, [DIR_SW] = 1,
        [DIR_W] = 2, [DIR_NW] = 3, [DIR_N] = 4, [DIR_NE] = 3,
    };
    if (facing < 0 || facing >= DIR_COUNT)
        facing = DIR_S;
    *row = rows[facing];
    *mirror = dir_is_left(facing);
}

Dir8 anim_facing_towards(fx dx, fx dz)
{
    return dir8_towards(dx, dz);
}

// Player

// Sword: raise, raised, arc, follow-through. The arc frame starts exactly
// when the swing lands (PLAYER_ATTACK_WINDUP); a host test checks this.
static const uint8_t sword_ticks[PF_SWORD_COUNT] = { 3, 2, 5, 6 };
static const uint8_t death_ticks[PF_DEATH_COUNT] = { 10, 255 };
// Throw: wind back, release (DAGGER_RELEASE), follow through.
static const uint8_t throw_ticks[PF_THROW_COUNT] = { 2, 2, 4, 4 };

const AnimDef player_anims[PANIM_COUNT] = {
    [PANIM_IDLE]  = { PF_IDLE, PF_IDLE_COUNT, 32, true, NULL },
    [PANIM_WALK]  = { PF_WALK, PF_WALK_COUNT, 7, true, NULL },
    [PANIM_DASH]  = { PF_WALK + 1, 1, 1, false, NULL },
    [PANIM_SWORD] = { PF_SWORD, PF_SWORD_COUNT, 0, false, sword_ticks },
    [PANIM_THROW] = { PF_THROW, PF_THROW_COUNT, 0, false, throw_ticks },
    [PANIM_BLAST] = { PF_SWORD + 2, 1, 1, false, NULL },
    [PANIM_HURT]  = { PF_HURT, 1, 1, false, NULL },
    [PANIM_DEATH] = { PF_DEATH, PF_DEATH_COUNT, 0, false, death_ticks },
};

PlayerAnim player_anim_select(const Player *p)
{
    if (p->hp <= 0)
        return PANIM_DEATH;
    switch (p->action) {
    case PACT_ATTACK: return PANIM_SWORD;
    case PACT_THROW:  return PANIM_THROW;
    case PACT_BLAST:  return PANIM_BLAST;
    case PACT_DASH:   return PANIM_DASH;
    default: break;
    }
    if (p->hurt_frames > PLAYER_IFRAMES_HURT - PLAYER_HURT_POSE)
        return PANIM_HURT;
    return PACT_WALK == p->action ? PANIM_WALK : PANIM_IDLE;
}

SpriteFrame player_anim_frame(AnimTrack *track, const Player *p,
                              uint32_t now)
{
    PlayerAnim a = player_anim_select(p);
    if (a != track->anim) {
        track->anim = a;
        track->start = now;
    }
    // The swing follows the simulation's timer, so chained attacks restart
    // the animation and the arc always matches the hit frame.
    int t = PANIM_SWORD == a ? PLAYER_ATTACK_FRAMES - p->action_timer
          : PANIM_THROW == a ? DAGGER_THROW_FRAMES - p->action_timer
          : (int)(now - track->start);
    const AnimDef *def = &player_anims[a];
    int row;
    SpriteFrame f;
    // The blast spins through all eight directions.
    Dir8 facing = PANIM_BLAST == a ? (Dir8)((p->facing + t / 2) % DIR_COUNT)
                                   : p->facing;
    anim_dir_row(facing, &row, &f.mirror);
    f.slot = row * PF_FRAMES_PER_DIR + def->first + anim_offset(def, t);
    return f;
}

// Enemies

// Offset 0..n-1 through a countdown timer that started at total.
static int through(int total, int timer, int n)
{
    if (total <= 0)
        return n - 1;
    int k = (total - timer) * n / total;
    return k < 0 ? 0 : k >= n ? n - 1 : k;
}

// Where each enemy's animations sit in its sheet (art_layout.h).
typedef struct {
    uint8_t per_dir;     // frames per direction
    uint8_t dirs;        // 5 drawn directions, 3 (down, right, up: the
                         // Miniworld monsters) or 1; left is right mirrored
    uint8_t idle, walk, walk_count;
    uint8_t attack, attack_count;
    uint8_t hurt, death, death_count;
} EnemyArt;

static const EnemyArt enemy_art[ENEMY_TYPE_COUNT] = {
    [ENEMY_CHASER]  = { CF_FRAMES_PER_DIR, 5, CF_IDLE, CF_WALK, CF_WALK_COUNT,
                        CF_SWORD, CF_SWORD_COUNT, CF_HURT, CF_DEATH, CF_DEATH_COUNT },
    [ENEMY_SHOOTER] = { SF_FRAMES_PER_DIR, 5, SF_IDLE, SF_WALK, SF_WALK_COUNT,
                        SF_STAFF, SF_STAFF_COUNT, SF_HURT, SF_DEATH, SF_DEATH_COUNT },
    [ENEMY_BRUTE]   = { BF_FRAMES_PER_DIR, 5, BF_IDLE, BF_WALK, BF_WALK_COUNT,
                        BF_SWORD, BF_SWORD_COUNT, BF_HURT, BF_DEATH, BF_DEATH_COUNT },
    [ENEMY_ARCHER]  = { AF_FRAMES_PER_DIR, 5, AF_IDLE, AF_WALK, AF_WALK_COUNT,
                        AF_BOW, AF_BOW_COUNT, AF_HURT, AF_DEATH, AF_DEATH_COUNT },
    [ENEMY_CASTER]  = { KF_FRAMES_PER_DIR, 5, KF_IDLE, KF_WALK, KF_WALK_COUNT,
                        KF_STAFF, KF_STAFF_COUNT, KF_HURT, KF_DEATH, KF_DEATH_COUNT },
    [ENEMY_SHAMAN]  = { HF_FRAMES_PER_DIR, 5, HF_IDLE, HF_WALK, HF_WALK_COUNT,
                        HF_STAFF, HF_STAFF_COUNT, HF_HURT, HF_DEATH, HF_DEATH_COUNT },
    [ENEMY_TURRET]  = { 1, 1, 0, 0, 1, 0, 1, 0, 0, 1 },   // a still idol (props sheet)
    [ENEMY_SLIME]   = { MF_FRAMES_PER_DIR, 1, MF_IDLE, MF_WALK, MF_WALK_COUNT,
                        MF_WALK + 3, 1, MF_HURT, MF_DEATH, MF_DEATH_COUNT },
    // Bosses: the Warlord is a big brute, the King a big slime.
    [ENEMY_WARLORD] = { BF_FRAMES_PER_DIR, 5, BF_IDLE, BF_WALK, BF_WALK_COUNT,
                        BF_SWORD, BF_SWORD_COUNT, BF_HURT, BF_DEATH, BF_DEATH_COUNT },
    [ENEMY_SLIME_KING] = { MF_FRAMES_PER_DIR, 1, MF_IDLE, MF_WALK, MF_WALK_COUNT,
                           MF_WALK + 3, 1, MF_HURT, MF_DEATH, MF_DEATH_COUNT },
    // The Caverns (Miniworld sheets): the digger is a crab, the boss a
    // yeti. Bats and crystals are drawn by play.c (one frame here).
    [ENEMY_DIGGER]  = { RF_FRAMES_PER_DIR, 3, RF_IDLE, RF_WALK, RF_WALK_COUNT,
                        RF_ATTACK, RF_ATTACK_COUNT, RF_HURT, RF_DEATH, RF_DEATH_COUNT },
    [ENEMY_GOLEM]   = { YF_FRAMES_PER_DIR, 3, YF_IDLE, YF_WALK, YF_WALK_COUNT,
                        YF_ATTACK, YF_ATTACK_COUNT, YF_HURT, YF_DEATH, YF_DEATH_COUNT },
    // The Molten Depths: a recoloured slime; a bull, a demon and a dragon
    // (Miniworld sheets).
    [ENEMY_FIRE_SLIME] = { MF_FRAMES_PER_DIR, 1, MF_IDLE, MF_WALK, MF_WALK_COUNT,
                           MF_WALK + 3, 1, MF_HURT, MF_DEATH, MF_DEATH_COUNT },
    [ENEMY_MAGMA_BRUTE] = { UF_FRAMES_PER_DIR, 3, UF_IDLE, UF_WALK, UF_WALK_COUNT,
                            UF_ATTACK, UF_ATTACK_COUNT, UF_HURT, UF_DEATH, UF_DEATH_COUNT },
    [ENEMY_IMP]     = { IF_FRAMES_PER_DIR, 3, IF_IDLE, IF_WALK, IF_WALK_COUNT,
                        IF_ATTACK, IF_ATTACK_COUNT, IF_HURT, IF_DEATH, IF_DEATH_COUNT },
    [ENEMY_MAGMA_LORD] = { GF_FRAMES_PER_DIR, 3, GF_IDLE, GF_WALK, GF_WALK_COUNT,
                           GF_ATTACK, GF_ATTACK_COUNT, GF_HURT, GF_DEATH, GF_DEATH_COUNT },
    [ENEMY_BAT]     = { 1, 1, 0, 0, 1, 0, 1, 0, 0, 1 },
    [ENEMY_CRYSTAL] = { 1, 1, 0, 0, 1, 0, 1, 0, 0, 1 },
    // The dummy only idles and flinches.
    [ENEMY_DUMMY]   = { DF_FRAMES_PER_DIR, 1, DF_IDLE, DF_IDLE, DF_IDLE_COUNT,
                        DF_IDLE, 1, DF_HURT, DF_HURT, 1 },
};

int enemy_frames_per_dir(EnemyType t)
{
    return enemy_art[t].per_dir;
}

int enemy_dirs(EnemyType t)
{
    return enemy_art[t].dirs;
}

SpriteFrame enemy_anim_frame(const Enemy *e, Dir8 facing)
{
    const EnemyDef *def = &enemy_defs[e->type];
    const EnemyArt *a = &enemy_art[e->type];
    if (ENEMY_TURRET == e->type || ENEMY_BAT == e->type
        || ENEMY_CRYSTAL == e->type)
        return (SpriteFrame){ 0, false };   // drawn by play.c
    bool melee = ENEMY_CHASER == e->type || ENEMY_BRUTE == e->type
              || ENEMY_WARLORD == e->type || ENEMY_DIGGER == e->type
              || ENEMY_GOLEM == e->type || ENEMY_MAGMA_BRUTE == e->type;

    int k;
    switch (e->state) {
    case EST_MOVE:
        k = a->walk + (e->anim / (ENEMY_SLIME == e->type
                                  || ENEMY_FIRE_SLIME == e->type ? 5 : 8))
                      % a->walk_count;
        break;
    case EST_WINDUP: {
        // Weapon raised across the telegraph (the first two frames).
        int raise = a->attack_count > 2 ? 2 : a->attack_count;
        k = a->attack + through(def->windup, e->timer, raise);
        break;
    }
    case EST_ATTACK:
        if (enemy_charges(e->type))   // a fast run
            k = a->walk + (e->anim / 3) % a->walk_count;
        else
            k = a->attack + (a->attack_count > 2 ? 2 : a->attack_count - 1);
        break;
    case EST_RECOVER:
        if (e->stunned)
            k = a->hurt;
        else if (melee)
            k = a->attack + a->attack_count - 1;   // follow-through
        else
            k = a->idle;
        break;
    case EST_DYING:
        k = a->death + through(ENEMY_DYING_FRAMES, e->timer, a->death_count);
        break;
    default: // EST_SPAWN
        k = a->idle + (e->anim / 24) % 2;
        break;
    }
    if (e->flash > 0 && EST_DYING != e->state)
        k = a->hurt;

    SpriteFrame f;
    int row = 0;
    if (1 == a->dirs) {
        f.mirror = dir_is_left(facing);
    } else {
        anim_dir_row(facing, &row, &f.mirror);
        if (3 == a->dirs) {
            // Down, right, up: the diagonals show the side.
            static const int8_t three[5] = { 0, 1, 1, 1, 2 };
            row = three[row];
        }
    }
    f.slot = row * a->per_dir + k;
    return f;
}
