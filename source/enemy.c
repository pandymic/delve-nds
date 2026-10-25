// Enemy stats and AI.

#include "run.h"

const EnemyDef enemy_defs[ENEMY_TYPE_COUNT] = {
    [ENEMY_CHASER] = {
        .hp = 3, .damage = 1, .speed = FX(0.042), .radius = FX(0.3),
        .windup = 24, .attack = 10, .recover = 36,
        .range = FX(1.2), .attack_speed = FX(0.13), .cooldown = 0,
    },
    [ENEMY_SHOOTER] = {
        .hp = 2, .damage = 1, .speed = FX(0.032), .radius = FX(0.3),
        .windup = 32, .attack = 1, .recover = 20,
        .range = FX(8.0), .attack_speed = FX(0.07), .cooldown = 100,
    },
    // attack = the longest a charge lasts.
    [ENEMY_BRUTE] = {
        .hp = 6, .damage = 2, .speed = FX(0.03), .radius = FX(0.36),
        .windup = 30, .attack = 45, .recover = 36,
        .range = BRUTE_CHARGE_RANGE, .attack_speed = FX(0.2), .cooldown = 90,
    },
    [ENEMY_ARCHER] = {
        .hp = 2, .damage = 1, .speed = FX(0.034), .radius = FX(0.3),
        .windup = 36, .attack = 1, .recover = 24,
        .range = FX(9.0), .attack_speed = FX(0.14), .cooldown = 110,
    },
    [ENEMY_CASTER] = {
        .hp = 3, .damage = ZONE_DAMAGE, .speed = FX(0.026), .radius = FX(0.3),
        .windup = 30, .attack = 1, .recover = 30,
        .range = FX(9.0), .attack_speed = 0, .cooldown = 150,
    },
    [ENEMY_SHAMAN] = {
        .hp = 4, .damage = 1, .speed = FX(0.024), .radius = FX(0.32),
        .windup = 34, .attack = 1, .recover = 30,
        .range = FX(10.0), .attack_speed = 0, .cooldown = 240,
    },
    [ENEMY_TURRET] = {
        .hp = 6, .damage = 1, .speed = 0, .radius = FX(0.4),
        .windup = 30, .attack = 1, .recover = 10,
        .range = FX(12.0), .attack_speed = FX(0.06), .cooldown = 120,
    },
    // recover = the pause after a slime touches the player.
    // Bosses. attack = the longest a charge or leap lasts.
    [ENEMY_WARLORD] = {
        .hp = 50, .damage = 2, .speed = FX(0.034), .radius = FX(0.5),
        .windup = 36, .attack = 50, .recover = 40,
        .range = FX(9.0), .attack_speed = FX(0.21), .cooldown = 50,
    },
    [ENEMY_SLIME_KING] = {
        .hp = 55, .damage = 2, .speed = FX(0.026), .radius = FX(0.55),
        .windup = 30, .attack = 40, .recover = 40,
        .range = FX(10.0), .attack_speed = FX(0.06), .cooldown = 48,
    },
    [ENEMY_DUMMY] = {
        .hp = 99, .damage = 0, .speed = 0, .radius = FX(0.3),
        .windup = 0, .attack = 0, .recover = 0,
        .range = 0, .attack_speed = 0, .cooldown = 0,
    },
    [ENEMY_SLIME] = {
        .hp = 1, .damage = 1, .speed = FX(0.05), .radius = FX(0.25),
        .windup = 0, .attack = 0, .recover = 30,
        .range = 0, .attack_speed = 0, .cooldown = 0,
    },
    // The Caverns. Bat: range = how close it starts a dive, attack =
    // the dive's length.
    [ENEMY_BAT] = {
        .hp = 1, .damage = 1, .speed = FX(0.05), .radius = FX(0.25),
        .windup = 16, .attack = 22, .recover = 30,
        .range = FX(2.6), .attack_speed = FX(0.13), .cooldown = 40,
    },
    // Digger: range = how close it surfaces, windup = the telegraph while
    // it breaks out, recover = time above ground.
    [ENEMY_DIGGER] = {
        .hp = 4, .damage = 2, .speed = FX(0.045), .radius = FX(0.3),
        .windup = 36, .attack = 8, .recover = 90,
        .range = FX(1.0), .attack_speed = 0, .cooldown = 0,
    },
    [ENEMY_CRYSTAL] = {
        .hp = 5, .damage = 1, .speed = 0, .radius = FX(0.4),
        .windup = 26, .attack = 1, .recover = 10,
        .range = FX(12.0), .attack_speed = FX(0.065), .cooldown = 100,
    },
    // The Molten Depths. The fire slime is slower than a slime, and the
    // magma brute hits for one less than a brute: both add fire on top.
    [ENEMY_FIRE_SLIME] = {
        .hp = 2, .damage = 1, .speed = FX(0.038), .radius = FX(0.28),
        .windup = 0, .attack = 0, .recover = 30,
        .range = 0, .attack_speed = 0, .cooldown = 0,
    },
    [ENEMY_MAGMA_BRUTE] = {
        .hp = 7, .damage = 1, .speed = FX(0.03), .radius = FX(0.36),
        .windup = 30, .attack = 45, .recover = 36,
        .range = BRUTE_CHARGE_RANGE, .attack_speed = FX(0.2), .cooldown = 100,
    },
    [ENEMY_IMP] = {
        .hp = 3, .damage = 1, .speed = FX(0.036), .radius = FX(0.3),
        .windup = 30, .attack = 1, .recover = 24,
        .range = FX(8.0), .attack_speed = FX(0.075), .cooldown = 110,
    },
    [ENEMY_MAGMA_LORD] = {
        .hp = 80, .damage = 2, .speed = FX(0.024), .radius = FX(0.55),
        .windup = 32, .attack = 1, .recover = 30,
        .range = FX(12.0), .attack_speed = FX(0.085), .cooldown = 34,
    },
    // attack = the length of a hop.
    [ENEMY_GOLEM] = {
        .hp = 55, .damage = 2, .speed = FX(0.026), .radius = FX(0.55),
        .windup = 38, .attack = 24, .recover = 46,
        .range = FX(10.0), .attack_speed = FX(0.07), .cooldown = 62,
    },
};

