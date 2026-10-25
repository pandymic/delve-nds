// The player: directions, movement, dash, attacks and getting hurt
// (entity.h).

#include "entity.h"

// Screen right = (+x, -z)/sqrt2, screen down = (+x, +z)/sqrt2.
static const fx dir_vec[DIR_COUNT][2] = {
    [DIR_E]  = { FX(0.70711), FX(-0.70711) },
    [DIR_SE] = { FX(1.0),     0 },
    [DIR_S]  = { FX(0.70711), FX(0.70711) },
    [DIR_SW] = { 0,           FX(1.0) },
    [DIR_W]  = { FX(-0.70711), FX(0.70711) },
    [DIR_NW] = { FX(-1.0),    0 },
    [DIR_N]  = { FX(-0.70711), FX(-0.70711) },
    [DIR_NE] = { 0,           FX(-1.0) },
};

void dir8_vector(Dir8 d, fx *x, fx *z)
{
    if (d < 0 || d >= DIR_COUNT) {
        *x = *z = 0;
        return;
    }
    *x = dir_vec[d][0];
    *z = dir_vec[d][1];
}

Dir8 dir8_from_input(int sx, int sy)
{
    static const Dir8 table[3][3] = {
        // sx = -1     0        1
        { DIR_NW, DIR_N,    DIR_NE }, // sy = -1
        { DIR_W,  DIR_NONE, DIR_E },  // sy = 0
        { DIR_SW, DIR_S,    DIR_SE }, // sy = 1
    };
    return table[sy + 1][sx + 1];
}

Dir8 dir8_towards(fx dx, fx dz)
{
    // Screen offset of a ground vector (see camera.h): right ~ dx - dz,
    // down ~ (dx + dz) * sin(pitch); the common factors cancel.
    fx sx = dx - dz;
    fx sy = fx_mul(dx + dz, FX(0.57358));
    fx ax = fx_abs(sx), ay = fx_abs(sy);
    const fx tan22 = FX(0.41421);
    if (0 == ax && 0 == ay)
        return DIR_S;
    if (ay <= fx_mul(ax, tan22))
        return sx > 0 ? DIR_E : DIR_W;
    if (ax <= fx_mul(ay, tan22))
        return sy > 0 ? DIR_S : DIR_N;
    if (sy > 0)
        return sx > 0 ? DIR_SE : DIR_SW;
    return sx > 0 ? DIR_NE : DIR_NW;
}

void player_init(Player *p, fx x, fx z)
{
    p->x = p->safe_x = x;
    p->z = p->safe_z = z;
    p->facing = DIR_S;
    p->action = PACT_IDLE;
    p->action_timer = 0;
    p->dash_max = PLAYER_DASH_CHARGES;
    p->dash_charges = p->dash_max;
    p->dash_recharge = 0;
    p->dash_recharge_time = PLAYER_DASH_RECHARGE;
    p->dash_gap = 0;
    p->iframes = 0;
    p->max_hp = p->hp = PLAYER_START_HP;
    p->attack_damage = 1;
    p->kb_x = p->kb_z = 0;
    p->anim_timer = 0;
    p->dagger_max = DAGGER_CHARGES;
    p->dagger_recharge_time = DAGGER_RECHARGE;
    p->dagger_pierce = DAGGER_PIERCE;
    p->dagger_damage = DAGGER_DAMAGE;
    p->iframes_hurt = PLAYER_IFRAMES_HURT;
    p->meter_max = BLAST_METER_MAX;
    p->attack_radius = PLAYER_ATTACK_RADIUS;
    p->blast_radius = BLAST_RADIUS;
    p->blast_damage = BLAST_DAMAGE;
    p->daggers = p->dagger_max;
    p->dagger_recharge = p->dagger_recharge_time;
    p->meter = 0;
}

static void move(Player *p, const Room *room, Dir8 d, fx speed)
{
    fx vx, vz;
    dir8_vector(d, &vx, &vz);
    // Pits do not stop the player: they fall in (run.c).
    room_walk(room, &p->x, &p->z, fx_mul(vx, speed), fx_mul(vz, speed),
              PLAYER_HALF);
}

// Refills one charge every `time` frames until `max`; the timer stays
// primed while full, so the first charge used takes a full wait.
static void recharge(int *charges, int max, int *timer, int time)
{
    if (*charges >= max) {
        *timer = time;
        return;
    }
    if (--*timer <= 0) {
        (*charges)++;
        *timer = time;
    }
}

static bool can_act(const Player *p)
{
    return PACT_IDLE == p->action || PACT_WALK == p->action;
}

// Actions aim where the D-pad points, if anywhere.
static void face_input(Player *p, const PlayerInput *in)
{
    if (DIR_NONE != in->move)
        p->facing = in->move;
}

