// The run simulation: rooms, combat, props, pickups, traps and floods
// (run.h).

#include <string.h>

#include "cheat.h"
#include "difficulty.h"
#include "rng.h"
#include "meta.h"
#include "roomgen.h"
#include "run.h"

static void update_flood(Run *run);
static void cool_floods(Run *run);

#define ENEMY_SEPARATION FX(0.6)
#define ENEMY_KNOCKBACK  FX(0.24)

#define DOOR_TOUCH       FX(0.06)  // how close to the wall counts as entering
#define DOOR_HALF_WIDTH  FX(0.42)
#define CHEST_REACH      FX(0.95)

static fx dist2(fx ax, fx az, fx bx, fx bz)
{
    fx dx = ax - bx, dz = az - bz;
    return fx_mul(dx, dx) + fx_mul(dz, dz);
}

// True when (ax, az) is closer than reach to (bx, bz).
static bool within(fx ax, fx az, fx bx, fx bz, fx reach)
{
    return dist2(ax, az, bx, bz) < fx_mul(reach, reach);
}

void run_start_hub(Run *run)
{
    memset(run, 0, sizeof(*run));
    run->seed = 0xCA3Bu;
    run->depth = 0;
    player_init(&run->player, FX(6.5), FX(10.3));
    roomgen_hub(&run->room);
    run_begin_room(run);
}

bool run_at_shrine(const Run *run)
{
    if (!run->room.hub)
        return false;
    return within(run->player.x, run->player.z, HUB_SHRINE_X, HUB_SHRINE_Z,
                  HUB_STATION_REACH);
}

void run_start(Run *run, uint32_t seed)
{
    run_start_at(run, seed, 0);
}

void run_start_at(Run *run, uint32_t seed, int area)
{
    int depth = area_first_depth(area);
    memset(run, 0, sizeof(*run));
    run->seed = seed;
    run->hurt_by = CAUSE_UNKNOWN;
    run->depth = run->start_depth = depth;
    player_init(&run->player, FX(6.0), FX(10.5));
    roomgen_generate(&run->room, roomgen_seed(seed, depth, 0), depth, RT_COMBAT,
                     SIDE_PZ, ROOM_SIZE / 2);
    run_begin_room(run);
    // Boons for the areas skipped: the first of each offer.
    for (int i = 0; i < area * WAYPOINT_BOONS; i++) {
        BoonId offer[BOON_CHOICES];
        if (boon_offer(run, offer) > 0)
            boon_apply(run, offer[0]);
        run->rooms_cleared++;   // a fresh offer each time...
    }
    run->rooms_cleared = 0;     // ...which counts no rooms
}

#define SWIFT_DASH_RECHARGE 32

static bool has_upgrade(uint32_t upgrades, UpgradeId id)
{
    return (upgrades >> id) & 1u;
}

void run_apply_upgrades(Run *run, uint32_t upgrades)
{
    Player *p = &run->player;
    if (has_upgrade(upgrades, UPG_VITALITY)) {
        p->max_hp++;
        p->hp++;
    }
    if (has_upgrade(upgrades, UPG_SWIFT_DASH))
        p->dash_recharge_time = SWIFT_DASH_RECHARGE;
    if (has_upgrade(upgrades, UPG_HEAVY_BLADE))
        p->attack_damage++;
    if (has_upgrade(upgrades, UPG_KEEN_EDGE))
        p->attack_damage++;
    if (has_upgrade(upgrades, UPG_VITALITY2)) {
        p->max_hp++;
        p->hp++;
    }
    if (has_upgrade(upgrades, UPG_QUIVER)) {
        p->dagger_max++;
        p->daggers++;
    }
    if (has_upgrade(upgrades, UPG_KINDLING))
        p->meter = p->meter_max;
    if (has_upgrade(upgrades, UPG_FLETCHING))
        p->dagger_recharge_time = p->dagger_recharge_time * FLETCHING_PCT / 100;
    if (has_upgrade(upgrades, UPG_BARBED_TIPS))
        p->dagger_pierce++;
    if (has_upgrade(upgrades, UPG_HUNTERS_MARK))
        p->dagger_damage++;
    if (has_upgrade(upgrades, UPG_IRON_HIDE))
        p->iframes_hurt = PLAYER_IFRAMES_HURT * IRON_HIDE_PCT / 100;
    run->second_wind = has_upgrade(upgrades, UPG_SECOND_WIND);
    run->deep_pockets = has_upgrade(upgrades, UPG_DEEP_POCKETS);
    run->thorns = has_upgrade(upgrades, UPG_THORNS);
    run->mending = has_upgrade(upgrades, UPG_MENDING);
}

void run_begin_room(Run *run)
{
    Room *r = &run->room;
    Player *p = &run->player;
    run_reset_room(run);
    run->chest_opened = false;
    run->decor_broken = 0;
    memset(run->fuse, 0, sizeof(run->fuse));
    memset(run->crate_broken, 0, sizeof(run->crate_broken));
    memset(run->trap_hits, 0, sizeof(run->trap_hits));
    memset(run->crust, 0, sizeof(run->crust));
    memset(run->flood_cells, 0, sizeof(run->flood_cells));
    run->flood_ring = -1;
    run->flooded = 0;
    for (int i = 0; i < MAX_DECOR; i++)
        run->decor_hp[i] = BARREL_HP;
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            run->crate_hp[z][x] = CRATE_HP;

    p->x = p->safe_x = r->spawn_x;
    p->z = p->safe_z = r->spawn_z;
    p->kb_x = p->kb_z = 0;
    room_path_map(r, FX_TO_INT(p->x), FX_TO_INT(p->z), run->path);
    p->action = PACT_IDLE;
    // Face into the room.
    static const Dir8 inward[SIDE_COUNT] = { DIR_SE, DIR_SW, DIR_NW, DIR_NE };
    if (r->entry_side >= 0)
        p->facing = inward[r->entry_side];

    Difficulty d = difficulty(run->depth);
    for (int i = 0; i < r->enemy_count; i++) {
        const EnemySpawn *s = &r->enemies[i];
        Enemy *e = run_spawn_enemy(run, (EnemyType)s->type, s->x, s->z);
        if (!e)
            break;
        int hp = (e->max_hp * d.hp_pct + 50) / 100;
        if (s->elite) {
            e->elite = true;
            hp *= ELITE_HP_MULT;
            e->speed = e->speed * ELITE_SPEED_PCT / 100;
        }
        e->hp = e->max_hp = hp > 0 ? hp : 1;
        e->damage += d.damage_bonus;
    }

    if (RT_REST == r->type && !r->hub && p->hp < p->max_hp) {
        int heal = imin(REWARD_REST_HEAL, p->max_hp - p->hp);
        p->hp += heal;
        run->reward_amount = heal;
        run->events |= EV_HEAL;
    }
}