#define ENEMY_HALF FX(0.25)

// Unit vector from the enemy to the player; returns the distance.
static fx dir_to_player(const Enemy *e, const Player *p, fx *ux, fx *uz)
{
    fx dx = p->x - e->x, dz = p->z - e->z;
    fx len = fx_sqrt(fx_mul(dx, dx) + fx_mul(dz, dz));
    if (len < FX(0.001)) {
        *ux = FX_ONE;
        *uz = 0;
        return 0;
    }
    *ux = fx_div(dx, len);
    *uz = fx_div(dz, len);
    return len;
}

static void step(Enemy *e, const Room *room, fx ux, fx uz, fx speed)
{
    room_move(room, &e->x, &e->z, fx_mul(ux, speed), fx_mul(uz, speed),
              ENEMY_HALF);
}

// Rotates (x, z) by the angle whose cosine and sine are c and s.
static void rotate(fx x, fx z, fx c, fx s, fx *rx, fx *rz)
{
    *rx = fx_mul(x, c) - fx_mul(z, s);
    *rz = fx_mul(x, s) + fx_mul(z, c);
}

// Line of sight is checked at the start of each enemy's frame
// (enemy_update), and again after it moves (refresh_sight), so attacks
// start from where the enemy really stands.
static bool sees_player(const Enemy *e)
{
    return e->sees;
}

static void refresh_sight(Enemy *e, const Run *run)
{
    e->sees = room_line_clear(&run->room, e->x, e->z, run->player.x,
                              run->player.z);
}

// Which way to walk towards the player: straight at them when nothing is in
// the way, else along the path map (run->path) to the next cell.
static void chase_direction(const Enemy *e, const Run *run, fx *ux, fx *uz)
{
    if (room_line_walkable(&run->room, e->x, e->z, run->player.x,
                           run->player.z))
        return;   // keep the straight line already in (ux, uz)
    int cx = FX_TO_INT(e->x), cz = FX_TO_INT(e->z);
    if (!room_in_bounds(cx, cz))
        return;
    const Room *r = &run->room;
    int best = run->path[cz][cx], bx = 0, bz = 0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = cx + dx, nz = cz + dz;
            if ((!dx && !dz) || !room_is_open(r, nx, nz)
                || run->path[nz][nx] >= best)
                continue;
            if (dx && dz && (!room_is_open(r, cx + dx, cz)
                             || !room_is_open(r, cx, cz + dz)))
                continue;
            best = run->path[nz][nx];
            bx = dx;
            bz = dz;
        }
    }
    if (!bx && !bz)
        return;
    fx tx = FX_INT(cx + bx) + FX_HALF - e->x;
    fx tz = FX_INT(cz + bz) + FX_HALF - e->z;
    fx len = fx_sqrt(fx_mul(tx, tx) + fx_mul(tz, tz));
    if (len < FX(0.01))
        return;
    *ux = fx_div(tx, len);
    *uz = fx_div(tz, len);
}

// One step towards the player along (ux, uz), or around whatever is in the
// way (chase_direction).
static void chase_step(Enemy *e, const Run *run, fx ux, fx uz, fx speed)
{
    chase_direction(e, run, &ux, &uz);
    step(e, &run->room, ux, uz, speed);
}

// True when the player is closer than reach (plus their size) to the enemy.
static bool player_in_reach(const Enemy *e, const Run *run, fx reach)
{
    const Player *p = &run->player;
    fx dx = p->x - e->x, dz = p->z - e->z;
    fx r = reach + PLAYER_HALF;
    return fx_mul(dx, dx) + fx_mul(dz, dz) < fx_mul(r, r);
}

static bool touching_player(const Enemy *e, const Run *run)
{
    return player_in_reach(e, run, enemy_defs[e->type].radius);
}

// A landing or burst at the enemy's feet: hurts the player within reach.
static void hurt_in_reach(Enemy *e, Run *run, fx reach)
{
    if (player_in_reach(e, run, reach))
        run_hurt_player(run, &run->player, e->damage, e->x, e->z, e->type);
}