void player_update(Player *p, const Room *room, const PlayerInput *in)
{
    if (p->dash_gap > 0)
        p->dash_gap--;
    recharge(&p->dash_charges, p->dash_max, &p->dash_recharge,
             p->dash_recharge_time);
    recharge(&p->daggers, p->dagger_max, &p->dagger_recharge,
             p->dagger_recharge_time);
    if (p->iframes > 0)
        p->iframes--;
    if (p->hurt_frames > 0)
        p->hurt_frames--;
    p->anim_timer++;

    if (p->kb_x || p->kb_z) {
        // Knockback stops at a pit's edge: you only fall in by your
        // own feet. Enemies can still be knocked in.
        room_move(room, &p->x, &p->z, p->kb_x, p->kb_z, PLAYER_HALF);
        p->kb_x = fx_mul(p->kb_x, FX(0.75));
        p->kb_z = fx_mul(p->kb_z, FX(0.75));
        if (fx_abs(p->kb_x) < FX(0.005) && fx_abs(p->kb_z) < FX(0.005))
            p->kb_x = p->kb_z = 0;
    }

    // Start a dash: allowed while walking, idle or attacking (cancels it).
    if (in->dash && PACT_DASH != p->action && p->dash_charges > 0
        && 0 == p->dash_gap) {
        face_input(p, in);
        p->action = PACT_DASH;
        p->action_timer = PLAYER_DASH_FRAMES;
        p->dash_charges--;
        p->dash_gap = PLAYER_DASH_GAP;
        if (p->iframes < PLAYER_IFRAMES_DASH)
            p->iframes = PLAYER_IFRAMES_DASH;
    } else if (in->blast && p->meter >= p->meter_max
               && PACT_BLAST != p->action) {
        // The blast can cancel anything, a dash included.
        p->action = PACT_BLAST;
        p->action_timer = BLAST_FRAMES;
        p->meter = 0;
        if (p->iframes < BLAST_IFRAMES)
            p->iframes = BLAST_IFRAMES;
    } else if (in->attack && can_act(p)) {
        face_input(p, in);
        p->action = PACT_ATTACK;
        p->action_timer = PLAYER_ATTACK_FRAMES;
    } else if (in->throw_dagger && can_act(p) && p->daggers > 0) {
        face_input(p, in);
        p->action = PACT_THROW;
        p->action_timer = DAGGER_THROW_FRAMES;
        p->daggers--;
    }

    switch (p->action) {
    case PACT_DASH:
        move(p, room, p->facing, PLAYER_DASH_SPEED);
        break;
    case PACT_ATTACK:
    case PACT_THROW:
        // Slow creep in the facing direction keeps attacks from feeling stuck.
        if (DIR_NONE != in->move)
            move(p, room, p->facing, PLAYER_ATTACK_SPEED);
        break;
    case PACT_BLAST:
        break;
    default:
        if (DIR_NONE != in->move) {
            p->facing = in->move;
            p->action = PACT_WALK;
            move(p, room, in->move, PLAYER_SPEED);
        } else {
            p->action = PACT_IDLE;
        }
        return;
    }
    // Timed actions end back at idle.
    if (--p->action_timer <= 0)
        p->action = PACT_IDLE;
}

bool player_attack_hits_now(const Player *p)
{
    return PACT_ATTACK == p->action
        && PLAYER_ATTACK_FRAMES - PLAYER_ATTACK_WINDUP == p->action_timer;
}

bool player_throw_releases_now(const Player *p)
{
    return PACT_THROW == p->action
        && DAGGER_THROW_FRAMES - DAGGER_RELEASE == p->action_timer;
}

bool player_blast_now(const Player *p)
{
    return PACT_BLAST == p->action
        && BLAST_FRAMES - BLAST_WINDUP == p->action_timer;
}

void player_add_meter(Player *p, int amount)
{
    p->meter += amount;
    if (p->meter > p->meter_max)
        p->meter = p->meter_max;
}

void player_attack_point(const Player *p, fx *x, fx *z)
{
    fx vx, vz;
    dir8_vector(p->facing, &vx, &vz);
    *x = p->x + fx_mul(vx, PLAYER_ATTACK_REACH);
    *z = p->z + fx_mul(vz, PLAYER_ATTACK_REACH);
}

bool player_hurt(Player *p, int damage, fx from_x, fx from_z)
{
    if (p->iframes > 0 || p->hp <= 0)
        return false;
    p->hp -= damage;
    if (p->hp < 0)
        p->hp = 0;
    p->iframes = p->iframes_hurt;
    p->hurt_frames = PLAYER_IFRAMES_HURT;

    fx dx = p->x - from_x, dz = p->z - from_z;
    fx len = fx_sqrt(fx_mul(dx, dx) + fx_mul(dz, dz));
    if (len > FX(0.01)) {
        p->kb_x = fx_mul(fx_div(dx, len), PLAYER_KNOCKBACK);
        p->kb_z = fx_mul(fx_div(dz, len), PLAYER_KNOCKBACK);
    }
    // Getting hit interrupts an attack or throw, but not a dash.
    if (PACT_ATTACK == p->action || PACT_THROW == p->action)
        p->action = PACT_IDLE;
    return true;
}