void run_enter_door(Run *run, int door)
{
    const Door d = run->room.doors[door];
    run->depth++;
    roomgen_generate(&run->room, roomgen_seed(run->seed, run->depth, door),
                     run->depth, d.leads_to, side_opposite(d.side), d.pos);
    run_begin_room(run);
}

static bool touching_door(const Player *p, const Door *d)
{
    fx cx, cz;
    door_center(d->side, d->pos, &cx, &cz);
    fx along, across;
    if (SIDE_NX == d->side || SIDE_PX == d->side) {
        across = fx_abs(p->x - cx);
        along = fx_abs(p->z - cz);
    } else {
        across = fx_abs(p->z - cz);
        along = fx_abs(p->x - cx);
    }
    return across <= PLAYER_HALF + DOOR_TOUCH && along <= DOOR_HALF_WIDTH;
}

static void check_room_features(Run *run)
{
    Player *p = &run->player;
    const Room *r = &run->room;

    if (RT_TREASURE == r->type && !run->chest_opened && r->feature_x >= 0) {
        fx cx = FX_INT(r->feature_x) + FX_HALF;
        fx cz = FX_INT(r->feature_z) + FX_HALF;
        if (within(p->x, p->z, cx, cz, CHEST_REACH)) {
            run->chest_opened = true;
            run->reward_amount = boon_gold(run, REWARD_CHEST_GOLD(run->depth));
            run->gold += run->reward_amount;
            run->events |= EV_CHEST_OPEN | EV_GOLD;
        }
    }

    // Doors open once any boon on offer has been taken.
    if (ROOM_CLEAR == run->status && !run->boon_pending) {
        for (int i = 0; i < r->door_count; i++) {
            if (touching_door(p, &r->doors[i])) {
                run->exit_door = i;
                run->events |= EV_DOOR_ENTER;
                break;
            }
        }
    }
}

void run_reset_room(Run *run)
{
    memset(run->enemies, 0, sizeof(run->enemies));
    memset(run->shots, 0, sizeof(run->shots));
    memset(run->daggers, 0, sizeof(run->daggers));
    memset(run->zones, 0, sizeof(run->zones));
    memset(run->flames, 0, sizeof(run->flames));
    run->boon_pending = false;
    memset(run->pickups, 0, sizeof(run->pickups));
    run->hit_count = 0;
    run->status = ROOM_FIGHT;
    run->frames = 0;
    run->events = 0;
}

void run_load_test_room(Run *run)
{
    room_make_test(&run->room);
    run_reset_room(run);
    run->room.door_count = 0;
    run_spawn_enemy(run, ENEMY_CHASER, FX(2.0), FX(2.0));
    run_spawn_enemy(run, ENEMY_CHASER, FX(10.0), FX(2.0));
    run_spawn_enemy(run, ENEMY_CHASER, FX(6.0), FX(4.5));
    run_spawn_enemy(run, ENEMY_SHOOTER, FX(1.5), FX(6.0));
    run_spawn_enemy(run, ENEMY_SHOOTER, FX(10.5), FX(7.0));
    run->player.x = FX(6.0);
    run->player.z = FX(10.5);
}

Enemy *run_spawn_enemy(Run *run, EnemyType type, fx x, fx z)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &run->enemies[i];
        if (e->active)
            continue;
        const EnemyDef *d = &enemy_defs[type];
        memset(e, 0, sizeof(*e));
        e->active = true;
        e->type = type;
        e->state = EST_SPAWN;
        e->timer = ENEMY_SPAWN_FRAMES;
        e->x = x;
        e->z = z;
        e->hp = e->max_hp = d->hp;
        e->damage = d->damage;
        e->speed = d->speed;
        e->cooldown = d->cooldown / 2 + (i * 17) % 40; // desync shooters
        e->strafe = (i & 1) ? 1 : -1;
        e->owner = -1;
        return e;
    }
    return NULL;
}

bool run_hurt_player(Run *run, Player *p, int damage, fx from_x, fx from_z,
                     uint8_t cause)
{
    if (cheat_on(run->cheats, CHEAT_GOD))
        return false;   // shots fly on through, as through a dash
    if (cheat_on(run->cheats, CHEAT_HARD))
        damage *= 2;
    if (!player_hurt(p, damage, from_x, from_z))
        return false;
    run->events |= EV_PLAYER_HURT;
    run->hurt_by = cause;
    // Thorns lash out after the enemies' turn (run_update), so no enemy is
    // hurt in the middle of its own update.
    if (run->thorns)
        run->thorns_due = true;
    return true;
}

Projectile *run_spawn_projectile(Run *run, ProjectileKind kind, fx x, fx z,
                                 fx vx, fx vz, int damage)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *s = &run->shots[i];
        if (s->active)
            continue;
        s->active = true;
        s->kind = (uint8_t)kind;
        s->owner = CAUSE_UNKNOWN;
        s->x = x;
        s->z = z;
        s->vx = vx;
        s->vz = vz;
        s->life = PROJECTILE_LIFE;
        s->damage = damage;
        return s;
    }
    return NULL;
}

static bool enemy_alive(const Enemy *e)
{
    return e->active && EST_DYING != e->state;
}

int run_enemies_alive(const Run *run)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        n += enemy_alive(&run->enemies[i])
          && ENEMY_DUMMY != run->enemies[i].type;
    return n;
}

// True when an enemy's body overlaps the circle of radius reach at (x, z).
static bool enemy_within(const Enemy *e, fx x, fx z, fx reach)
{
    return within(x, z, e->x, e->z, reach + enemy_defs[e->type].radius);
}

// True when the living player overlaps the circle of radius reach at (x, z).
static bool player_within(const Player *p, fx x, fx z, fx reach)
{
    return p->hp > 0 && within(x, z, p->x, p->z, reach + PLAYER_HALF);
}

// Knockback of the given strength pushing an enemy straight away from
// (x, z). When the two coincide there is no direction: the push is
// (0, fallback_z).
static void push_away(const Enemy *e, fx x, fx z, fx strength, fx fallback_z,
                      fx *kx, fx *kz)
{
    fx dx = e->x - x, dz = e->z - z;
    fx len = fx_sqrt(fx_mul(dx, dx) + fx_mul(dz, dz));
    if (len > FX(0.01)) {
        *kx = fx_mul(fx_div(dx, len), strength);
        *kz = fx_mul(fx_div(dz, len), strength);
    } else {
        *kx = 0;
        *kz = fallback_z;
    }
}

bool run_pot_has_gold(const Run *run, int decor_index)
{
    uint32_t h = rng_hash3(run->room.seed, 0x9071u, (uint32_t)decor_index);
    return h % 100 < REWARD_POT_CHANCE;
}