static void touch_player(Enemy *e, Run *run)
{
    if (touching_player(e, run)) {
        Player *p = &run->player;
        run_hurt_player(run, p, e->damage, e->x, e->z, e->type);
        e->hit_done = true;
    }
}

static void start_windup(Enemy *e, fx ux, fx uz)
{
    e->state = EST_WINDUP;
    e->timer = enemy_defs[e->type].windup;
    e->aim_x = ux;
    e->aim_z = uz;
}

static void finish_attack(Enemy *e)
{
    const EnemyDef *d = &enemy_defs[e->type];
    e->state = EST_RECOVER;
    e->timer = d->recover;
}

// After a recovery, ranged enemies wait out their cooldown.
static void recover(Enemy *e)
{
    if (--e->timer <= 0) {
        e->state = EST_MOVE;
        e->cooldown = enemy_defs[e->type].cooldown;
        e->stunned = false;
    }
}

// A charge ran into something: props just ahead take the blow, and the
// enemy is dazed for `stun` frames.
static void slam_into(Enemy *e, Run *run, fx reach, int damage, int stun)
{
    fx hx = e->x + fx_mul(e->aim_x, reach);
    fx hz = e->z + fx_mul(e->aim_z, reach);
    run_hit_props(run, hx, hz, reach, damage);
    run->events |= EV_SLAM;
    run_record_hit(run, HIT_SLAM, 0, hx, hz);
    e->state = EST_RECOVER;
    e->timer = stun;
    e->stunned = true;
}

// Airborne bosses glide to their landing spot (tx, tz), arriving as the
// attack timer runs out.
static void glide(Enemy *e, Run *run)
{
    if (e->timer > 0) {
        fx sx = (e->tx - e->x) / e->timer, sz = (e->tz - e->z) / e->timer;
        room_move(&run->room, &e->x, &e->z, sx, sz, ENEMY_HALF);
    }
}

// Ranged enemies keep between near and far from the player, closing in
// along the path when they have no clear line, else strafing.
static void kite(Enemy *e, Run *run, fx near, fx far, fx ux, fx uz, fx dist)
{
    if (dist < near) {
        step(e, &run->room, -ux, -uz, e->speed);
    } else if (dist > far || !sees_player(e)) {
        chase_step(e, run, ux, uz, e->speed);
    } else {
        // Strafe sideways; flip now and then, and when blocked.
        fx sx = -uz * e->strafe, sz = ux * e->strafe;
        fx ox = e->x, oz = e->z;
        step(e, &run->room, sx, sz, e->speed / 2);
        if ((e->x == ox && e->z == oz) || 119 == (e->anim % 120))
            e->strafe = -e->strafe;
    }
    if (e->cooldown > 0)
        e->cooldown--;
    if (0 == e->cooldown)
        refresh_sight(e, run);   // about to decide on attacking
}

// Chaser and brute: melee

static void chaser_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);

    switch (e->state) {
    case EST_MOVE:
        // Lunges need a clear line; walking follows the path map.
        chase_step(e, run, ux, uz, e->speed);
        if (dist < d->range) {
            refresh_sight(e, run);
            if (sees_player(e))
                start_windup(e, ux, uz);
        }
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            e->state = EST_ATTACK;
            e->timer = d->attack;
            e->hit_done = false;
        }
        break;
    case EST_ATTACK:
        step(e, &run->room, e->aim_x, e->aim_z, d->attack_speed);
        if (!e->hit_done)
            touch_player(e, run);
        if (--e->timer <= 0)
            finish_attack(e);
        break;
    case EST_RECOVER:
        if (--e->timer <= 0)
            e->state = EST_MOVE;
        break;
    default:
        break;
    }
}

