// A run in progress: room, player, enemy and projectile pools, and the
// per-frame simulation. Pure logic (no libnds), so whole fights can be
// simulated on the host.

#ifndef DELVE_RUN_H
#define DELVE_RUN_H

#include <stdint.h>

#include "config.h"
#include "entity.h"
#include "boon.h"
#include "room.h"

typedef enum {
    ROOM_FIGHT,
    ROOM_CLEAR,
    ROOM_DEAD,
} RoomStatus;

// Things that happened this frame, for effects and sound.
enum {
    EV_PLAYER_SWING  = 1 << 0,
    EV_PLAYER_DASH   = 1 << 1,
    EV_PLAYER_HURT   = 1 << 2,
    EV_ENEMY_HIT     = 1 << 3,
    EV_ENEMY_DIED    = 1 << 4,
    EV_ENEMY_SHOOT   = 1 << 5,
    EV_ROOM_CLEAR    = 1 << 6,
    EV_PLAYER_DIED   = 1 << 7,
    EV_SHOT_BLOCKED  = 1 << 8,
    EV_DOOR_ENTER    = 1 << 9,   // run->exit_door says which
    EV_GOLD          = 1 << 10,  // run->reward_amount gold gained
    EV_HEAL          = 1 << 11,  // run->reward_amount hearts healed
    EV_CHEST_OPEN    = 1 << 12,
    EV_POT_BREAK     = 1 << 13,
    EV_DAGGER_THROW  = 1 << 14,
    EV_DAGGER_WALL   = 1 << 15,  // a dagger hit a wall (see run->hits)
    EV_BLAST         = 1 << 16,
    EV_METER_FULL    = 1 << 17,  // the blast meter just filled
    EV_BARREL_BREAK  = 1 << 18,
    EV_CRATE_BREAK   = 1 << 19,
    EV_FUSE_LIT      = 1 << 20,
    EV_EXPLODE       = 1 << 21,
    EV_COIN          = 1 << 22,  // run->reward_amount gold picked up
    EV_HEART         = 1 << 23,  // a heart picked up
    EV_TRAP_FIRED    = 1 << 24,  // spikes or flames just came out
    EV_PLAYER_FELL   = 1 << 25,  // into a pit
    EV_ENEMY_FELL    = 1 << 26,
    EV_ZONE_MARKED   = 1 << 27,  // a caster marked the floor
    EV_ZONE_BURST    = 1 << 28,
    EV_SUMMON        = 1 << 29,  // a shaman called slimes
    EV_CHARGE        = 1 << 30,  // a brute started a charge
    EV_SLAM          = 1u << 31, // ...and hit a wall
};

// Events past 32 bits, in run->events2.
enum {
    EV2_BOON_READY   = 1 << 0,   // a boon is on offer (room cleared)
    EV2_BOON_TAKEN   = 1 << 1,
    EV2_SHOCK        = 1 << 2,   // a Shock Dash went off
    EV2_BOSS_DOWN    = 1 << 3,   // the area's boss was beaten
    EV2_SECOND_WIND  = 1 << 4,   // a killing blow was survived (upgrade)
    EV2_FLOOD_WARN   = 1 << 5,   // the Magma Lord: a ring of floor will flood
    EV2_FLOOD        = 1 << 6,   // ...and now it is lava
    EV2_COOLED       = 1 << 7,   // the lava cooled (the Lord fell)
    EV2_MENDED       = 1 << 8,   // Mending healed a heart (upgrade)
};

// Where something was hit this frame, for damage numbers and sparks.
typedef enum {
    HIT_ENEMY,       // amount = damage
    HIT_KILL,        // amount = damage
    HIT_POT,
    HIT_DAGGER_WALL,
    HIT_SHOT,        // a projectile destroyed
    HIT_BARREL,      // a barrel broke
    HIT_CHIP,        // a barrel or crate took a hit but held
    HIT_CRATE,       // a crate broke
    HIT_FUSE,        // an explosive barrel was lit
    HIT_EXPLODE,
    HIT_COIN,        // a coin was picked up
    HIT_HEART,
    HIT_TRAP,        // a trap fired (spikes up, flames on)
    HIT_FALL,        // something fell into a pit here
    HIT_ZONE,        // a caster's mark burst
    HIT_SLAM,        // a brute hit a wall
    HIT_SUMMON,      // a slime appeared
    HIT_SHOCK,       // a Shock Dash went off
} HitKind;