Zone *run_spawn_zone(Run *run, fx x, fx z)
{
    return run_spawn_zone_as(run, x, z, ZONE_TELEGRAPH, ZONE_RADIUS,
                             ZONE_DAMAGE, ENEMY_CASTER);
}

Zone *run_spawn_zone_as(Run *run, fx x, fx z, int timer, fx radius,
                        int damage, uint8_t owner)
{
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *zn = &run->zones[i];
        if (zn->active)
            continue;
        zn->active = true;
        zn->x = x;
        zn->z = z;
        zn->timer = timer;
        zn->radius = radius;
        zn->damage = (uint8_t)damage;
        zn->owner = owner;
        run->events |= EV_ZONE_MARKED;
        return zn;
    }
    return NULL;
}

void run_record_hit(Run *run, HitKind kind, int amount, fx x, fx z)
{
    if (run->hit_count >= MAX_HITS)
        return;
    RunHit *h = &run->hits[run->hit_count++];
    h->kind = (uint8_t)kind;
    h->amount = (uint8_t)(amount > 255 ? 255 : amount);
    h->x = x;
    h->z = z;
}

// Pickups

Pickup *run_spawn_pickup(Run *run, PickupKind kind, int value, fx x, fx z)
{
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *k = &run->pickups[i];
        if (k->active)
            continue;
        // A little sideways hop, different for each slot and frame.
        uint32_t h = rng_hash3(run->room.seed, run->frames, (uint32_t)i);
        memset(k, 0, sizeof(*k));
        k->active = true;
        k->kind = (uint8_t)kind;
        k->value = (uint8_t)value;
        k->x = x;
        k->z = z;
        k->y = FX(0.2);
        k->vy = FX(0.07);
        k->vx = (fx)(h % 41) * FX(0.001) - FX(0.02);
        k->vz = (fx)((h >> 8) % 41) * FX(0.001) - FX(0.02);
        return k;
    }
    return NULL;
}

// Coins and hearts hop and settle, then fly to the player when near (from
// anywhere once the room is clear) and are collected on touch.
static void update_pickups(Run *run)
{
    Player *p = &run->player;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *k = &run->pickups[i];
        if (!k->active)
            continue;
        if (k->age < 255)
            k->age++;
        // Hop and settle.
        room_move(&run->room, &k->x, &k->z, k->vx, k->vz, FX(0.1));
        k->y += k->vy;
        k->vy -= FX(0.008);
        if (k->y <= 0) {
            k->y = 0;
            k->vy = 0;
            k->vx = k->vz = 0;
        }
        if (k->age < PICKUP_SETTLE || p->hp <= 0)
            continue;
        // A heart only goes to a hurt player.
        if (PICK_HEART == k->kind && p->hp >= p->max_hp)
            continue;
        fx dx = p->x - k->x, dz = p->z - k->z;
        fx d2 = dist2(p->x, p->z, k->x, k->z);
        if (d2 < fx_mul(PICKUP_REACH, PICKUP_REACH)) {
            k->active = false;
            if (PICK_COIN == k->kind) {
                run->gold += k->value;
                run->reward_amount = k->value;
                run->events |= EV_COIN;
                run_record_hit(run, HIT_COIN, k->value, k->x, k->z);
            } else {
                p->hp++;
                run->events |= EV_HEART;
                run_record_hit(run, HIT_HEART, 1, k->x, k->z);
            }
            continue;
        }
        if (ROOM_CLEAR == run->status
            || d2 < fx_mul(PICKUP_MAGNET, PICKUP_MAGNET)) {
            fx len = fx_sqrt(d2);
            k->x += fx_mul(fx_div(dx, len), PICKUP_MAGNET_SPEED);
            k->z += fx_mul(fx_div(dz, len), PICKUP_MAGNET_SPEED);
        }
    }
}

// Destructible props

static uint32_t prop_roll(const Run *run, uint32_t tag, int index)
{
    return rng_hash3(run->room.seed, tag, (uint32_t)index) % 100;
}

static void break_decor(Run *run, int i)
{
    const Decor *d = &run->room.decor[i];
    run->decor_broken |= 1u << i;
    if (DECOR_POT == d->kind) {
        run->events |= EV_POT_BREAK;
        run_record_hit(run, HIT_POT, 0, d->x, d->z);
        if (run_pot_has_gold(run, i))
            run_spawn_pickup(run, PICK_COIN, boon_gold(run, REWARD_POT_GOLD),
                             d->x, d->z);
    } else if (DECOR_BARREL == d->kind) {
        run->events |= EV_BARREL_BREAK;
        run_record_hit(run, HIT_BARREL, 0, d->x, d->z);
        uint32_t roll = prop_roll(run, 0xBA44u, i);
        if (roll < REWARD_BARREL_HEART)
            run_spawn_pickup(run, PICK_HEART, 1, d->x, d->z);
        else if (roll < REWARD_BARREL_HEART + REWARD_BARREL_COIN)
            run_spawn_pickup(run, PICK_COIN, boon_gold(run, 1), d->x, d->z);
    }
}

// Circle (x, z, r) against an obstacle cell's box.
static bool circle_hits_cell(fx x, fx z, fx r, int cx, int cz, CellType t)
{
    fx in = room_cell_inset(t);
    fx bx0 = FX_INT(cx) + in, bx1 = FX_INT(cx + 1) - in;
    fx bz0 = FX_INT(cz) + in, bz1 = FX_INT(cz + 1) - in;
    fx px = fx_clamp(x, bx0, bx1), pz = fx_clamp(z, bz0, bz1);
    return dist2(x, z, px, pz) < fx_mul(r, r);
}

static void damage_crate(Run *run, int cx, int cz, int damage)
{
    fx x = FX_INT(cx) + FX_HALF, z = FX_INT(cz) + FX_HALF;
    if (run->crate_hp[cz][cx] > damage) {
        run->crate_hp[cz][cx] = (uint8_t)(run->crate_hp[cz][cx] - damage);
        run_record_hit(run, HIT_CHIP, 0, x, z);
        return;
    }
    run->crate_hp[cz][cx] = 0;
    run->room.cells[cz][cx] = CELL_FLOOR;
    run->crate_broken[cz][cx] = true;
    run->events |= EV_CRATE_BREAK;
    run_record_hit(run, HIT_CRATE, 0, x, z);
}

// True when decor i is an unbroken breakable prop within radius of (x, z).
static bool prop_in_reach(const Run *run, int i, fx x, fx z, fx radius)
{
    const Decor *d = &run->room.decor[i];
    return decor_is_breakable(d->kind) && !(run->decor_broken & (1u << i))
        && within(x, z, d->x, d->z, radius + DECOR_POT_RADIUS);
}

