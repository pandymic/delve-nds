// Entities: the player, enemies, projectiles, zones and daggers (the last
// four in static pools owned by run.h). Pure logic, no libnds.

#ifndef DELVE_ENTITY_H
#define DELVE_ENTITY_H

#include <stdbool.h>
#include <stdint.h>

#include "fixed.h"
#include "room.h"

// Eight movement directions, clockwise in screen space from screen-right.
typedef enum {
    DIR_E, DIR_SE, DIR_S, DIR_SW, DIR_W, DIR_NW, DIR_N, DIR_NE,
    DIR_COUNT,
    DIR_NONE = -1,
} Dir8;

// World-space unit vector for a screen direction.
void dir8_vector(Dir8 d, fx *x, fx *z);

// Direction from screen-space input (-1/0/1 per axis, y down).
Dir8 dir8_from_input(int sx, int sy);

// Nearest screen direction for a world-space offset (dx, dz). DIR_S for a
// zero offset.
Dir8 dir8_towards(fx dx, fx dz);

typedef enum {
    PACT_IDLE,
    PACT_WALK,
    PACT_DASH,
    PACT_ATTACK,
    PACT_THROW,    // dagger (Y)
    PACT_BLAST,    // special (X)
} PlayerAction;

// Abstract per-frame input, so the player can be driven by tests.
typedef struct {
    Dir8 move;     // DIR_NONE when no direction held
    bool dash;     // pressed this frame
    bool attack;   // pressed this frame
    bool throw_dagger;  // pressed this frame
    bool blast;    // pressed this frame
} PlayerInput;

#define PLAYER_HALF          FX(0.28)   // collision half-size
#define PLAYER_SPEED         FX(0.075)  // tiles per frame
#define PLAYER_ATTACK_SPEED  FX(0.025)
#define PLAYER_DASH_SPEED    FX(0.26)
#define PLAYER_DASH_FRAMES   10
#define PLAYER_DASH_GAP      14         // min frames between dashes
#define PLAYER_DASH_CHARGES  2
#define PLAYER_DASH_RECHARGE 50         // frames per charge
#define PLAYER_IFRAMES_DASH  (PLAYER_DASH_FRAMES + 4)
#define PLAYER_ATTACK_FRAMES 16
#define PLAYER_ATTACK_WINDUP 5          // frames before the swing lands
#define PLAYER_START_HP      6          // half-hearts are not used: 1 = 1 heart
#define PLAYER_IFRAMES_HURT  60
#define PLAYER_KNOCKBACK     FX(0.16)
#define PLAYER_ATTACK_REACH  FX(0.55)   // hit circle centre ahead of the player
#define PLAYER_ATTACK_RADIUS FX(0.65)

// Dagger (Y): charges that refill one at a time.
#define DAGGER_CHARGES       3
#define DAGGER_RECHARGE      90         // frames per charge
#define DAGGER_THROW_FRAMES  12
#define DAGGER_RELEASE       4          // frames into the throw it leaves the hand
#define DAGGER_SPEED         FX(0.22)
#define DAGGER_LIFE          40         // frames (about 9 tiles)
#define DAGGER_RADIUS        FX(0.18)
#define DAGGER_DAMAGE        1
#define DAGGER_PIERCE        1          // extra enemies it passes through

// Blast (X): fills one segment per hit landed.
#define BLAST_METER_MAX      10
#define BLAST_FRAMES         18
#define BLAST_WINDUP         6          // frames before it goes off
#define BLAST_RADIUS         FX(2.2)
#define BLAST_DAMAGE         2
#define BLAST_KNOCKBACK      FX(0.36)
#define BLAST_IFRAMES        (BLAST_FRAMES + 6)

// Pits: falling in costs a heart and puts the player back on the edge.
#define PIT_DAMAGE           1

// Auto-aim: an attack or throw with no direction held turns towards the
// nearest enemy within this range.
#define AUTO_AIM_RANGE       FX(5.0)