// What dealt the player's last damage; at death, the cause shown on the
// summary and kept in the run history (saved). Below CAUSE_PIT it is the
// EnemyType. Never renumber.
enum {
    CAUSE_PIT       = 32,
    CAUSE_EXPLOSION = 33,
    CAUSE_SPIKES    = 34,   // CAUSE_SPIKES + HazardKind for traps
    CAUSE_FIRE      = 35,
    CAUSE_SAW       = 36,
    CAUSE_ROCKFALL  = 37,
    CAUSE_LAVA      = 39,   // a pit in the Molten Depths
    CAUSE_WATER     = 40,   // a pit in the Sewers
    CAUSE_ABANDONED = 48,
    CAUSE_UNKNOWN   = 63,
};

typedef struct {
    uint8_t kind;    // HitKind
    uint8_t amount;
    fx x, z;
} RunHit;

#define MAX_HITS 12

// Rewards, kept here so they are easy to tune.
#define REWARD_CLEAR_GOLD(depth)  (4 + (depth) * 2)
#define REWARD_CHEST_GOLD(depth)  (18 + (depth) * 5)
#define REWARD_BOSS_GOLD(depth)   (40 + (depth) * 4)
#define REWARD_REST_HEAL          2
#define REWARD_POT_GOLD           2
#define REWARD_POT_CHANCE         40   // percent of pots holding gold
#define REWARD_BARREL_HEART       15   // percent of barrels holding a heart
#define REWARD_BARREL_COIN        30   // ...or else a coin worth 1

// Destructibles.
#define BARREL_HP             2
#define CRATE_HP              3
#define EXPLOSIVE_FUSE        45    // frames from being hit to exploding
#define EXPLOSIVE_CHAIN_FUSE  12    // when lit by another explosion
#define EXPLODE_RADIUS        FX(1.8)
#define EXPLODE_DAMAGE        3     // to enemies and props
#define EXPLODE_PLAYER_DAMAGE 1
#define EXPLODE_KNOCKBACK     FX(0.3)

// Coins and hearts: they pop out, settle, then can be picked up. Once the
// room is clear they all fly to the player.
#define PICKUP_SETTLE         16    // frames before it can be picked up
#define PICKUP_REACH          FX(0.45)
#define PICKUP_MAGNET         FX(1.6)
#define PICKUP_MAGNET_SPEED   FX(0.09)

typedef enum { PICK_COIN, PICK_HEART } PickupKind;

typedef struct {
    bool active;
    uint8_t kind;    // PickupKind
    uint8_t value;   // coins: gold
    uint8_t age;
    fx x, y, z, vx, vy, vz;
} Pickup;

// Traps: a cycle of idle, warning and active frames (spikes and fire), or
// a blade sliding along its track (saws).
#define SPIKE_CYCLE      150
#define SPIKE_WARN       90    // spikes start rising
#define SPIKE_UP         110   // ...and are out, until the end of the cycle
#define FIRE_CYCLE       180
#define FIRE_WARN        120   // smoke and sparks
#define FIRE_ON          140   // flames, until the end of the cycle
#define SAW_SPEED        FX(0.04)
#define TRAP_DAMAGE      1
#define SPIKE_REACH      FX(0.5)
#define FIRE_REACH       FX(0.45)
#define SAW_REACH        FX(0.35)
// Falling rock (the Caverns): a shadow grows, then the rock lands.
#define ROCK_CYCLE       210
#define ROCK_WARN        150   // the shadow appears
#define ROCK_HIT         190   // ...and the rock lands
#define ROCK_IMPACT      6     // frames it hurts
#define ROCK_REACH       FX(0.55)
// Crust (the Molten Depths).
#define CRUST_REACH      FX(0.5)
#define CRUST_CRACK      45    // frames from the first step to lava
#define CRUST_OPEN       150   // frames it stays open
// The Magma Lord's floods: a ring of floor glows, then is lava.
#define FLOOD_WARN       100