// Index of an unbroken breakable prop within radius of (x, z), or -1.
static int prop_at(const Run *run, fx x, fx z, fx radius)
{
    for (int i = 0; i < run->room.decor_count; i++)
        if (prop_in_reach(run, i, x, z, radius))
            return i;
    return -1;
}

// Everything breakable within radius of (x, z) takes damage: pots break,
// barrels take hits, explosive barrels are lit, crates splinter.
static void hit_props(Run *run, fx x, fx z, fx radius, int damage,
                      bool by_explosion)
{
    const Room *r = &run->room;
    for (int i = 0; i < r->decor_count; i++) {
        if (!prop_in_reach(run, i, x, z, radius))
            continue;
        const Decor *d = &r->decor[i];
        if (DECOR_EXPLOSIVE == d->kind) {
            int fuse = by_explosion ? EXPLOSIVE_CHAIN_FUSE : EXPLOSIVE_FUSE;
            if (0 == run->fuse[i] || run->fuse[i] > fuse) {
                if (0 == run->fuse[i]) {
                    run->events |= EV_FUSE_LIT;
                    run_record_hit(run, HIT_FUSE, 0, d->x, d->z);
                }
                run->fuse[i] = (uint8_t)fuse;
            }
        } else if (DECOR_BARREL == d->kind && run->decor_hp[i] > damage) {
            run->decor_hp[i] = (uint8_t)(run->decor_hp[i] - damage);
            run_record_hit(run, HIT_CHIP, 0, d->x, d->z);
        } else {
            break_decor(run, i);
        }
    }
    // Crates whose box the circle touches.
    int c0x = FX_TO_INT(x - radius), c1x = FX_TO_INT(x + radius);
    int c0z = FX_TO_INT(z - radius), c1z = FX_TO_INT(z + radius);
    for (int cz = c0z; cz <= c1z; cz++) {
        for (int cx = c0x; cx <= c1x; cx++) {
            if (!room_in_bounds(cx, cz))
                continue;
            if (CELL_CRATE == room_cell(r, cx, cz)
                && circle_hits_cell(x, z, radius, cx, cz, CELL_CRATE))
                damage_crate(run, cx, cz, damage);
        }
    }
}

// Code Genie's one-hit kills (cheat.h): the player's own hits take an
// enemy's whole health, and break any prop at once.
static int player_damage(const Run *run, const Enemy *e, int damage)
{
    if (cheat_on(run->cheats, CHEAT_ONE_HIT) && e->hp > damage)
        return e->hp;
    return damage;
}

static int prop_damage(const Run *run, int damage)
{
    return cheat_on(run->cheats, CHEAT_ONE_HIT) ? 255 : damage;
}

// One blast meter segment for a landed hit.
static void fill_meter(Run *run)
{
    Player *p = &run->player;
    if (p->meter >= p->meter_max)
        return;
    player_add_meter(p, 1);
    if (p->meter == p->meter_max)
        run->events |= EV_METER_FULL;
}

// Damages an alive enemy and knocks it back along (kx, kz). Hits from the
// sword and daggers fill the blast meter. Returns true if it died.
static bool damage_enemy(Run *run, Enemy *e, int damage, fx kx, fx kz,
                         bool fills_meter)
{
    if (enemy_charges(e->type) && e->stunned)
        damage *= 2;   // a dazed brute is wide open
    if (enemy_is_boss(e->type)) {
        kx /= 4;   // bosses barely budge
        kz /= 4;
    }
    if (ENEMY_DUMMY == e->type) {
        // The training dummy takes the hit, shows it, and stays up.
        e->flash = ENEMY_FLASH_FRAMES;
        run->events |= EV_ENEMY_HIT;
        if (fills_meter)
            fill_meter(run);
        run_record_hit(run, HIT_ENEMY, damage, e->x, e->z);
        return false;
    }
    e->hp -= damage;
    e->flash = ENEMY_FLASH_FRAMES;
    if (!enemy_rooted(e->type)) {   // idols and crystals do not budge
        e->kb_x = kx;
        e->kb_z = kz;
    }
    run->events |= EV_ENEMY_HIT;
    if (fills_meter)
        fill_meter(run);
    if (e->hp <= 0) {
        e->state = EST_DYING;
        e->timer = ENEMY_DYING_FRAMES;
        run->kills++;
        // A fire slime bursts into flame where it falls.
        if (ENEMY_FIRE_SLIME == e->type)
            run_spawn_zone_as(run, e->x, e->z, EMBER_TELEGRAPH, EMBER_RADIUS,
                              EMBER_DAMAGE, ENEMY_FIRE_SLIME);
        run->events |= EV_ENEMY_DIED;
        run_record_hit(run, HIT_KILL, damage, e->x, e->z);
        // Vampiric: some kills leave a heart behind.
        uint32_t roll = rng_hash3(run->seed, (uint32_t)run->kills, run->frames) % 100;
        if (roll < (uint32_t)(VAMPIRIC_PCT * run->boons[BOON_VAMPIRIC]))
            run_spawn_pickup(run, PICK_HEART, 1, e->x, e->z);
        return true;
    }
    run_record_hit(run, HIT_ENEMY, damage, e->x, e->z);
    if (EST_WINDUP == e->state && ENEMY_CHASER == e->type) {
        // A hit during the wind-up interrupts the lunge.
        e->state = EST_RECOVER;
        e->timer = enemy_defs[e->type].recover / 2;
    }
    return false;
}

static bool hittable(const Enemy *e)
{
    // The Slime King cannot be hit in mid-leap, nor a digger underground.
    return enemy_alive(e) && EST_SPAWN != e->state
        && !(ENEMY_SLIME_KING == e->type && EST_ATTACK == e->state)
        && !enemy_buried(e);
}

bool enemy_buried(const Enemy *e)
{
    return ENEMY_DIGGER == e->type
        && (EST_MOVE == e->state || EST_WINDUP == e->state);
}

// Destroys enemy projectiles within radius of (x, z).
static void destroy_shots(Run *run, fx x, fx z, fx radius)
{
    fx r = radius + PROJECTILE_RADIUS;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *s = &run->shots[i];
        if (s->active && within(x, z, s->x, s->z, r)) {
            s->active = false;
            run->events |= EV_SHOT_BLOCKED;
            run_record_hit(run, HIT_SHOT, 0, s->x, s->z);
        }
    }
}