typedef struct {
    fx x, z;
    Dir8 facing;
    PlayerAction action;
    int action_timer;      // frames left in dash / attack
    int dash_charges;
    int dash_recharge;     // frames until the next charge
    int dash_recharge_time;
    int dash_gap;
    int iframes;
    int hurt_frames;       // counts down from PLAYER_IFRAMES_HURT after a hit
    int hp, max_hp;
    int attack_damage;
    fx kb_x, kb_z;         // knockback velocity, decays each frame
    int anim_timer;        // free-running, drives the walk cycle
    fx safe_x, safe_z;     // last spot on solid floor (pit respawn)
    int daggers;           // charges ready
    int dagger_recharge;   // frames until the next charge
    int meter;             // blast meter, 0..meter_max

    // Stats that boons (boon.c) and upgrades change; set from the
    // constants above by player_init.
    int dash_max;
    int dagger_max;
    int dagger_recharge_time;
    int dagger_pierce;
    int dagger_damage;
    int iframes_hurt;      // invulnerable frames after a hit
    int meter_max;
    fx attack_radius;
    fx blast_radius;
    int blast_damage;
} Player;

// A fresh player at (x, z) with the base stats (upgrades apply after).
void player_init(Player *p, fx x, fx z);
// One frame: recharges, knockback, then the action the input starts or
// continues, and movement.
void player_update(Player *p, const Room *room, const PlayerInput *in);

// True on the frame the swing becomes active.
bool player_attack_hits_now(const Player *p);

// True on the frame a thrown dagger leaves the hand.
bool player_throw_releases_now(const Player *p);

// True on the frame the blast goes off.
bool player_blast_now(const Player *p);

// Adds to the blast meter (hits landed), capped at BLAST_METER_MAX.
void player_add_meter(Player *p, int amount);

// Applies damage unless invulnerable. Returns true if it landed.
bool player_hurt(Player *p, int damage, fx from_x, fx from_z);

// Centre of the sword's hit circle.
void player_attack_point(const Player *p, fx *x, fx *z);

// Enemies

typedef enum {
    ENEMY_CHASER,    // orc: walks up, telegraphs, lunges
    ENEMY_SHOOTER,   // mage: keeps distance, fires fireballs
    ENEMY_BRUTE,     // big orc: winds up, charges in a straight line
    ENEMY_ARCHER,    // keeps far away, looses fast arrows
    ENEMY_CASTER,    // warlock: marks the floor under you, then it bursts
    ENEMY_SHAMAN,    // orc shaman: summons slimes
    ENEMY_TURRET,    // a stone idol: never moves, fires patterns
    ENEMY_SLIME,     // summoned: small, quick, touch damage
    ENEMY_DUMMY,     // hub training dummy: never fights, never dies
    ENEMY_WARLORD,   // boss of the Dungeon: charges, fireball rings and fans
    ENEMY_SLIME_KING,// boss of the Sewers: leaps, bursts, splits off slimes
    // The Caverns.
    ENEMY_BAT,       // flies (over pits too), weaves in, dives
    ENEMY_DIGGER,    // tunnels under you, bursts out, then burrows again
    ENEMY_CRYSTAL,   // a floating crystal: turning volleys of shards
    ENEMY_GOLEM,     // boss of the Caverns: hops and slams, crystal rain
    // The Molten Depths.
    ENEMY_FIRE_SLIME,  // hops at you; bursts into flame where it dies
    ENEMY_MAGMA_BRUTE, // a brute whose charge leaves a burning trail
    ENEMY_IMP,         // keeps away, throws three fireballs at once
    ENEMY_MAGMA_LORD,  // boss of the Depths: fire waves, floods the edges
    ENEMY_TYPE_COUNT
} EnemyType;

typedef enum {
    EST_SPAWN,    // appearing, harmless
    EST_MOVE,
    EST_WINDUP,   // telegraph before an attack
    EST_ATTACK,
    EST_RECOVER,
    EST_DYING,
} EnemyState;

// Per-type stats. Tuned in enemy.c; difficulty scaling applies on spawn.
typedef struct {
    int hp;
    int damage;
    fx speed;
    fx radius;
    int windup, attack, recover;  // frames
    fx range;         // chaser: lunge trigger distance; shooter: max shot range
    fx attack_speed;  // chaser: lunge speed; shooter: projectile speed
    int cooldown;     // frames between attacks
} EnemyDef;