// The brute winds up, then charges in a straight line until it hits
// something. A wall leaves it dazed (and taking double damage); a pit in
// its way is the end of it.
static void brute_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);

    switch (e->state) {
    case EST_MOVE:
        chase_step(e, run, ux, uz, e->speed);
        if (e->cooldown > 0)
            e->cooldown--;
        if (0 == e->cooldown && dist < d->range) {
            refresh_sight(e, run);
            if (sees_player(e))
                start_windup(e, ux, uz);
        }
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            e->state = EST_ATTACK;
            e->timer = d->attack;
            e->hit_done = false;
            run->events |= EV_CHARGE;
        }
        break;
    case EST_ATTACK: {
        fx ox = e->x, oz = e->z;
        fx vx = fx_mul(e->aim_x, d->attack_speed);
        fx vz = fx_mul(e->aim_z, d->attack_speed);
        room_walk(&run->room, &e->x, &e->z, vx, vz, ENEMY_HALF);
        if (room_over_pit(&run->room, e->x, e->z)) {
            run_enemy_fell(run, e);
            return;
        }
        if (!e->hit_done)
            touch_player(e, run);
        // A magma brute's charge leaves fire behind it.
        if (ENEMY_MAGMA_BRUTE == e->type && 0 == e->timer % 15)
            run_spawn_zone_as(run, ox, oz, EMBER_TELEGRAPH, EMBER_RADIUS,
                              EMBER_DAMAGE, ENEMY_MAGMA_BRUTE);
        // Barely moved: it slammed into something.
        fx moved = fx_abs(e->x - ox) + fx_abs(e->z - oz);
        if (moved < d->attack_speed / 3) {
            slam_into(e, run, FX(0.5), 2, BRUTE_STUN);
            break;
        }
        if (--e->timer <= 0)
            finish_attack(e);
        break;
    }
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// Slimes just bump into you, then back off for a moment.
static void slime_update(Enemy *e, Run *run)
{
    fx ux, uz;
    dir_to_player(e, &run->player, &ux, &uz);
    switch (e->state) {
    case EST_MOVE:
        chase_step(e, run, ux, uz, e->speed);
        if (touching_player(e, run)) {
            touch_player(e, run);
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        // Hop back a little while recovering.
        step(e, &run->room, -ux, -uz, e->speed / 2);
        if (--e->timer <= 0)
            e->state = EST_MOVE;
        break;
    default:
        e->state = EST_MOVE;
        break;
    }
}

// Ranged

// What a boss's rings and fans are made of.
static ProjectileKind volley_kind(const Enemy *e)
{
    return ENEMY_GOLEM == e->type ? PROJ_SHARD : PROJ_FIREBALL;
}

static void fire(Run *run, const Enemy *e, ProjectileKind kind, fx ux, fx uz,
                 fx speed)
{
    // Volleys are many shots at once (bosses, crystals, imps): each hurts
    // for one heart, whatever the depth.
    bool volley = enemy_is_boss(e->type) || ENEMY_CRYSTAL == e->type
               || ENEMY_IMP == e->type;
    int damage = volley ? 1 : e->damage;
    Projectile *s = run_spawn_projectile(run, kind, e->x, e->z,
                                         fx_mul(ux, speed), fx_mul(uz, speed),
                                         damage);
    if (s)
        s->owner = e->type;
    run->events |= EV_ENEMY_SHOOT;
}

static void shooter_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    bool archer = ENEMY_ARCHER == e->type;
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);

    switch (e->state) {
    case EST_MOVE:
        kite(e, run, archer ? ARCHER_NEAR : SHOOTER_NEAR,
             archer ? ARCHER_FAR : SHOOTER_FAR, ux, uz, dist);
        if (0 == e->cooldown && dist < d->range && sees_player(e))
            start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            fire(run, e, archer ? PROJ_ARROW : PROJ_FIREBALL, e->aim_x,
                 e->aim_z, d->attack_speed);
            if (ENEMY_IMP == e->type) {
                // Two more, 20 degrees either side.
                const fx c = FX(0.93969), sn = FX(0.34202);
                for (int side = -1; side <= 1; side += 2) {
                    fx vx, vz;
                    rotate(e->aim_x, e->aim_z, c, sn * side, &vx, &vz);
                    fire(run, e, PROJ_FIREBALL, vx, vz, d->attack_speed);
                }
            }
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// The caster marks the floor under the player; the mark bursts a moment
// later (run.c). It needs no line of sight: the spell arcs over walls.
static void caster_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);

    switch (e->state) {
    case EST_MOVE:
        kite(e, run, CASTER_NEAR, CASTER_FAR, ux, uz, dist);
        if (0 == e->cooldown && dist < d->range)
            start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            run_spawn_zone(run, run->player.x, run->player.z);
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// Living enemies summoned by pool slot `owner`; only those of `type`, or
// of any type when type is ENEMY_TYPE_COUNT.
static int summoned_by(const Run *run, int owner, EnemyType type)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *m = &run->enemies[i];
        n += m->active && EST_DYING != m->state && m->owner == owner
          && (ENEMY_TYPE_COUNT == type || m->type == type);
    }
    return n;
}

static int slimes_of(const Run *run, int owner)
{
    return summoned_by(run, owner, ENEMY_SLIME);
}