static void player_attack(Run *run)
{
    Player *p = &run->player;
    run->events |= EV_PLAYER_SWING;

    fx hx, hz;
    player_attack_point(p, &hx, &hz);
    fx fvx, fvz;
    dir8_vector(p->facing, &fvx, &fvz);
    hit_props(run, hx, hz, p->attack_radius, prop_damage(run, p->attack_damage),
              false);

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &run->enemies[i];
        if (!hittable(e) || !enemy_within(e, hx, hz, p->attack_radius))
            continue;
        damage_enemy(run, e, player_damage(run, e, p->attack_damage),
                     fx_mul(fvx, ENEMY_KNOCKBACK),
                     fx_mul(fvz, ENEMY_KNOCKBACK), true);
    }

    // The swing also destroys projectiles it touches.
    destroy_shots(run, hx, hz, p->attack_radius);
}

// Daggers and the blast

static void throw_dagger(Run *run)
{
    Player *p = &run->player;
    for (int i = 0; i < MAX_DAGGERS; i++) {
        Dagger *d = &run->daggers[i];
        if (d->active)
            continue;
        fx vx, vz;
        dir8_vector(p->facing, &vx, &vz);
        memset(d, 0, sizeof(*d));
        d->active = true;
        d->x = p->x + fx_mul(vx, FX(0.3));
        d->z = p->z + fx_mul(vz, FX(0.3));
        d->vx = fx_mul(vx, DAGGER_SPEED);
        d->vz = fx_mul(vz, DAGGER_SPEED);
        d->life = DAGGER_LIFE;
        d->pierce = p->dagger_pierce;
        d->dir = p->facing;
        run->events |= EV_DAGGER_THROW;
        return;
    }
}

// Moves a dagger in two half steps so it cannot skip past a thin target.
static void update_dagger(Run *run, Dagger *d)
{
    for (int half = 0; half < 2 && d->active; half++) {
        d->x += d->vx / 2;
        d->z += d->vz / 2;
        if (room_blocked(&run->room, d->x, d->z, FX(0.05))) {
            d->active = false;
            int cx = FX_TO_INT(d->x), cz = FX_TO_INT(d->z);
            if (room_in_bounds(cx, cz)
                && CELL_CRATE == room_cell(&run->room, cx, cz))
                damage_crate(run, cx, cz,
                             prop_damage(run, run->player.dagger_damage));
            run->events |= EV_DAGGER_WALL;
            run_record_hit(run, HIT_DAGGER_WALL, 0, d->x - d->vx / 2,
                       d->z - d->vz / 2);
            return;
        }
        for (int i = 0; i < MAX_ENEMIES && d->active; i++) {
            Enemy *e = &run->enemies[i];
            if (!hittable(e) || (d->hit_mask & (1u << i))
                || !enemy_within(e, d->x, d->z, DAGGER_RADIUS))
                continue;
            d->hit_mask |= (uint16_t)(1u << i);
            damage_enemy(run, e, player_damage(run, e, run->player.dagger_damage),
                         d->vx / 2, d->vz / 2, true);
            if (d->pierce-- <= 0)
                d->active = false;
        }
        if (d->active) {
            destroy_shots(run, d->x, d->z, DAGGER_RADIUS);
            // A prop stops the dagger and takes one hit. (Crates are hit by
            // the wall check above, when the dagger reaches their box.)
            if (prop_at(run, d->x, d->z, DAGGER_RADIUS) >= 0) {
                d->active = false;
                hit_props(run, d->x, d->z, DAGGER_RADIUS,
                          prop_damage(run, run->player.dagger_damage), false);
                // hit_props may also chip a crate the dagger is touching:
                // that is the same single hit.
            }
        }
    }
    if (d->active && --d->life <= 0)
        d->active = false;
}

// Wider than the room's diagonal, for Code Genie's room wipe.
#define WIPE_RADIUS FX_INT(ROOM_SIZE * 2)

static void blast(Run *run)
{
    Player *p = &run->player;
    run->events |= EV_BLAST;
    // Room wipe (cheat.h): every enemy, wherever it is (buried, in mid-leap
    // or still spawning), and every shot.
    bool wipe = cheat_on(run->cheats, CHEAT_WIPE);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &run->enemies[i];
        if (wipe ? !enemy_alive(e)
                 : !hittable(e) || !enemy_within(e, p->x, p->z, p->blast_radius))
            continue;
        fx kx, kz;
        push_away(e, p->x, p->z, BLAST_KNOCKBACK, BLAST_KNOCKBACK, &kx, &kz);
        int damage = wipe && e->hp > p->blast_damage ? e->hp
                   : player_damage(run, e, p->blast_damage);
        damage_enemy(run, e, damage, kx, kz, false);
    }
    destroy_shots(run, p->x, p->z, wipe ? WIPE_RADIUS : p->blast_radius);
    hit_props(run, p->x, p->z, p->blast_radius,
              prop_damage(run, p->blast_damage), false);
}

// Pits

void run_enemy_fell(Run *run, Enemy *e)
{
    e->state = EST_DYING;
    e->timer = 1;
    e->active = false;
    run->kills++;
    run->events |= EV_ENEMY_DIED | EV_ENEMY_FELL;
    run_record_hit(run, HIT_FALL, 0, e->x, e->z);
}

// The player walks over pits freely while dashing; otherwise standing over
// one means a fall: a heart lost, back to the last safe spot.
static void check_pit(Run *run)
{
    Player *p = &run->player;
    if (p->hp <= 0)
        return;
    if (!room_over_pit(&run->room, p->x, p->z)) {
        if (PACT_DASH != p->action) {
            p->safe_x = p->x;
            p->safe_z = p->z;
        }
        return;
    }
    if (PACT_DASH == p->action)
        return;
    run_record_hit(run, HIT_FALL, 0, p->x, p->z);
    run->events |= EV_PLAYER_FELL;
    run->hurt_by = area_defs[run->room.area].pit_cause;
    if (!cheat_on(run->cheats, CHEAT_GOD))   // still back on the edge
        p->hp -= PIT_DAMAGE;
    if (p->hp < 0)
        p->hp = 0;
    p->x = p->safe_x;
    p->z = p->safe_z;
    p->kb_x = p->kb_z = 0;
    p->action = PACT_IDLE;
    p->iframes = p->iframes_hurt;
    p->hurt_frames = PLAYER_IFRAMES_HURT;
}

void run_hit_props(Run *run, fx x, fx z, fx radius, int damage)
{
    hit_props(run, x, z, radius, damage, false);
}

static void update_zones(Run *run)
{
    Player *p = &run->player;
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *zn = &run->zones[i];
        if (!zn->active || --zn->timer > 0)
            continue;
        zn->active = false;
        run->events |= EV_ZONE_BURST;
        run_record_hit(run, HIT_ZONE, zn->radius < ZONE_RADIUS, zn->x, zn->z);
        if (player_within(p, zn->x, zn->z, zn->radius))
            run_hurt_player(run, p, zn->damage, zn->x, zn->z, zn->owner);
        hit_props(run, zn->x, zn->z, zn->radius, zn->damage, false);
    }
}