typedef enum { TRAP_IDLE, TRAP_WARN, TRAP_ACTIVE } TrapState;

// State of a spike trap or fire jet at a frame of the room; *t receives
// frames since that state began. Saws are always active.
TrapState hazard_state(const Hazard *h, uint32_t frame, int *t);

// Where a saw blade is at a frame of the room.
void hazard_saw_pos(const Hazard *h, uint32_t frame, fx *x, fx *z);

// A patch of fire left by a Flame Dash (boon.c).
typedef struct {
    bool active;
    fx x, z;
    int life;
    uint16_t hit_mask;   // enemies already burnt by it
} Flame;

typedef struct Run {
    uint32_t seed;
    int depth;
    int start_depth;     // 1, or a waypoint's first depth
    int kills;
    int gold;            // banked into meta-currency when the run ends
    int rooms_cleared;
    bool chest_opened;
    uint32_t decor_broken; // bit i: room.decor[i] is broken
    uint8_t decor_hp[MAX_DECOR];
    uint8_t fuse[MAX_DECOR];   // explosive barrels: frames left once lit
    uint8_t crate_hp[ROOM_SIZE][ROOM_SIZE];
    bool crate_broken[ROOM_SIZE][ROOM_SIZE];   // debris left on the floor
    Pickup pickups[MAX_PICKUPS];
    uint16_t trap_hits[MAX_HAZARDS];   // enemies hit this active phase
    uint8_t crust[MAX_HAZARDS];        // crust: frames since it cracked (0 =
                                       // whole)
    int8_t flood_ring;                 // ring warned of, or -1
    int flood_timer;
    uint8_t flooded;                   // bit per flooded ring
    bool flood_cells[ROOM_SIZE][ROOM_SIZE];   // floor turned to lava
    uint8_t path[ROOM_SIZE][ROOM_SIZE];  // steps to the player (enemy.c)
    uint8_t boons[BOON_COUNT];   // stacks taken (boon.c)
    bool boon_pending;   // a cleared combat room offers a boon first
    bool second_wind;    // UPG_SECOND_WIND not yet used this run
    bool deep_pockets;   // UPG_DEEP_POCKETS
    bool thorns;         // UPG_THORNS
    bool thorns_due;     // ...and the player was just hit
    bool mending;        // UPG_MENDING
    int mend_fights;     // fights cleared towards the next Mending heart
    Flame flames[MAX_FLAMES];
    uint8_t hurt_by;     // CAUSE_* / EnemyType of the last damage taken
    int bosses;          // bosses beaten this run
    uint32_t time;       // frames played this run (not paused or dead)
    uint32_t cheats;     // Code Genie bits (cheat.h); set by the caller
                         // after run_start*, which clear it
    int exit_door;       // valid with EV_DOOR_ENTER
    int reward_amount;   // valid with EV_GOLD / EV_HEAL
    Room room;
    Player player;
    Enemy enemies[MAX_ENEMIES];
    Projectile shots[MAX_PROJECTILES];
    Dagger daggers[MAX_DAGGERS];
    Zone zones[MAX_ZONES];
    RunHit hits[MAX_HITS];   // this frame's hits
    int hit_count;
    RoomStatus status;
    uint32_t frames;     // frames in the current room
    uint32_t events;     // EV_* flags raised during the last update
    uint32_t events2;    // EV2_* flags
} Run;

// Starts at depth 1 in a generated combat room.
void run_start(Run *run, uint32_t seed);