extern const EnemyDef enemy_defs[ENEMY_TYPE_COUNT];

#define ENEMY_SPAWN_FRAMES   36
#define SUMMON_SPAWN_FRAMES  14   // slimes pop in quickly
#define ENEMY_DYING_FRAMES   24
#define ENEMY_FLASH_FRAMES   6
#define SHOOTER_NEAR         FX(3.0)  // backs off when closer
#define SHOOTER_FAR          FX(5.0)  // approaches when further
#define ARCHER_NEAR          FX(4.5)
#define ARCHER_FAR           FX(7.0)
#define CASTER_NEAR          FX(4.0)
#define CASTER_FAR           FX(7.0)
#define BRUTE_CHARGE_RANGE   FX(5.5)
#define BRUTE_STUN           60       // frames dazed after hitting a wall
#define SHAMAN_MAX_SLIMES    3        // alive at once, per shaman
#define ZONE_TELEGRAPH       50       // frames a marked spot glows first
#define ZONE_RADIUS          FX(1.0)
#define ZONE_DAMAGE          2
// Small fires (a fire slime's death, a magma brute's trail).
#define EMBER_TELEGRAPH      34
#define EMBER_RADIUS         FX(0.6)
#define EMBER_DAMAGE         1
#define ELITE_HP_MULT        2
#define ELITE_SPEED_PCT      115

typedef struct {
    bool active;
    EnemyType type;
    EnemyState state;
    int timer;
    fx x, z;
    fx aim_x, aim_z;     // unit attack direction, locked at wind-up
    fx kb_x, kb_z;
    int hp, max_hp;
    int damage;
    fx speed;
    int cooldown;
    int flash;           // hit flash frames left
    int strafe;          // shooter strafe direction, +1 / -1
    bool hit_done;       // chaser lunge already hit this attack
    bool elite;
    int anim;
    int8_t owner;        // slimes: index of the shaman that summoned them
    bool stunned;        // brute: dazed after a charge hit a wall
    uint8_t pattern;     // turret: which volley comes next
    bool sees;           // clear line to the player this frame (enemy.c)
    uint8_t attack_kind; // bosses: which attack is under way
    fx tx, tz;           // Slime King: where the leap lands
} Enemy;

static inline bool enemy_is_boss(EnemyType t)
{
    return ENEMY_WARLORD == t || ENEMY_SLIME_KING == t || ENEMY_GOLEM == t
        || ENEMY_MAGMA_LORD == t;
}

// Brutes that wind up and charge in a line (and are dazed by a wall).
static inline bool enemy_charges(EnemyType t)
{
    return ENEMY_BRUTE == t || ENEMY_WARLORD == t || ENEMY_MAGMA_BRUTE == t;
}

// Flying enemies cross pits and never fall in.
static inline bool enemy_flies(EnemyType t)
{
    return ENEMY_BAT == t;
}

// Stationary shooters: no knockback, never pushed aside.
static inline bool enemy_rooted(EnemyType t)
{
    return ENEMY_TURRET == t || ENEMY_CRYSTAL == t;
}

typedef enum { PROJ_FIREBALL, PROJ_ARROW, PROJ_SHARD } ProjectileKind;

typedef struct {
    bool active;
    uint8_t kind;        // ProjectileKind
    uint8_t owner;       // EnemyType that fired it (death causes)
    fx x, z, vx, vz;
    int life;
    int damage;
} Projectile;

// A spot on the floor marked by a caster, a boss or fire: it glows, then
// bursts.
typedef struct {
    bool active;
    fx x, z;
    int timer;           // frames until the burst
    fx radius;
    uint8_t damage;
    uint8_t owner;       // cause of the damage (EnemyType)
} Zone;

// The player's thrown daggers.
typedef struct {
    bool active;
    fx x, z, vx, vz;
    int life;
    int pierce;          // enemies it can still pass through
    uint16_t hit_mask;   // enemies already hit (bit per pool slot)
    Dir8 dir;
} Dagger;

#define PROJECTILE_RADIUS FX(0.12)
#define PROJECTILE_LIFE   240
#define PROJECTILE_HEIGHT FX(0.45)

#endif // DELVE_ENTITY_H