// Explosions

static void explode(Run *run, int i)
{
    const Decor *b = &run->room.decor[i];
    fx x = b->x, z = b->z;
    run->decor_broken |= 1u << i;
    run->fuse[i] = 0;
    run->events |= EV_EXPLODE;
    run_record_hit(run, HIT_EXPLODE, 0, x, z);

    for (int k = 0; k < MAX_ENEMIES; k++) {
        Enemy *e = &run->enemies[k];
        if (!hittable(e) || !enemy_within(e, x, z, EXPLODE_RADIUS))
            continue;
        fx kx, kz;
        push_away(e, x, z, EXPLODE_KNOCKBACK, 0, &kx, &kz);
        damage_enemy(run, e, EXPLODE_DAMAGE, kx, kz, false);
    }
    // Friendly fire: the player is not spared.
    Player *p = &run->player;
    if (player_within(p, x, z, EXPLODE_RADIUS))
        run_hurt_player(run, p, EXPLODE_PLAYER_DAMAGE, x, z, CAUSE_EXPLOSION);
    destroy_shots(run, x, z, EXPLODE_RADIUS);
    hit_props(run, x, z, EXPLODE_RADIUS, EXPLODE_DAMAGE, true);
}

static void update_fuses(Run *run)
{
    for (int i = 0; i < run->room.decor_count; i++)
        if (run->fuse[i] && 0 == --run->fuse[i])
            explode(run, i);
}

// Traps

TrapState hazard_state(const Hazard *h, uint32_t frame, int *t)
{
    int dummy;
    if (!t)
        t = &dummy;
    int cycle, warn, on;
    switch (h->kind) {
    case HAZ_SPIKES:
        cycle = SPIKE_CYCLE; warn = SPIKE_WARN; on = SPIKE_UP;
        break;
    case HAZ_FIRE:
        cycle = FIRE_CYCLE; warn = FIRE_WARN; on = FIRE_ON;
        break;
    case HAZ_CRUST:
        *t = (int)frame;
        return TRAP_IDLE;   // run.c steps crust (update_crust)
    case HAZ_ROCKFALL: {
        // Only the impact is active; the rest of the cycle is the rubble
        // lying there (idle).
        int f = (int)((frame + (uint32_t)h->phase) % (uint32_t)ROCK_CYCLE);
        if (f >= ROCK_HIT + ROCK_IMPACT || f < ROCK_WARN) {
            *t = f;
            return TRAP_IDLE;
        }
        *t = f >= ROCK_HIT ? f - ROCK_HIT : f - ROCK_WARN;
        return f >= ROCK_HIT ? TRAP_ACTIVE : TRAP_WARN;
    }
    default:
        *t = (int)frame;
        return TRAP_ACTIVE;
    }
    int f = (int)((frame + (uint32_t)h->phase) % (uint32_t)cycle);
    if (f >= on) {
        *t = f - on;
        return TRAP_ACTIVE;
    }
    if (f >= warn) {
        *t = f - warn;
        return TRAP_WARN;
    }
    *t = f;
    return TRAP_IDLE;
}

void hazard_saw_pos(const Hazard *h, uint32_t frame, fx *x, fx *z)
{
    fx len = fx_abs(h->x2 - h->x) + fx_abs(h->z2 - h->z);
    int period = FX_TO_INT(fx_div(2 * len, SAW_SPEED));
    if (period <= 0) {
        *x = h->x;
        *z = h->z;
        return;
    }
    // Back and forth along the track: distance travelled, folded at the
    // far end.
    int f = (int)((frame + (uint32_t)h->phase * 4u) % (uint32_t)period);
    fx travel = f * SAW_SPEED;
    if (travel > len)
        travel = 2 * len - travel;
    fx k = fx_div(travel, len);
    *x = h->x + fx_mul(h->x2 - h->x, k);
    *z = h->z + fx_mul(h->z2 - h->z, k);
}

// Enemies (not flyers) standing over a cell that just turned to lava fall
// in.
static void drop_into(Run *run, int cx, int cz)
{
    for (int k = 0; k < MAX_ENEMIES; k++) {
        Enemy *e = &run->enemies[k];
        if (enemy_alive(e) && !enemy_flies(e->type) && !enemy_is_boss(e->type)
            && FX_TO_INT(e->x) == cx && FX_TO_INT(e->z) == cz)
            run_enemy_fell(run, e);
    }
}

// Crust (the Molten Depths): whole until the player steps on it, then it
// cracks, opens to lava, and sets again.
static void update_crust(Run *run, int i)
{
    const Hazard *h = &run->room.hazards[i];
    int cx = FX_TO_INT(h->x), cz = FX_TO_INT(h->z);
    uint8_t *t = &run->crust[i];
    Player *p = &run->player;
    if (0 == *t) {
        if (p->hp > 0 && PACT_DASH != p->action
            && within(h->x, h->z, p->x, p->z, CRUST_REACH)) {
            *t = 1;
            run->events |= EV_TRAP_FIRED;
            run_record_hit(run, HIT_TRAP, HAZ_CRUST, h->x, h->z);
        }
        return;
    }
    (*t)++;
    if (CRUST_CRACK == *t) {
        run->room.cells[cz][cx] = CELL_PIT;
        drop_into(run, cx, cz);
        run_record_hit(run, HIT_FALL, 0, h->x, h->z);
    } else if (*t >= CRUST_CRACK + CRUST_OPEN) {
        run->room.cells[cz][cx] = CELL_FLOOR;
        *t = 0;
    }
}

// Spikes, fire jets, saws and falling rock hurt whoever stands in them
// while active; crust steps on its own (update_crust).
static void update_traps(Run *run)
{
    Player *p = &run->player;
    for (int i = 0; i < run->room.hazard_count; i++) {
        const Hazard *h = &run->room.hazards[i];
        if (HAZ_CRUST == h->kind) {
            update_crust(run, i);
            continue;
        }
        int t;
        TrapState st = hazard_state(h, run->frames, &t);
        fx x = h->x, z = h->z, reach = SPIKE_REACH;
        if (HAZ_SAW == h->kind) {
            hazard_saw_pos(h, run->frames, &x, &z);
            reach = SAW_REACH;
        } else if (HAZ_FIRE == h->kind) {
            reach = FIRE_REACH;
        } else if (HAZ_ROCKFALL == h->kind) {
            reach = ROCK_REACH;
        }
        if (TRAP_ACTIVE != st) {
            run->trap_hits[i] = 0;
            continue;
        }
        if (0 == t && HAZ_SAW != h->kind) {
            run->events |= EV_TRAP_FIRED;
            run_record_hit(run, HIT_TRAP, h->kind, x, z);
        }
        if (player_within(p, x, z, reach))
            run_hurt_player(run, p, TRAP_DAMAGE, x, z,
                            (uint8_t)(CAUSE_SPIKES + h->kind - HAZ_SPIKES));
        // Enemies: once per active phase (saws: whenever they are clear of
        // the blade again).
        for (int k = 0; k < MAX_ENEMIES; k++) {
            Enemy *e = &run->enemies[k];
            if (!hittable(e))
                continue;
            uint16_t bit = (uint16_t)(1u << k);
            if (!enemy_within(e, x, z, reach)) {
                if (HAZ_SAW == h->kind)
                    run->trap_hits[i] &= (uint16_t)~bit;
                continue;
            }
            if (run->trap_hits[i] & bit)
                continue;
            run->trap_hits[i] |= bit;
            fx kx, kz;
            push_away(e, x, z, ENEMY_KNOCKBACK, 0, &kx, &kz);
            damage_enemy(run, e, TRAP_DAMAGE, kx, kz, false);
        }
    }
}

