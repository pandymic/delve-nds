// Room data: a square grid of cells surrounded by walls.
// Pure logic (no libnds). Cells are indexed [z][x]; world units are tiles,
// cell (x, z) spans [x, x+1] x [z, z+1].

#ifndef DELVE_ROOM_H
#define DELVE_ROOM_H

#include <stdbool.h>
#include <stdint.h>

#include "area.h"
#include "config.h"
#include "fixed.h"

typedef enum {
    CELL_FLOOR = 0,
    CELL_PILLAR,
    CELL_CRATE,
    CELL_CHEST,   // treasure room feature, drawn as a billboard
    CELL_FIRE,    // rest room campfire, drawn as a billboard
    CELL_VOID,    // outside the room's outline: solid, not drawn
    CELL_PIT,     // a bottomless hole: the player falls in; enemies and
                  // pickups treat it as solid; projectiles fly over it
    CELL_PROP,    // solid under a standing prop (a tree, the tent): drawn
                  // by its decor, not as a box
} CellType;

// Walls by the axis they sit on. On screen: NX = upper-left, NZ = upper-right
// (full-height back walls), PX = lower-right, PZ = lower-left (low ledges).
typedef enum {
    SIDE_NONE = -1,
    SIDE_NX = 0,   // x = 0
    SIDE_NZ,       // z = 0
    SIDE_PX,       // x = ROOM_SIZE
    SIDE_PZ,       // z = ROOM_SIZE
    SIDE_COUNT
} WallSide;

typedef enum {
    RT_COMBAT,
    RT_REST,
    RT_TREASURE,
    RT_BOSS,      // every 10th depth; the door before it leads only there
    RT_COUNT
} RoomType;

// Areas (which depths, what they hold): area.h.

typedef struct {
    WallSide side;
    int8_t pos;          // tile index along the wall
    RoomType leads_to;   // reward preview: the next room's type
} Door;

typedef struct {
    uint8_t type;        // EnemyType
    uint8_t elite;
    fx x, z;
} EnemySpawn;

// Room dressing. Pots and barrels break (run.c); explosive barrels blow up.
typedef enum {
    DECOR_TORCH,     // mounted on a back wall; also lights the room
    DECOR_CHAIN,     // hanging on a back wall
    DECOR_BARREL,    // clutter against a wall (variant 0/1), 2 hits
    DECOR_POT,       // clutter, 1 hit
    DECOR_GRATE,     // floor decal
    DECOR_PLATE,     // floor decal: pressure plate
    DECOR_EXPLOSIVE, // red barrel: lit by any hit, explodes after a fuse
    DECOR_SKULL,     // clutter, decoration only
    DECOR_BONES,     // floor decal
    DECOR_CRACK,     // floor decal
    DECOR_RUBBLE,    // floor decal
    DECOR_COBWEB,    // high on a back wall, near a corner
    DECOR_BANNER,    // on a back wall (variant 0 red, 1 blue)
    // Outdoor props (the world sheet). Decoration only; the ones that block
    // stand on CELL_PROP cells.
    DECOR_TREE,      // variant: WorldTree
    DECOR_BUSH,
    DECOR_ROCK,
    DECOR_STONES,
    DECOR_STUMP,
    DECOR_TENT,
    DECOR_WELL,
    DECOR_SIGN,
    DECOR_BENCH,     // variant 1: the table
    DECOR_CRYSTAL,   // on a back wall in the Caverns; lights the room
    DECOR_KIND_COUNT
} DecorKind;

// Trees of the world sheet (DECOR_TREE variants).
enum {
    TREE_PINE, TREE_PINE2, TREE_PINE_SMALL, TREE_FRUIT, TREE_ROUND,
    TREE_GREEN, TREE_PUNY, TREE_KIND_COUNT
};

static inline bool decor_is_wall_mounted(int kind)
{
    return DECOR_TORCH == kind || DECOR_CHAIN == kind || DECOR_COBWEB == kind
        || DECOR_BANNER == kind || DECOR_CRYSTAL == kind;
}

static inline bool decor_is_decal(int kind)
{
    return DECOR_GRATE == kind || DECOR_PLATE == kind || DECOR_BONES == kind
        || DECOR_CRACK == kind || DECOR_RUBBLE == kind;
}

static inline bool decor_is_breakable(int kind)
{
    return DECOR_POT == kind || DECOR_BARREL == kind || DECOR_EXPLOSIVE == kind;
}

// Traps. Timings live in run.c (hazard_phase).
typedef enum {
    HAZ_SPIKES,      // a floor tile that stabs up on a cycle
    HAZ_FIRE,        // a floor vent that bursts into flame on a cycle
    HAZ_SAW,         // a blade running back and forth along a track
    HAZ_ROCKFALL,    // a rock drops from the ceiling on a cycle
    HAZ_CRUST,       // cooled crust over lava: cracks under you, opens,
                     // then sets again
    HAZ_KIND_COUNT
} HazardKind;

typedef struct {
    uint8_t kind;    // HazardKind
    uint8_t phase;   // offset into its cycle, so traps are not in step
    fx x, z;         // cell centre (saw: one end of the track)
    fx x2, z2;       // saw: the other end
} Hazard;