// Starts at a waypoint: the first depth of `area`, with
// WAYPOINT_BOONS boons for each area skipped (a run that walked down would
// have about twice that). Area 0 is run_start.
#define WAYPOINT_BOONS 3
void run_start_at(Run *run, uint32_t seed, int area);

// The camp between runs (roomgen_hub): depth 0, no enemies but the dummy;
// its door (a portal) is open from the start.
void run_start_hub(Run *run);

// True when the player stands at the hub's upgrade shrine.
#define HUB_STATION_REACH FX(1.1)
bool run_at_shrine(const Run *run);

// Applies purchased meta upgrades (MetaProgress.upgrades bits) to the
// player. Call right after run_start().
void run_apply_upgrades(Run *run, uint32_t upgrades);

// Leaves through exit door i: generates and enters the next room.
void run_enter_door(Run *run, int door);

// Places the player and spawns enemies for the room in run->room.
void run_begin_room(Run *run);

// Clears enemies and projectiles for a new room (the room itself is set by
// the caller or the generator).
void run_reset_room(Run *run);

// Fixed room with three orcs and two mages, for host tests.
void run_load_test_room(Run *run);

// Takes a free slot of a pool and fills it in; NULL when the pool is full.
// Enemies start spawning with their type's base stats.
Enemy *run_spawn_enemy(Run *run, EnemyType type, fx x, fx z);
Projectile *run_spawn_projectile(Run *run, ProjectileKind kind, fx x, fx z,
                                 fx vx, fx vz, int damage);
Zone *run_spawn_zone(Run *run, fx x, fx z);   // a caster's mark
// A mark that bursts after timer frames, hurting within radius; owner is
// the cause of death it records (an EnemyType).
Zone *run_spawn_zone_as(Run *run, fx x, fx z, int timer, fx radius,
                        int damage, uint8_t owner);

// Floods: ring 0 is the room's outer ring of cells, ring 1 the next.
// run_start_flood warns (EV2_FLOOD_WARN); FLOOD_WARN frames later the
// ring's floor is lava (EV2_FLOOD). The boss's fall cools it (EV2_COOLED).
int run_flood_ring_of(int x, int z);
bool run_start_flood(Run *run, int ring);

// Hurts the player (player_hurt) and, if it landed, raises EV_PLAYER_HURT
// and records the cause (CAUSE_* or an EnemyType).
bool run_hurt_player(Run *run, Player *p, int damage, fx from_x, fx from_z,
                     uint8_t cause);

// Damages breakable props and crates within radius of (x, z) (a brute's
// slam, a caster's burst).
void run_hit_props(Run *run, fx x, fx z, fx radius, int damage);

// Enemies still to beat in the room (the camp's dummy never counts).
int run_enemies_alive(const Run *run);

// One frame of the room: the player, their attacks, pits, daggers, props,
// traps, enemies, shots, marks, floods and pickups, then the room's status
// (cleared, dead) and its rewards. What happened is in run->events.
void run_update(Run *run, const PlayerInput *in);

// Debug helper: kills every enemy in the room (bound to X+Y in DEBUG builds).
void run_debug_kill_all(Run *run);

// Takes a boon on offer (applies it, and lets the doors open).
void run_take_boon(Run *run, BoonId id);

// True if pot i of the current room holds gold. Fixed per room seed.
bool run_pot_has_gold(const Run *run, int decor_index);

// Drops a coin or heart at (x, z), popping up from the ground.
Pickup *run_spawn_pickup(Run *run, PickupKind kind, int value, fx x, fx z);

// Enemy AI step (enemy.c).
void enemy_update(Enemy *e, Run *run);

// A digger underground (or breaking out): unseen and out of reach.
bool enemy_buried(const Enemy *e);

// How often enemies re-read the way to the player.
#define PATH_REFRESH 8

// Records a hit (for effects) and an enemy falling; used by enemy.c.
void run_enemy_fell(Run *run, Enemy *e);
void run_record_hit(Run *run, HitKind kind, int amount, fx x, fx z);

#endif // DELVE_RUN_H