// With no direction held, attacks and throws turn towards the nearest
// enemy in range.
static void auto_aim(Run *run, const PlayerInput *in)
{
    Player *p = &run->player;
    if (DIR_NONE != in->move || !(in->attack || in->throw_dagger))
        return;
    if (PACT_IDLE != p->action && PACT_WALK != p->action)
        return;
    fx best = fx_mul(AUTO_AIM_RANGE, AUTO_AIM_RANGE);
    const Enemy *target = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &run->enemies[i];
        if (!hittable(e))
            continue;
        fx d2 = dist2(p->x, p->z, e->x, e->z);
        if (d2 < best) {
            best = d2;
            target = e;
        }
    }
    if (target)
        p->facing = dir8_towards(target->x - p->x, target->z - p->z);
}

static void update_daggers(Run *run)
{
    for (int i = 0; i < MAX_DAGGERS; i++)
        if (run->daggers[i].active)
            update_dagger(run, &run->daggers[i]);
}

// Enemy shots fly until they hit a wall, time out, or hit the player.
static void update_projectiles(Run *run)
{
    Player *p = &run->player;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *s = &run->shots[i];
        if (!s->active)
            continue;
        s->x += s->vx;
        s->z += s->vz;
        if (--s->life <= 0
            || room_blocked(&run->room, s->x, s->z, PROJECTILE_RADIUS / 2)) {
            s->active = false;
            continue;
        }
        if (player_within(p, s->x, s->z, PROJECTILE_RADIUS)) {
            // Dodging through a shot with i-frames leaves it flying.
            if (run_hurt_player(run, p, s->damage, s->x - s->vx,
                                s->z - s->vz, s->owner))
                s->active = false;
        }
    }
}

// Keeps enemies from stacking on each other or on the player.
static void separate(Run *run)
{
    const fx min2 = fx_mul(ENEMY_SEPARATION, ENEMY_SEPARATION);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *a = &run->enemies[i];
        if (!enemy_alive(a))
            continue;
        for (int j = i + 1; j < MAX_ENEMIES; j++) {
            Enemy *b = &run->enemies[j];
            if (!enemy_alive(b))
                continue;
            fx dx = b->x - a->x, dz = b->z - a->z;
            if (dist2(b->x, b->z, a->x, a->z) >= min2)
                continue;
            // Nudge apart a little each frame; exact resolution is not
            // needed and this stays cheap.
            fx px = dx >= 0 ? FX(0.02) : FX(-0.02);
            fx pz = dz >= 0 ? FX(0.02) : FX(-0.02);
            // Idols and crystals stand their ground.
            if (!enemy_rooted(a->type))
                room_move(&run->room, &a->x, &a->z, -px, -pz, FX(0.25));
            if (!enemy_rooted(b->type))
                room_move(&run->room, &b->x, &b->z, px, pz, FX(0.25));
        }
    }
}

// Boons in play

void run_take_boon(Run *run, BoonId id)
{
    boon_apply(run, id);
    run->boon_pending = false;
    run->events2 |= EV2_BOON_TAKEN;
}

static void spawn_flame(Run *run, fx x, fx z)
{
    for (int i = 0; i < MAX_FLAMES; i++) {
        Flame *f = &run->flames[i];
        if (f->active)
            continue;
        f->active = true;
        f->x = x;
        f->z = z;
        f->life = FLAME_LIFE;
        f->hit_mask = 0;
        return;
    }
}

static void update_flames(Run *run)
{
    for (int i = 0; i < MAX_FLAMES; i++) {
        Flame *f = &run->flames[i];
        if (!f->active)
            continue;
        if (--f->life <= 0) {
            f->active = false;
            continue;
        }
        for (int k = 0; k < MAX_ENEMIES; k++) {
            Enemy *e = &run->enemies[k];
            uint16_t bit = (uint16_t)(1u << k);
            if (!hittable(e) || (f->hit_mask & bit))
                continue;
            if (enemy_within(e, f->x, f->z, FLAME_RADIUS)) {
                f->hit_mask |= bit;
                damage_enemy(run, e, player_damage(run, e, FLAME_DAMAGE), 0, 0,
                             false);
            }
        }
    }
}

// Shock Dash: the end of a dash knocks everything nearby away. Thorns
// (an upgrade) does the same when the player is hit.
static void shock(Run *run, int damage)
{
    Player *p = &run->player;
    run->events2 |= EV2_SHOCK;
    run_record_hit(run, HIT_SHOCK, 0, p->x, p->z);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &run->enemies[i];
        if (!hittable(e) || !enemy_within(e, p->x, p->z, SHOCK_RADIUS))
            continue;
        fx kx, kz;
        push_away(e, p->x, p->z, SHOCK_KNOCKBACK, 0, &kx, &kz);
        damage_enemy(run, e, player_damage(run, e, damage), kx, kz, false);
    }
}

// Code Genie's endless blast, dash and daggers (cheat.h): topped up as soon
// as they are spent, so the HUD never shows them used.
static void endless_kit(Run *run)
{
    Player *p = &run->player;
    if (cheat_on(run->cheats, CHEAT_BLAST))
        p->meter = p->meter_max;
    if (cheat_on(run->cheats, CHEAT_STAMINA))
        p->dash_charges = p->dash_max;
    if (cheat_on(run->cheats, CHEAT_DAGGERS))
        p->daggers = p->dagger_max;
}