// The shaman keeps away and calls slimes to either side of itself.
static void shaman_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    int self = (int)(e - run->enemies);
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);

    switch (e->state) {
    case EST_MOVE:
        kite(e, run, CASTER_NEAR, CASTER_FAR, ux, uz, dist);
        if (0 == e->cooldown && dist < d->range
            && slimes_of(run, self) < SHAMAN_MAX_SLIMES)
            start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            for (int side = -1; side <= 1; side += 2) {
                if (slimes_of(run, self) >= SHAMAN_MAX_SLIMES)
                    break;
                fx x = e->x - fx_mul(uz, FX(0.8)) * side;
                fx z = e->z + fx_mul(ux, FX(0.8)) * side;
                if (room_blocked(&run->room, x, z, ENEMY_HALF)
                    || room_over_pit(&run->room, x, z))
                    continue;
                Enemy *s = run_spawn_enemy(run, ENEMY_SLIME, x, z);
                if (!s)
                    break;
                s->timer = SUMMON_SPAWN_FRAMES;
                s->owner = (int8_t)self;
                s->elite = e->elite;
                run_record_hit(run, HIT_SUMMON, 0, x, z);
                run->events |= EV_SUMMON;
            }
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// The idol never moves; it fires four bolts at a time, alternating a plus
// and a cross.
static void turret_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);
    e->kb_x = e->kb_z = 0;

    switch (e->state) {
    case EST_MOVE:
        if (e->cooldown > 0)
            e->cooldown--;
        if (0 == e->cooldown && dist < d->range)
            start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            static const fx diag = FX(0.70711);
            static const fx plus[4][2] = {
                { FX_ONE, 0 }, { -FX_ONE, 0 }, { 0, FX_ONE }, { 0, -FX_ONE },
            };
            static const fx cross[4][2] = {
                { diag, diag }, { -diag, diag }, { diag, -diag }, { -diag, -diag },
            };
            const fx (*dirs)[2] = (e->pattern & 1) ? cross : plus;
            for (int i = 0; i < 4; i++)
                fire(run, e, PROJ_FIREBALL, dirs[i][0], dirs[i][1],
                     d->attack_speed);
            e->pattern++;
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// Bosses

enum { BOSS_CHARGE, BOSS_FAN, BOSS_SUMMON, BOSS_LEAP };

#define BOSS_MAX_MINIONS 4
#define BOSS_STUN        40
#define RING_SHOTS       8
#define LEAP_REACH       FX(1.2)   // landing hurts this close

// Shots in every direction (a ring); `turned` rotates it by 22.5 degrees
// to fill the gaps of a plain ring.
static void ring(Run *run, const Enemy *e, fx speed, bool turned)
{
    static const fx dirs[RING_SHOTS][2] = {
        { FX(1.0), 0 }, { FX(0.70711), FX(0.70711) }, { 0, FX(1.0) },
        { FX(-0.70711), FX(0.70711) }, { FX(-1.0), 0 },
        { FX(-0.70711), FX(-0.70711) }, { 0, FX(-1.0) },
        { FX(0.70711), FX(-0.70711) },
    };
    const fx c = FX(0.92388), s = FX(0.38268);   // cos, sin 22.5 degrees
    for (int i = 0; i < RING_SHOTS; i++) {
        fx x = dirs[i][0], z = dirs[i][1];
        if (turned)
            rotate(dirs[i][0], dirs[i][1], c, s, &x, &z);
        fire(run, e, volley_kind(e), x, z, speed);
    }
}

// Five shots fanned around the aim (-30..30 degrees).
static void fan(Run *run, const Enemy *e, fx ux, fx uz, fx speed)
{
    static const fx cs[5][2] = {
        { FX(0.86603), FX(-0.5) }, { FX(0.96593), FX(-0.25882) },
        { FX(1.0), 0 }, { FX(0.96593), FX(0.25882) }, { FX(0.86603), FX(0.5) },
    };
    for (int i = 0; i < 5; i++) {
        fx vx, vz;
        rotate(ux, uz, cs[i][0], cs[i][1], &vx, &vz);
        fire(run, e, volley_kind(e), vx, vz, speed);
    }
}

static int minions_of(const Run *run, int owner)
{
    return summoned_by(run, owner, ENEMY_TYPE_COUNT);
}

static void summon(Run *run, Enemy *e, EnemyType type, int count, fx ux,
                   fx uz)
{
    int self = (int)(e - run->enemies);
    for (int side = -1, made = 0; side <= 1 && made < count; side += 2) {
        if (minions_of(run, self) >= BOSS_MAX_MINIONS)
            break;
        fx x = e->x - fx_mul(uz, FX(1.2)) * side;
        fx z = e->z + fx_mul(ux, FX(1.2)) * side;
        if (room_blocked(&run->room, x, z, ENEMY_HALF)
            || room_over_pit(&run->room, x, z))
            continue;
        Enemy *m = run_spawn_enemy(run, type, x, z);
        if (!m)
            break;
        m->timer = SUMMON_SPAWN_FRAMES;
        m->owner = (int8_t)self;
        run_record_hit(run, HIT_SUMMON, 0, x, z);
        run->events |= EV_SUMMON;
        made++;
    }
}

// The Orc Warlord: charges (then answers with a ring of fire). Below half
// health he also fans fireballs and calls orcs.
static void warlord_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    bool enraged = e->hp * 2 <= e->max_hp;
    fx ux, uz;
    dir_to_player(e, &run->player, &ux, &uz);

    switch (e->state) {
    case EST_MOVE: {
        chase_step(e, run, ux, uz, e->speed);
        if (e->cooldown > 0) {
            e->cooldown--;
            break;
        }
        refresh_sight(e, run);
        int kind = enraged ? (int)(e->pattern % 3) : BOSS_CHARGE;
        if (BOSS_CHARGE == kind && !sees_player(e))
            kind = BOSS_FAN;
        e->attack_kind = (uint8_t)kind;
        e->pattern++;
        start_windup(e, ux, uz);
        break;
    }
    case EST_WINDUP:
        if (--e->timer > 0)
            break;
        switch (e->attack_kind) {
        case BOSS_CHARGE:
            e->state = EST_ATTACK;
            e->timer = d->attack;
            e->hit_done = false;
            run->events |= EV_CHARGE;
            break;
        case BOSS_FAN:
            fan(run, e, ux, uz, FX(0.08));
            finish_attack(e);
            break;
        default:
            summon(run, e, ENEMY_CHASER, 2, ux, uz);
            finish_attack(e);
            break;
        }
        break;
    case EST_ATTACK: {
        fx ox = e->x, oz = e->z;
        room_move(&run->room, &e->x, &e->z, fx_mul(e->aim_x, d->attack_speed),
                  fx_mul(e->aim_z, d->attack_speed), ENEMY_HALF * 2);
        if (!e->hit_done)
            touch_player(e, run);
        fx moved = fx_abs(e->x - ox) + fx_abs(e->z - oz);
        bool slammed = moved < d->attack_speed / 3;
        if (slammed || --e->timer <= 0) {
            ring(run, e, FX(0.06), false);
            if (slammed)
                slam_into(e, run, FX(0.7), 3, BOSS_STUN);
            else
                finish_attack(e);
        }
        break;
    }
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// The Slime King: leaps at where you stood and bursts on landing. Every
// fifth of his health lost splits two slimes off him.
static void slime_king_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    dir_to_player(e, &run->player, &ux, &uz);

    int lost = (e->max_hp - e->hp) * 5 / e->max_hp;
    while (e->pattern < lost && e->pattern < 4) {
        e->pattern++;
        summon(run, e, ENEMY_SLIME, 2, ux, uz);
    }

    switch (e->state) {
    case EST_MOVE:
        chase_step(e, run, ux, uz, e->speed);
        if (e->cooldown > 0)
            e->cooldown--;
        else
            start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            // The landing spot is where the player stands now.
            e->tx = run->player.x;
            e->tz = run->player.z;
            e->state = EST_ATTACK;
            e->timer = d->attack;
            e->attack_kind = BOSS_LEAP;
            run->events |= EV_CHARGE;
        }
        break;
    case EST_ATTACK:
        glide(e, run);
        if (--e->timer <= 0) {
            ring(run, e, FX(0.06), false);
            // Below half health a second, slower ring fills the gaps.
            if (e->hp * 2 <= e->max_hp)
                ring(run, e, FX(0.04), true);
            run->events |= EV_SLAM;
            run_record_hit(run, HIT_SLAM, 0, e->x, e->z);
            hurt_in_reach(e, run, LEAP_REACH);
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// The Caverns

// Flying: pits are open air.
static void fly(Enemy *e, const Room *room, fx ux, fx uz, fx speed)
{
    room_walk(room, &e->x, &e->z, fx_mul(ux, speed), fx_mul(uz, speed),
              ENEMY_HALF);
}

// The bat weaves in towards the player, hovers a moment (the telegraph),
// dives straight through, then flutters off.
static void bat_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);
    switch (e->state) {
    case EST_MOVE: {
        static const int8_t weave[8] = { 0, 3, 5, 3, 0, -3, -5, -3 };
        fx w = weave[(e->anim / 6) & 7] * FX(0.12);
        fly(e, &run->room, ux - fx_mul(uz, w), uz + fx_mul(ux, w), e->speed);
        if (e->cooldown > 0)
            e->cooldown--;
        else if (dist < d->range)
            start_windup(e, ux, uz);
        break;
    }
    case EST_WINDUP:
        if (--e->timer <= 0) {
            e->state = EST_ATTACK;
            e->timer = d->attack;
            e->hit_done = false;
            run->events |= EV_CHARGE;
        }
        break;
    case EST_ATTACK:
        fly(e, &run->room, e->aim_x, e->aim_z, d->attack_speed);
        if (!e->hit_done)
            touch_player(e, run);
        if (--e->timer <= 0)
            finish_attack(e);
        break;
    case EST_RECOVER:
        fly(e, &run->room, -ux, -uz, e->speed);
        recover(e);
        break;
    default:
        break;
    }
}

#define DIGGER_BURST FX(1.1)   // breaking out hurts this close

// The digger tunnels towards the player out of reach (EST_MOVE = buried),
// breaks out where it stops (a telegraph, then a burst), stands a while to
// be hit, then burrows again.
static void digger_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);
    switch (e->state) {
    case EST_MOVE:
        chase_step(e, run, ux, uz, e->speed);
        if (dist < d->range)
            start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            e->state = EST_ATTACK;
            e->timer = d->attack;
            run->events |= EV_SLAM;
            run_record_hit(run, HIT_SLAM, 0, e->x, e->z);
            hurt_in_reach(e, run, DIGGER_BURST);
        }
        break;
    case EST_ATTACK:
        if (--e->timer <= 0)
            finish_attack(e);
        break;
    case EST_RECOVER:
        // Above ground: plods after the player, then digs back in.
        chase_step(e, run, ux, uz, e->speed / 3);
        if (--e->timer <= 0) {
            e->state = EST_MOVE;
            run_record_hit(run, HIT_FALL, 0, e->x, e->z);   // a puff of dust
        }
        break;
    default:
        break;
    }
}