typedef struct {
    uint8_t kind;        // DecorKind
    uint8_t variant;
    WallSide wall;       // wall-mounted kinds: which wall (x/z on its line)
    fx x, z;
} Decor;

#define DECOR_POT_RADIUS  FX(0.3)

typedef struct {
    uint8_t cells[ROOM_SIZE][ROOM_SIZE];
    uint32_t seed;
    RoomType type;
    WallSide entry_side;
    int8_t entry_pos;
    Door doors[MAX_DOORS];
    uint8_t door_count;
    fx spawn_x, spawn_z;
    EnemySpawn enemies[MAX_ENEMIES];
    uint8_t enemy_count;
    int8_t feature_x, feature_z;   // chest/campfire cell, -1 if none
    bool hub;                      // the camp between runs (roomgen_hub)
    uint16_t dirt[ROOM_SIZE];      // camp: bit x of row z is a dirt path
    uint8_t area;                  // AREA_*
    Decor decor[MAX_DECOR];
    uint8_t decor_count;
    Hazard hazards[MAX_HAZARDS];
    uint8_t hazard_count;
} Room;

static inline WallSide side_opposite(WallSide s)
{
    return (WallSide)((s + 2) % SIDE_COUNT);
}

// Door geometry: centre on the wall line, and unit inward normal.
void door_center(WallSide side, int pos, fx *x, fx *z);
void side_inward(WallSide side, fx *nx, fx *nz);

// Floor cell just inside a wall position.
void door_cell(WallSide side, int pos, int *cx, int *cz);

// Index of the exit door at (side, pos), or -1.
int room_door_at(const Room *r, WallSide side, int pos);

// An empty room: all floor, no doors, entry or feature.
void room_clear(Room *r);

// Fixed layout for tests: four pillars, three crates, no doors.
void room_make_test(Room *r);

// True if every floor cell is reachable from (sx, sz) (4-connected).
bool room_connected(const Room *r, int sx, int sz);

static inline CellType room_cell(const Room *r, int x, int z)
{
    return (CellType)r->cells[z][x];
}

// Walls (outside the grid), obstacles, void and pits are solid: anything
// that is not walkable floor. Used for connectivity and spawning.
bool room_solid(const Room *r, int x, int z);

static inline bool room_in_bounds(int x, int z)
{
    return x >= 0 && z >= 0 && x < ROOM_SIZE && z < ROOM_SIZE;
}

static inline bool room_is_open(const Room *r, int x, int z)
{
    return room_in_bounds(x, z) && CELL_FLOOR == r->cells[z][x];
}

// True if the point (x, z) is over a pit cell.
bool room_over_pit(const Room *r, fx x, fx z);

// Visual and collision inset of an obstacle from its cell edges, and height.
fx room_cell_inset(CellType t);
fx room_cell_height(CellType t);

// Moves an axis-aligned square of half-size r by (dx, dz), sliding along
// walls and obstacle boxes. Resolves x then z. Pits are solid (enemies,
// pickups).
void room_move(const Room *r, fx *x, fx *z, fx dx, fx dz, fx half);

// Like room_move, but pits are open: the player can step (and fall) in, and
// knocked-back enemies can be pushed in.
void room_walk(const Room *r, fx *x, fx *z, fx dx, fx dz, fx half);

// True if the square at (x, z) overlaps an obstacle, void, or leaves the
// room. Pits do not block (projectiles fly over them).
bool room_blocked(const Room *r, fx x, fx z, fx half);

// True if nothing solid (walls, void, obstacles) lies on the straight line
// between two points. Pits do not block sight.
bool room_line_clear(const Room *r, fx x0, fx z0, fx x1, fx z1);

// Like room_line_clear, but pits block too: a straight walk is possible.
bool room_line_walkable(const Room *r, fx x0, fx z0, fx x1, fx z1);

// Path distances in cells from (cx, cz) over walkable floor, 8-way without
// cutting corners; 255 where unreachable.
void room_path_map(const Room *r, int cx, int cz,
                   uint8_t dist[ROOM_SIZE][ROOM_SIZE]);

// True if the obstacle in cell (cx, cz) is between the camera and an
// entity standing at (x, z) with a sprite of the given screen half-width and
// height (world units, as drawn). Uses the camera's fixed iso projection.
bool room_occludes(const Room *r, int cx, int cz, fx x, fx z,
                   fx sprite_half_w, fx sprite_h);

// Per-cell obstacle alpha (31 = opaque) for the occlusion fade.
#define FADE_ALPHA_OPAQUE 31
#define FADE_ALPHA_MIN    14   // never 0: alpha 0 is wireframe on the DS
#define FADE_ALPHA_STEP   3    // per frame
typedef uint8_t RoomAlpha[ROOM_SIZE][ROOM_SIZE];

// Every obstacle opaque (entering a room).
void room_fade_reset(RoomAlpha alpha);

// Tweens each obstacle towards faded or opaque depending on whether it hides
// the entity at (x, z).
void room_fade_update(const Room *r, RoomAlpha alpha, fx x, fx z,
                      fx sprite_half_w, fx sprite_h);

#endif // DELVE_ROOM_H