void run_update(Run *run, const PlayerInput *in)
{
    // Events raised outside run_update (room entry) survive one update.
    run->events &= EV_HEAL;
    run->events2 = 0;
    if (run->frames > 0)
        run->events = 0;
    run->frames++;
    Player *p = &run->player;

    if (ROOM_DEAD == run->status)
        return;
    run->time++;

    run->hit_count = 0;
    PlayerAction before = p->action;
    auto_aim(run, in);
    player_update(p, &run->room, in);
    endless_kit(run);
    if (PACT_DASH == p->action && PACT_DASH != before)
        run->events |= EV_PLAYER_DASH;
    if (run->boons[BOON_FLAME_DASH] && PACT_DASH == p->action
        && 0 == run->frames % FLAME_EVERY)
        spawn_flame(run, p->x, p->z);
    if (run->boons[BOON_SHOCK_DASH] && PACT_DASH == before
        && PACT_DASH != p->action)
        shock(run, SHOCK_DAMAGE);
    if (player_attack_hits_now(p))
        player_attack(run);
    if (player_throw_releases_now(p))
        throw_dagger(run);
    if (player_blast_now(p))
        blast(run);
    check_pit(run);
    if (1 == (run->frames % PATH_REFRESH)) {
        int cx = FX_TO_INT(p->x), cz = FX_TO_INT(p->z);
        if (room_is_open(&run->room, cx, cz))
            room_path_map(&run->room, cx, cz, run->path);
    }
    update_daggers(run);
    update_fuses(run);
    update_traps(run);

    for (int i = 0; i < MAX_ENEMIES; i++)
        if (run->enemies[i].active)
            enemy_update(&run->enemies[i], run);
    update_projectiles(run);
    update_zones(run);
    update_flood(run);
    update_flames(run);
    separate(run);
    update_pickups(run);
    if (run->thorns_due) {
        run->thorns_due = false;
        if (p->hp > 0)
            shock(run, THORNS_DAMAGE);
    }

    if (p->hp <= 0 && run->second_wind) {
        // Second Wind: one killing blow per run is shrugged off.
        run->second_wind = false;
        p->hp = imin(SECOND_WIND_HP, p->max_hp);
        p->iframes = PLAYER_IFRAMES_HURT * 3;
        run->events2 |= EV2_SECOND_WIND;
    }
    if (p->hp <= 0) {
        run->status = ROOM_DEAD;
        run->events |= EV_PLAYER_DIED;
    } else if (ROOM_FIGHT == run->status && 0 == run_enemies_alive(run)) {
        run->status = ROOM_CLEAR;
        run->events |= EV_ROOM_CLEAR;
        run->rooms_cleared++;
        // Leftover shots and marks fizzle out.
        for (int i = 0; i < MAX_PROJECTILES; i++)
            run->shots[i].active = false;
        for (int i = 0; i < MAX_ZONES; i++)
            run->zones[i].active = false;
        bool fight = RT_COMBAT == run->room.type || RT_BOSS == run->room.type;
        if (RT_BOSS == run->room.type) {
            // A boss leaves a heart behind as well.
            run_spawn_pickup(run, PICK_HEART, 1, p->x + FX(0.8), p->z);
            run->events2 |= EV2_BOSS_DOWN;
            run->bosses++;
            cool_floods(run);
        }
        if (fight && run->room.enemy_count > 0) {
            run->reward_amount = boon_gold(run, RT_BOSS == run->room.type
                                                ? REWARD_BOSS_GOLD(run->depth)
                                                : REWARD_CLEAR_GOLD(run->depth));
            run->gold += run->reward_amount;
            run->events |= EV_GOLD;
            // Mending: a heart back every few fights.
            if (run->mending && ++run->mend_fights >= MENDING_FIGHTS) {
                run->mend_fights = 0;
                if (p->hp < p->max_hp) {
                    p->hp++;
                    run->events2 |= EV2_MENDED;
                }
            }
            // A boon is offered before the doors open.
            BoonId offer[BOON_CHOICES];
            if (boon_offer(run, offer) > 0) {
                run->boon_pending = true;
                run->events2 |= EV2_BOON_READY;
            }
        }
    }

    if (p->hp > 0)
        check_room_features(run);
}

// The Magma Lord's floods

int run_flood_ring_of(int x, int z)
{
    return imin(imin(x, z), ROOM_SIZE - 1 - imax(x, z));
}

bool run_start_flood(Run *run, int ring)
{
    if (run->flood_ring >= 0 || (run->flooded & (1u << ring)))
        return false;
    run->flood_ring = (int8_t)ring;
    run->flood_timer = FLOOD_WARN;
    run->events2 |= EV2_FLOOD_WARN;
    return true;
}

static void update_flood(Run *run)
{
    // The lava cools as soon as no boss is left standing (his summons
    // may still be about).
    if (run->flooded || run->flood_ring >= 0) {
        bool boss = false;
        for (int k = 0; k < MAX_ENEMIES; k++)
            boss |= enemy_alive(&run->enemies[k])
                 && enemy_is_boss(run->enemies[k].type);
        if (!boss) {
            cool_floods(run);
            return;
        }
    }
    if (run->flood_ring < 0 || --run->flood_timer > 0)
        return;
    int ring = run->flood_ring;
    Room *r = &run->room;
    for (int z = 0; z < ROOM_SIZE; z++) {
        for (int x = 0; x < ROOM_SIZE; x++) {
            if (run_flood_ring_of(x, z) != ring || CELL_FLOOR != r->cells[z][x])
                continue;
            r->cells[z][x] = CELL_PIT;
            run->flood_cells[z][x] = true;
            drop_into(run, x, z);
        }
    }
    // Nobody is left standing in it: the boss steps in, and the player's
    // way back from a fall is the middle of the room.
    const fx mid = FX_INT(ROOM_SIZE) / 2;
    for (int k = 0; k < MAX_ENEMIES; k++) {
        Enemy *e = &run->enemies[k];
        if (enemy_alive(e) && enemy_is_boss(e->type)
            && room_over_pit(r, e->x, e->z)) {
            e->x = mid;
            e->z = mid;
        }
    }
    Player *p = &run->player;
    if (room_over_pit(r, p->safe_x, p->safe_z)) {
        p->safe_x = mid;
        p->safe_z = mid + FX(1.5);
    }
    run->flooded |= (uint8_t)(1u << ring);
    run->flood_ring = -1;
    run->events2 |= EV2_FLOOD;
}

// The boss is down: the lava cools back to floor.
static void cool_floods(Run *run)
{
    if (!run->flooded)
        return;
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            if (run->flood_cells[z][x]) {
                run->room.cells[z][x] = CELL_FLOOR;
                run->flood_cells[z][x] = false;
            }
    run->flooded = 0;
    run->flood_ring = -1;
    run->events2 |= EV2_COOLED;
}

void run_debug_kill_all(Run *run)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &run->enemies[i];
        if (enemy_alive(e)) {
            e->state = EST_DYING;
            e->timer = ENEMY_DYING_FRAMES;
            run->kills++;
        }
    }
}