// Unit vectors every 20 degrees.
static const fx turn20[18][2] = {
    { FX(1.0), 0 }, { FX(0.93969), FX(0.34202) }, { FX(0.76604), FX(0.64279) },
    { FX(0.5), FX(0.86603) }, { FX(0.17365), FX(0.98481) },
    { FX(-0.17365), FX(0.98481) }, { FX(-0.5), FX(0.86603) },
    { FX(-0.76604), FX(0.64279) }, { FX(-0.93969), FX(0.34202) },
    { FX(-1.0), 0 }, { FX(-0.93969), FX(-0.34202) },
    { FX(-0.76604), FX(-0.64279) }, { FX(-0.5), FX(-0.86603) },
    { FX(-0.17365), FX(-0.98481) }, { FX(0.17365), FX(-0.98481) },
    { FX(0.5), FX(-0.86603) }, { FX(0.76604), FX(-0.64279) },
    { FX(0.93969), FX(-0.34202) },
};

// The crystal floats in place and fires six shards at a time, the pattern
// turning 20 degrees each volley.
static void crystal_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);
    e->kb_x = e->kb_z = 0;
    switch (e->state) {
    case EST_MOVE:
        if (e->cooldown > 0)
            e->cooldown--;
        if (0 == e->cooldown && dist < d->range)
            start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer <= 0) {
            for (int k = 0; k < 6; k++) {
                const fx *v = turn20[(e->pattern + k * 3) % 18];
                fire(run, e, PROJ_SHARD, v[0], v[1], d->attack_speed);
            }
            e->pattern++;
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

enum { GOLEM_HOP, GOLEM_FAN, GOLEM_RAIN, GOLEM_CALL };

#define GOLEM_HOP_REACH  FX(2.4)    // furthest a hop goes
#define GOLEM_SLAM       FX(1.25)   // landing hurts this close
#define GOLEM_RAIN_R     FX(1.7)

// The Crystal Golem: hops at the player and lands in a ring of shards,
// fans shards, and calls crystal rain (marks round the player that burst).
// Below half health he also calls crystals to his side.
static void golem_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    bool enraged = e->hp * 2 <= e->max_hp;
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);

    switch (e->state) {
    case EST_MOVE:
        chase_step(e, run, ux, uz, e->speed);
        if (e->cooldown > 0) {
            e->cooldown--;
            break;
        }
        e->attack_kind = (uint8_t)(e->pattern % (enraged ? 4 : 3));
        e->pattern++;
        start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer > 0)
            break;
        switch (e->attack_kind) {
        case GOLEM_HOP: {
            // Towards where the player stands, but no further than a hop.
            fx reach = fx_min(dist, GOLEM_HOP_REACH);
            e->tx = e->x + fx_mul(ux, reach);
            e->tz = e->z + fx_mul(uz, reach);
            e->state = EST_ATTACK;
            e->timer = d->attack;
            run->events |= EV_CHARGE;
            break;
        }
        case GOLEM_FAN:
            fan(run, e, ux, uz, FX(0.075));
            finish_attack(e);
            break;
        case GOLEM_RAIN: {
            Player *p = &run->player;
            run_spawn_zone_as(run, p->x, p->z, ZONE_TELEGRAPH, ZONE_RADIUS,
                              ZONE_DAMAGE, ENEMY_GOLEM);
            for (int k = 0; k < 3; k++) {
                const fx *v = turn20[(e->pattern * 5 + k * 6) % 18];
                run_spawn_zone_as(run, p->x + fx_mul(v[0], GOLEM_RAIN_R),
                                  p->z + fx_mul(v[1], GOLEM_RAIN_R),
                                  ZONE_TELEGRAPH, ZONE_RADIUS, ZONE_DAMAGE,
                                  ENEMY_GOLEM);
            }
            finish_attack(e);
            break;
        }
        default:
            summon(run, e, ENEMY_CRYSTAL, 2, ux, uz);
            finish_attack(e);
            break;
        }
        break;
    case EST_ATTACK:
        glide(e, run);
        if (--e->timer <= 0) {
            ring(run, e, FX(0.06), false);
            if (enraged)
                ring(run, e, FX(0.045), true);
            run->events |= EV_SLAM;
            run_record_hit(run, HIT_SLAM, 0, e->x, e->z);
            run_hit_props(run, e->x, e->z, GOLEM_SLAM, 2);
            hurt_in_reach(e, run, GOLEM_SLAM);
            finish_attack(e);
        }
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

// The Molten Depths

enum { LORD_WAVE, LORD_FAN, LORD_ERUPT, LORD_CALL };

#define LORD_FLOOD1_PCT  70    // health left when the outer ring floods
#define LORD_FLOOD2_PCT  40    // ...and the next ring

// The Magma Lord keeps his distance and sends fire: a wave of bursts
// rolling out towards the player, a fan of fireballs, an eruption round
// the player. Hurt, he calls fire slimes, ends every attack in a ring of
// fire, and floods the arena's edges with lava, one ring at a time.
static void magma_lord_update(Enemy *e, Run *run)
{
    const EnemyDef *d = &enemy_defs[e->type];
    bool enraged = e->hp * 2 <= e->max_hp;
    fx ux, uz;
    fx dist = dir_to_player(e, &run->player, &ux, &uz);

    int pct = e->hp * 100 / e->max_hp;
    if (pct <= LORD_FLOOD1_PCT)
        run_start_flood(run, 0);
    if (pct <= LORD_FLOOD2_PCT && (run->flooded & 1u))
        run_start_flood(run, 1);

    switch (e->state) {
    case EST_MOVE:
        kite(e, run, CASTER_NEAR, CASTER_FAR, ux, uz, dist);
        if (e->cooldown > 0)
            break;
        e->attack_kind = (uint8_t)(e->pattern % (enraged ? 4 : 3));
        e->pattern++;
        start_windup(e, ux, uz);
        break;
    case EST_WINDUP:
        if (--e->timer > 0)
            break;
        switch (e->attack_kind) {
        case LORD_WAVE:
            // Rows of five bursts, each row further out and later.
            for (int row = 1; row <= 3; row++) {
                fx cx = e->x + fx_mul(e->aim_x, FX(1.6) * row);
                fx cz = e->z + fx_mul(e->aim_z, FX(1.6) * row);
                for (int k = -2; k <= 2; k++)
                    run_spawn_zone_as(run, cx - fx_mul(e->aim_z, FX(1.4) * k),
                                      cz + fx_mul(e->aim_x, FX(1.4) * k),
                                      30 + row * 14, FX(0.8), ZONE_DAMAGE,
                                      ENEMY_MAGMA_LORD);
            }
            if (enraged)
                fan(run, e, ux, uz, d->attack_speed);
            break;
        case LORD_FAN:
            fan(run, e, ux, uz, d->attack_speed);
            break;
        case LORD_ERUPT: {
            Player *p = &run->player;
            run_spawn_zone_as(run, p->x, p->z, ZONE_TELEGRAPH, ZONE_RADIUS,
                              ZONE_DAMAGE, ENEMY_MAGMA_LORD);
            for (int k = 0; k < 6; k++) {
                const fx *v = turn20[(e->pattern * 4 + k * 3 + 2) % 18];
                run_spawn_zone_as(run, p->x + fx_mul(v[0], FX(1.5)),
                                  p->z + fx_mul(v[1], FX(1.5)),
                                  ZONE_TELEGRAPH + 10, FX(0.8), ZONE_DAMAGE,
                                  ENEMY_MAGMA_LORD);
            }
            break;
        }
        default:
            summon(run, e, ENEMY_FIRE_SLIME, 2, ux, uz);
            break;
        }
        // Enraged, every attack ends in a ring of fire as well.
        if (enraged)
            ring(run, e, FX(0.05), 0 != (e->pattern & 1));
        finish_attack(e);
        break;
    case EST_RECOVER:
        recover(e);
        break;
    default:
        break;
    }
}

void enemy_update(Enemy *e, Run *run)
{
    e->anim++;
    if (e->flash > 0)
        e->flash--;

    if (e->kb_x || e->kb_z) {
        // Knockback can push an enemy into a pit.
        room_walk(&run->room, &e->x, &e->z, e->kb_x, e->kb_z, ENEMY_HALF);
        e->kb_x = fx_mul(e->kb_x, FX(0.7));
        e->kb_z = fx_mul(e->kb_z, FX(0.7));
        if (fx_abs(e->kb_x) < FX(0.005) && fx_abs(e->kb_z) < FX(0.005))
            e->kb_x = e->kb_z = 0;
        if (EST_DYING != e->state && !enemy_flies(e->type)
            && room_over_pit(&run->room, e->x, e->z)) {
            run_enemy_fell(run, e);
            return;
        }
    }

    switch (e->state) {
    case EST_SPAWN:
        if (--e->timer <= 0)
            e->state = EST_MOVE;
        return;
    case EST_DYING:
        if (--e->timer <= 0)
            e->active = false;
        return;
    default:
        break;
    }

    if (run->player.hp <= 0)
        return;

    e->sees = ENEMY_SLIME == e->type || ENEMY_FIRE_SLIME == e->type
           || room_line_clear(&run->room, e->x, e->z, run->player.x,
                              run->player.z);
    switch (e->type) {
    case ENEMY_CHASER:  chaser_update(e, run); break;
    case ENEMY_BRUTE:   brute_update(e, run); break;
    case ENEMY_SLIME:   slime_update(e, run); break;
    case ENEMY_CASTER:  caster_update(e, run); break;
    case ENEMY_SHAMAN:  shaman_update(e, run); break;
    case ENEMY_TURRET:  turret_update(e, run); break;
    case ENEMY_DUMMY:   e->kb_x = e->kb_z = 0; break;   // just stands there
    case ENEMY_WARLORD: warlord_update(e, run); break;
    case ENEMY_SLIME_KING: slime_king_update(e, run); break;
    case ENEMY_BAT:     bat_update(e, run); break;
    case ENEMY_DIGGER:  digger_update(e, run); break;
    case ENEMY_CRYSTAL: crystal_update(e, run); break;
    case ENEMY_GOLEM:   golem_update(e, run); break;
    case ENEMY_FIRE_SLIME:  slime_update(e, run); break;
    case ENEMY_MAGMA_BRUTE: brute_update(e, run); break;
    case ENEMY_MAGMA_LORD:  magma_lord_update(e, run); break;
    default:            shooter_update(e, run); break;   // mage, archer
    }
}
