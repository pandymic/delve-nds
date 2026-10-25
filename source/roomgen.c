// Seeded room generation: shapes, doors, obstacles, enemies, dressing,
// traps, and the fixed camp (roomgen.h).

#include <string.h>
#include <stddef.h>

#include "difficulty.h"
#include "entity.h"
#include "rng.h"
#include "roomgen.h"

// Cells kept clear while generating: RES_CLEAR keeps obstacles and room
// cuts off a cell, RES_NO_ENEMY also keeps enemies from spawning there.
enum { RES_NONE, RES_CLEAR, RES_NO_ENEMY };
typedef uint8_t Reserved[ROOM_SIZE][ROOM_SIZE];

#define DOOR_POS_MIN        2
#define DOOR_POS_MAX        (ROOM_SIZE - 3)
#define SPAWN_INSET         FX(1.2)   // spawn distance from the entry wall
#define ENEMY_MIN_DIST      FX(4.5)   // from the player spawn

uint32_t roomgen_seed(uint32_t run_seed, int depth, int door_index)
{
    return rng_hash3(run_seed, (uint32_t)depth, (uint32_t)door_index);
}

static void reserve_area(Reserved res, int cx, int cz, int radius)
{
    for (int z = cz - radius; z <= cz + radius; z++)
        for (int x = cx - radius; x <= cx + radius; x++)
            if (room_in_bounds(x, z))
                res[z][x] = RES_CLEAR;
}

static void reserve_door(Reserved res, WallSide side, int pos)
{
    int cx, cz;
    door_cell(side, pos, &cx, &cz);
    reserve_area(res, cx, cz, 1);
    // And one more cell inwards so the doorway is never blocked.
    fx nx, nz;
    side_inward(side, &nx, &nz);
    reserve_area(res, cx + FX_TO_INT(nx) * 2, cz + FX_TO_INT(nz) * 2, 0);
}

// A doorway needs floor at the wall and one cell further in.
static bool door_ok(const Room *r, WallSide side, int pos)
{
    int cx, cz;
    door_cell(side, pos, &cx, &cz);
    fx nx, nz;
    side_inward(side, &nx, &nz);
    return room_is_open(r, cx, cz)
        && room_is_open(r, cx + FX_TO_INT(nx), cz + FX_TO_INT(nz));
}

// Room shapes: cut from the full square, one operation at a time, each
// undone if it breaks the room (see shape_ok). RoomShape and each area's
// shape weights are in area.h and area.c.

#define MIN_FLOOR_CELLS 64

static int floor_cells(const Room *r)
{
    int n = 0;
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            n += room_is_open(r, x, z);
    return n;
}

// After a cut: still connected, big enough, and with somewhere for an exit
// door besides the entry wall.
static bool shape_ok(const Room *r, int scx, int scz)
{
    if (!room_connected(r, scx, scz) || floor_cells(r) < MIN_FLOOR_CELLS)
        return false;
    for (int s = 0; s < SIDE_COUNT; s++) {
        if (s == r->entry_side)
            continue;
        for (int pos = DOOR_POS_MIN; pos <= DOOR_POS_MAX; pos++)
            if (door_ok(r, (WallSide)s, pos))
                return true;
    }
    return false;
}

// Sets cells in [x0, x1] x [z0, z1] to t, skipping reserved ones.
static void carve(Room *r, const Reserved res, int x0, int z0, int x1, int z1,
                  CellType t)
{
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            if (room_in_bounds(x, z) && !res[z][x])
                r->cells[z][x] = (uint8_t)t;
}

// Carves `width` cells across the room at step i of a band running along
// x (or z), starting at `at` on the other axis.
static void carve_across(Room *r, const Reserved res, bool along_x, int i,
                         int at, int width, CellType t)
{
    for (int w = 0; w < width; w++) {
        if (along_x)
            carve(r, res, i, at + w, i, at + w, t);
        else
            carve(r, res, at + w, i, at + w, i, t);
    }
}

// Index picked with probability w[i] / sum(w), or `fallback` when every
// weight is zero (no random number is drawn then).
static int pick_weighted(Rng *rng, const int *w, int n, int fallback)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += w[i];
    if (0 == total)
        return fallback;
    int roll = (int)rng_range(rng, (uint32_t)total);
    for (int i = 0; i < n; i++) {
        if (roll < w[i])
            return i;
        roll -= w[i];
    }
    return fallback;
}

static void cut_corner(Room *r, const Reserved res, int corner, int w, int h)
{
    int x0 = corner & 1 ? ROOM_SIZE - w : 0;
    int z0 = corner & 2 ? ROOM_SIZE - h : 0;
    carve(r, res, x0, z0, x0 + w - 1, z0 + h - 1, CELL_VOID);
}

static void apply_shape(Room *r, Rng *rng, const Reserved res, RoomShape shape)
{
    switch (shape) {
    case SHAPE_NOTCH: {
        int n = rng_between(rng, 1, 2);
        for (int i = 0; i < n; i++)
            cut_corner(r, res, (int)rng_range(rng, 4), rng_between(rng, 2, 4),
                       rng_between(rng, 2, 4));
        break;
    }
    case SHAPE_CROSS: {
        int k = rng_between(rng, 3, 4);
        for (int c = 0; c < 4; c++)
            cut_corner(r, res, c, k, k);
        break;
    }
    case SHAPE_TEE: {
        // Corners 0+1 (low z), 2+3 (high z), 0+2 (low x) or 1+3 (high x).
        static const int8_t pairs[4][2] = { { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 } };
        int p = (int)rng_range(rng, 4);
        int k = rng_between(rng, 3, 4);
        cut_corner(r, res, pairs[p][0], k, k);
        cut_corner(r, res, pairs[p][1], k, k);
        break;
    }
    case SHAPE_RING: {
        int k = rng_between(rng, 2, 4);
        int x0 = rng_between(rng, 3, ROOM_SIZE - 3 - k);
        int z0 = rng_between(rng, 3, ROOM_SIZE - 3 - k);
        carve(r, res, x0, z0, x0 + k - 1, z0 + k - 1, CELL_PIT);
        break;
    }
    case SHAPE_SPLIT: {
        bool along_x = rng_chance(rng, 50);
        int at = rng_between(rng, 4, ROOM_SIZE - 5);
        int width = rng_between(rng, 1, 2);
        int bridge = rng_between(rng, 2, ROOM_SIZE - 4);
        CellType t = rng_chance(rng, 50) ? CELL_PIT : CELL_VOID;
        for (int i = 0; i < ROOM_SIZE; i++)
            if (i != bridge && i != bridge + 1)
                carve_across(r, res, along_x, i, at, width, t);
        break;
    }
    case SHAPE_CAVE: {
        // Ragged walls: each side eaten into by 0-2 cells, the depth
        // wandering along it; then a few rock outcrops inside.
        for (int s = 0; s < SIDE_COUNT; s++) {
            int depth = rng_between(rng, 0, 2);
            fx nx, nz;
            side_inward((WallSide)s, &nx, &nz);
            for (int pos = 0; pos < ROOM_SIZE; pos++) {
                if (rng_chance(rng, 40))
                    depth += rng_chance(rng, 50) ? 1 : -1;
                depth = iclamp(depth, 0, 2);
                int cx, cz;
                door_cell((WallSide)s, pos, &cx, &cz);
                for (int k = 0; k < depth; k++) {
                    int x = cx + FX_TO_INT(nx) * k, z = cz + FX_TO_INT(nz) * k;
                    carve(r, res, x, z, x, z, CELL_VOID);
                }
            }
        }
        int n = rng_between(rng, 1, 3);
        for (int i = 0; i < n; i++) {
            int x0 = rng_between(rng, 3, ROOM_SIZE - 4);
            int z0 = rng_between(rng, 3, ROOM_SIZE - 4);
            bool wide = rng_chance(rng, 50);
            carve(r, res, x0, z0, x0 + wide, z0 + !wide, CELL_PILLAR);
        }
        break;
    }
    case SHAPE_RIVER: {
        // A river of lava (pits) across the room, wandering by a cell at
        // a time, two cells wide, with one or two bridges.
        bool along_x = rng_chance(rng, 50);
        int at = rng_between(rng, 4, ROOM_SIZE - 6);
        int bridges[2] = { rng_between(rng, 1, 5),
                           rng_between(rng, 6, ROOM_SIZE - 2) };
        int n_bridges = rng_between(rng, 1, 2);
        for (int i = 0; i < ROOM_SIZE; i++) {
            if (i > 0 && rng_chance(rng, 35))
                at += rng_chance(rng, 50) ? 1 : -1;
            at = iclamp(at, 3, ROOM_SIZE - 5);
            bool bridge = false;
            for (int b = 0; b < n_bridges; b++)
                bridge |= i == bridges[b];
            if (!bridge)
                carve_across(r, res, along_x, i, at, 2, CELL_PIT);
        }
        break;
    }
    case SHAPE_PITS: {
        int n = rng_between(rng, 1, 3);
        for (int i = 0; i < n; i++) {
            int w = rng_between(rng, 1, 2), h = rng_between(rng, 1, 2);
            int x0 = rng_between(rng, 2, ROOM_SIZE - 2 - w);
            int z0 = rng_between(rng, 2, ROOM_SIZE - 2 - h);
            carve(r, res, x0, z0, x0 + w - 1, z0 + h - 1, CELL_PIT);
        }
        break;
    }
    default:
        break;
    }
}

// Picks and cuts the room's shape. The first room of a run stays square.
static void shape_room(Room *r, Rng *rng, const Reserved res, int depth,
                       int scx, int scz)
{
    if (depth <= 1 || RT_BOSS == r->type)
        return;   // the first room and boss arenas stay square
    int weights[SHAPE_COUNT];
    for (int i = 0; i < SHAPE_COUNT; i++)
        weights[i] = area_defs[r->area].shapes[i];
    RoomShape shape = (RoomShape)pick_weighted(rng, weights, SHAPE_COUNT,
                                               SHAPE_SQUARE);
    // A few tries; a failed cut is rolled back to the full square.
    uint8_t backup[ROOM_SIZE][ROOM_SIZE];
    memcpy(backup, r->cells, sizeof(backup));
    for (int attempt = 0; attempt < 4; attempt++) {
        apply_shape(r, rng, res, shape);
        if (shape_ok(r, scx, scz))
            return;
        memcpy(r->cells, backup, sizeof(backup));
    }
}

// Weighted reward roll for an exit door.
static RoomType roll_reward(Rng *rng, int depth)
{
    uint32_t r = rng_range(rng, 100);
    if (depth_is_boss(depth + 1))
        return RT_BOSS;            // the way on goes through the boss
    if (depth < 2)
        return RT_COMBAT;          // first doors always lead to a fight
    if (r < 62)
        return RT_COMBAT;
    if (r < 82)
        return RT_TREASURE;
    return RT_REST;
}

static void place_doors(Room *r, Rng *rng, int depth, Reserved res)
{
    WallSide sides[SIDE_COUNT - 1];
    int n = 0;
    for (int s = 0; s < SIDE_COUNT; s++)
        if (s != r->entry_side)
            sides[n++] = (WallSide)s;
    // Fisher-Yates shuffle of the candidate walls.
    for (int i = n - 1; i > 0; i--) {
        int j = (int)rng_range(rng, (uint32_t)i + 1);
        WallSide t = sides[i];
        sides[i] = sides[j];
        sides[j] = t;
    }

    // Only positions where the room's shape has floor at the doorway.
    // Before a boss there is only one way on.
    int wanted = rng_between(rng, 1, MAX_DOORS);
    if (depth_is_boss(depth + 1))
        wanted = 1;
    r->door_count = 0;
    for (int i = 0; i < n && r->door_count < wanted; i++) {
        int options[ROOM_SIZE], count = 0;
        for (int pos = DOOR_POS_MIN; pos <= DOOR_POS_MAX; pos++)
            if (door_ok(r, sides[i], pos))
                options[count++] = pos;
        if (0 == count)
            continue;
        Door *d = &r->doors[r->door_count++];
        d->side = sides[i];
        d->pos = (int8_t)options[rng_range(rng, (uint32_t)count)];
        d->leads_to = roll_reward(rng, depth);
        reserve_door(res, d->side, d->pos);
    }
    // At least one door leads to a fight, so runs keep their pace.
    bool any_combat = false;
    for (int i = 0; i < r->door_count; i++)
        any_combat |= RT_COMBAT == r->doors[i].leads_to;
    if (!any_combat && !depth_is_boss(depth + 1))
        r->doors[rng_range(rng, r->door_count)].leads_to = RT_COMBAT;
}

static void place_obstacles(Room *r, Rng *rng, const Reserved res,
                            int spawn_cx, int spawn_cz)
{
    int count = rng_between(rng, 0, ROOMGEN_MAX_OBSTACLES);
    for (int i = 0; i < count; i++) {
        for (int attempt = 0; attempt < 20; attempt++) {
            // Keep the outer ring clear so walls can always be walked along.
            int x = rng_between(rng, 1, ROOM_SIZE - 2);
            int z = rng_between(rng, 1, ROOM_SIZE - 2);
            if (res[z][x] || CELL_FLOOR != r->cells[z][x])
                continue;
            r->cells[z][x] = rng_chance(rng, 50) ? CELL_PILLAR : CELL_CRATE;
            if (room_connected(r, spawn_cx, spawn_cz))
                break;
            r->cells[z][x] = CELL_FLOOR; // rejected: it split the room
        }
    }
}

#define MAX_TURRETS_PER_ROOM 2
#define MAX_SHAMANS_PER_ROOM 2

static int count_type(const Room *r, EnemyType t)
{
    int n = 0;
    for (int i = 0; i < r->enemy_count; i++)
        n += r->enemies[i].type == t;
    return n;
}

// A weighted pick from the depth's spawn table, mixed by the area
// (area.c), with idols and shamans capped per room.
static EnemyType pick_enemy(Rng *rng, const Difficulty *d, const Room *r)
{
    const AreaDef *a = &area_defs[r->area];
    int w[ENEMY_TYPE_COUNT];
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++) {
        w[t] = d->weights[t] * a->pct[t] / 100 + a->native[t];
        if ((ENEMY_TURRET == t && count_type(r, ENEMY_TURRET) >= MAX_TURRETS_PER_ROOM)
            || (ENEMY_SHAMAN == t && count_type(r, ENEMY_SHAMAN) >= MAX_SHAMANS_PER_ROOM))
            w[t] = 0;
    }
    return (EnemyType)pick_weighted(rng, w, ENEMY_TYPE_COUNT, ENEMY_CHASER);
}

// The boss stands across the arena from the player.
static void place_boss(Room *r)
{
    int scx = FX_TO_INT(r->spawn_x), scz = FX_TO_INT(r->spawn_z);
    int bx = ROOM_SIZE - 1 - scx, bz = ROOM_SIZE - 1 - scz;
    bx = iclamp(bx, 2, ROOM_SIZE - 3);
    bz = iclamp(bz, 2, ROOM_SIZE - 3);
    r->cells[bz][bx] = CELL_FLOOR;
    EnemySpawn *s = &r->enemies[r->enemy_count++];
    s->type = area_defs[r->area].boss;
    s->elite = 0;
    s->x = FX_INT(bx) + FX_HALF;
    s->z = FX_INT(bz) + FX_HALF;
}

static void place_enemies(Room *r, Rng *rng, int depth, const Reserved res)
{
    if (RT_REST == r->type)
        return;
    if (RT_BOSS == r->type) {
        place_boss(r);
        return;
    }
    Difficulty d = difficulty(depth);
    int count = d.enemy_count;
    int pct = area_defs[r->area].count_pct;
    if (pct)
        count = (count * pct + 50) / 100;
    if (RT_TREASURE == r->type)
        count = (count + 1) / 2;

    const fx min2 = fx_mul(ENEMY_MIN_DIST, ENEMY_MIN_DIST);
    for (int i = 0; i < count && r->enemy_count < MAX_ENEMIES; i++) {
        for (int attempt = 0; attempt < 30; attempt++) {
            int x = rng_between(rng, 0, ROOM_SIZE - 1);
            int z = rng_between(rng, 0, ROOM_SIZE - 1);
            if (CELL_FLOOR != r->cells[z][x] || RES_NO_ENEMY == res[z][x])
                continue;
            fx ex = FX_INT(x) + FX_HALF, ez = FX_INT(z) + FX_HALF;
            fx dx = ex - r->spawn_x, dz = ez - r->spawn_z;
            if (fx_mul(dx, dx) + fx_mul(dz, dz) < min2)
                continue;
            EnemySpawn *s = &r->enemies[r->enemy_count++];
            s->type = (uint8_t)pick_enemy(rng, &d, r);
            s->elite = rng_chance(rng, (uint32_t)d.elite_chance);
            s->x = ex;
            s->z = ez;
            break;
        }
    }
}

// Dressing

#define DECOR_WALL_GAP      3   // min tiles between wall-mounted props
#define DECOR_CLUTTER_MIN   2
#define DECOR_CLUTTER_MAX   5
#define DECOR_DECALS_MAX    3
#define DECOR_WALL_HUG      FX(0.22)  // clutter offset towards its wall

static Decor *add_decor(Room *r, DecorKind kind, int variant, WallSide wall,
                        fx x, fx z)
{
    if (r->decor_count >= MAX_DECOR)
        return NULL;
    Decor *d = &r->decor[r->decor_count++];
    d->kind = (uint8_t)kind;
    d->variant = (uint8_t)variant;
    d->wall = wall;
    d->x = x;
    d->z = z;
    return d;
}

// True if wall position pos on side is a doorway or next to one.
static bool near_doorway(const Room *r, WallSide side, int pos)
{
    for (int i = 0; i < r->door_count; i++)
        if (r->doors[i].side == side && pos >= r->doors[i].pos - 1
            && pos <= r->doors[i].pos + 1)
            return true;
    return r->entry_side == side && pos >= r->entry_pos - 1
        && pos <= r->entry_pos + 1;
}

// Torches, chains and banners on the two back walls (the ones facing the
// camera), and cobwebs high in their corners.
static void place_wall_props(Room *r, Rng *rng)
{
    static const WallSide backs[2] = { SIDE_NX, SIDE_NZ };
    int torches = 0;
    for (int w = 0; w < 2; w++) {
        WallSide side = backs[w];
        int last = -DECOR_WALL_GAP;
        int start = rng_between(rng, 1, 3);
        for (int pos = start; pos < ROOM_SIZE - 1; pos++) {
            int wcx, wcz;
            door_cell(side, pos, &wcx, &wcz);
            if (pos - last < DECOR_WALL_GAP || near_doorway(r, side, pos)
                || !room_is_open(r, wcx, wcz))
                continue;
            if (!rng_chance(rng, 55))
                continue;
            fx x, z;
            door_center(side, pos, &x, &z);
            const AreaDef *a = &area_defs[r->area];
            bool torch = torches < MAX_TORCH_LIGHTS && !rng_chance(rng, 25);
            bool banner = rng_chance(rng, 50);
            if (torch) {
                torches++;
                add_decor(r, (DecorKind)a->wall_light, 0, side, x, z);
            } else if (!a->banners) {
                continue;   // bare rock between the crystals
            } else if (banner) {
                add_decor(r, DECOR_BANNER, (int)rng_range(rng, 2), side, x, z);
            } else {
                add_decor(r, DECOR_CHAIN, 0, side, x, z);
            }
            last = pos;
        }
        // A cobweb in one of this wall's corners now and then.
        if (rng_chance(rng, 40)) {
            int pos = rng_chance(rng, 50) ? 0 : ROOM_SIZE - 1;
            int wcx, wcz;
            door_cell(side, pos, &wcx, &wcz);
            if (!near_doorway(r, side, pos) && room_is_open(r, wcx, wcz)) {
                fx x, z;
                door_center(side, pos, &x, &z);
                add_decor(r, DECOR_COBWEB, 0, side, x, z);
            }
        }
    }
}

// Barrels (some explosive), pots and skulls in floor cells along the walls.
static void place_clutter(Room *r, Rng *rng, const Reserved res,
                          int explosive_pct)
{
    int count = rng_between(rng, DECOR_CLUTTER_MIN, DECOR_CLUTTER_MAX);
    for (int i = 0; i < count; i++) {
        for (int attempt = 0; attempt < 12; attempt++) {
            // A random cell on the outer ring and the wall it touches.
            WallSide side = (WallSide)rng_range(rng, SIDE_COUNT);
            int pos = rng_between(rng, 0, ROOM_SIZE - 1);
            int cx, cz;
            door_cell(side, pos, &cx, &cz);
            if (res[cz][cx] || CELL_FLOOR != r->cells[cz][cx])
                continue;
            fx nx, nz;
            side_inward(side, &nx, &nz);
            fx x = FX_INT(cx) + FX_HALF - fx_mul(nx, DECOR_WALL_HUG);
            fx z = FX_INT(cz) + FX_HALF - fx_mul(nz, DECOR_WALL_HUG);
            // One prop per cell, and none under a torch or chain.
            bool taken = false;
            for (int j = 0; j < r->decor_count; j++) {
                const Decor *o = &r->decor[j];
                if (decor_is_decal(o->kind))
                    continue;
                int ox = FX_TO_INT(o->x), oz = FX_TO_INT(o->z);
                if (SIDE_NX == o->wall)
                    ox = 0;
                if (SIDE_NZ == o->wall)
                    oz = 0;
                taken |= ox == cx && oz == cz;
            }
            if (taken)
                continue;
            int roll = (int)rng_range(rng, 100);
            if (roll < 45)
                add_decor(r, DECOR_POT, 0, SIDE_NONE, x, z);
            else if (roll < 55)
                add_decor(r, DECOR_SKULL, 0, SIDE_NONE, x, z);
            else if (RT_REST != r->type && rng_chance(rng, (uint32_t)explosive_pct))
                add_decor(r, DECOR_EXPLOSIVE, 0, SIDE_NONE, x, z);
            else
                add_decor(r, DECOR_BARREL, (int)rng_range(rng, 2), SIDE_NONE,
                          x, z);
            break;
        }
    }
}

// Grates, plates, bones, cracks and rubble on open floor.
static void place_decals(Room *r, Rng *rng)
{
    // Grates and plates only on worked stone (not in the Caverns).
    static const uint8_t kinds[5] = {
        DECOR_BONES, DECOR_CRACK, DECOR_RUBBLE, DECOR_GRATE, DECOR_PLATE,
    };
    int n_kinds = area_defs[r->area].masonry ? 5 : 3;
    int count = rng_between(rng, 1, DECOR_DECALS_MAX);
    for (int i = 0; i < count; i++) {
        int x = rng_between(rng, 1, ROOM_SIZE - 2);
        int z = rng_between(rng, 1, ROOM_SIZE - 2);
        if (CELL_FLOOR != r->cells[z][x])
            continue;
        add_decor(r, kinds[rng_range(rng, (uint32_t)n_kinds)], 0, SIDE_NONE,
                  FX_INT(x) + FX_HALF, FX_INT(z) + FX_HALF);
    }
}

// Uses its own random stream, so dressing never changes the layout or
// enemies a seed produces.
static void place_decor(Room *r, const Reserved res, int depth)
{
    Rng rng;
    rng_seed(&rng, rng_hash3(r->seed, 0xDEC0u, 0));
    place_decals(r, &rng);
    place_wall_props(r, &rng);
    place_clutter(r, &rng, res, difficulty(depth).explosive_pct);
}

// Traps

#define SAW_MIN_LEN 4
#define SAW_MAX_LEN 6

static bool trap_cell_ok(const Room *r, const Reserved res, int x, int z)
{
    if (x < 1 || z < 1 || x > ROOM_SIZE - 2 || z > ROOM_SIZE - 2)
        return false;
    if (res[z][x] || CELL_FLOOR != r->cells[z][x])
        return false;
    // Keep traps apart, and off clutter.
    for (int i = 0; i < r->hazard_count; i++) {
        const Hazard *h = &r->hazards[i];
        int hx = FX_TO_INT(h->x), hz = FX_TO_INT(h->z);
        if (x >= hx - 1 && x <= hx + 1 && z >= hz - 1 && z <= hz + 1)
            return false;
    }
    for (int i = 0; i < r->decor_count; i++) {
        const Decor *d = &r->decor[i];
        if (!decor_is_decal(d->kind) && SIDE_NONE == d->wall
            && FX_TO_INT(d->x) == x && FX_TO_INT(d->z) == z)
            return false;
    }
    return true;
}

static void add_hazard(Room *r, Rng *rng, HazardKind kind, int x0, int z0,
                       int x1, int z1)
{
    Hazard *h = &r->hazards[r->hazard_count++];
    h->kind = (uint8_t)kind;
    h->phase = (uint8_t)rng_range(rng, 256);
    h->x = FX_INT(x0) + FX_HALF;
    h->z = FX_INT(z0) + FX_HALF;
    h->x2 = FX_INT(x1) + FX_HALF;
    h->z2 = FX_INT(z1) + FX_HALF;
}

// A straight run of free cells for a saw track.
static bool place_saw(Room *r, Rng *rng, const Reserved res)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        bool along_x = rng_chance(rng, 50);
        int len = rng_between(rng, SAW_MIN_LEN, SAW_MAX_LEN);
        int x = rng_between(rng, 1, ROOM_SIZE - 2);
        int z = rng_between(rng, 1, ROOM_SIZE - 2);
        int dx = along_x ? 1 : 0, dz = along_x ? 0 : 1;
        bool ok = true;
        for (int i = 0; i < len && ok; i++)
            ok = trap_cell_ok(r, res, x + dx * i, z + dz * i);
        if (!ok)
            continue;
        add_hazard(r, rng, HAZ_SAW, x, z, x + dx * (len - 1),
                   z + dz * (len - 1));
        return true;
    }
    return false;
}

// Spikes, fire jets and saws, by depth (difficulty.c). Never in rest rooms,
// fewer in treasure rooms. Its own random stream, like the dressing.
static void place_hazards(Room *r, const Reserved res, int depth)
{
    if (RT_REST == r->type || RT_BOSS == r->type)
        return;
    Difficulty d = difficulty(depth);
    int n = RT_TREASURE == r->type ? d.hazards / 2 : d.hazards;
    if (0 == n || 0 == d.hazard_kinds)
        return;
    Rng rng;
    rng_seed(&rng, rng_hash3(r->seed, 0x4A2u, 0));
    for (int i = 0; i < n && r->hazard_count < MAX_HAZARDS; i++) {
        HazardKind kind = (HazardKind)area_defs[r->area]
                              .traps[rng_range(&rng, (uint32_t)d.hazard_kinds)];
        if (HAZ_SAW == kind) {
            place_saw(r, &rng, res);
            continue;
        }
        for (int attempt = 0; attempt < 20; attempt++) {
            int x = rng_between(&rng, 1, ROOM_SIZE - 2);
            int z = rng_between(&rng, 1, ROOM_SIZE - 2);
            if (trap_cell_ok(r, res, x, z)) {
                add_hazard(r, &rng, kind, x, z, x, z);
                break;
            }
        }
    }
}

// The camp: a clearing at night on a hilltop. Forest behind (drawn
// by render.c past the -x and -z edges), a cliff in front, a dirt path from
// where the player arrives, past the fire, to the crypt (the "door").
static const char *const hub_dirt[ROOM_SIZE] = {
    ".....###....",   // z = 0: before the crypt
    "......#.....",
    "......#.....",
    "......#.....",
    "....###.....",
    "....#.#.....",   // the fire is at (5, 5)
    "....###.....",
    "......#.....",
    "...#######..",   // to the shrine and the dummy
    "......#.....",
    ".....##.....",
    ".....##.....",
};

void roomgen_hub(Room *r)
{
    room_clear(r);
    r->seed = 0xCA3Bu;
    r->type = RT_REST;
    r->hub = true;
    r->entry_side = SIDE_PZ;
    r->entry_pos = ROOM_SIZE / 2;
    r->spawn_x = FX(6.5);
    r->spawn_z = FX(10.3);
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            if ('#' == hub_dirt[z][x])
                r->dirt[z] |= (uint16_t)(1u << x);

    // The campfire in the middle.
    int mid = ROOM_SIZE / 2 - 1;
    r->cells[mid][mid] = CELL_FIRE;
    r->feature_x = r->feature_z = (int8_t)mid;

    // The crypt into the depths.
    r->door_count = 1;
    r->doors[0] = (Door){ SIDE_NZ, HUB_PORTAL_POS, RT_COMBAT };

    // Torches either side of the crypt's door (on its front, the z = 0
    // line), lighting the way down.
    for (int side = -1; side <= 1; side += 2) {
        fx x, z;
        door_center(SIDE_NZ, HUB_PORTAL_POS + side, &x, &z);
        add_decor(r, DECOR_TORCH, 0, SIDE_NZ, x, z);
    }

    // Props: (kind, variant, x, z, solid). Solid ones stand on CELL_PROP.
    static const struct {
        uint8_t kind, variant;
        fx x, z;
        bool solid;
    } props[] = {
        // Trees at the back corners and down the sides.
        { DECOR_TREE, TREE_ROUND,  FX(0.5),  FX(0.6),  true },
        { DECOR_TREE, TREE_PINE2,  FX(1.5),  FX(0.4),  true },
        { DECOR_TREE, TREE_FRUIT,  FX(0.5),  FX(1.7),  true },
        { DECOR_TREE, TREE_PINE,   FX(11.5), FX(0.5),  true },
        { DECOR_TREE, TREE_GREEN,  FX(11.4), FX(4.6),  true },
        { DECOR_TREE, TREE_PINE,   FX(0.5),  FX(10.5), true },
        // The tent and the well.
        { DECOR_TENT,  0, FX(1.5),  FX(4.4),  true },
        { DECOR_WELL,  0, FX(10.5), FX(2.5),  true },
        // By the fire.
        { DECOR_BENCH, 0, FX(4.15), FX(5.5),  false },
        { DECOR_BENCH, 0, FX(6.85), FX(5.5),  false },
        { DECOR_STUMP, 0, FX(5.5),  FX(3.9),  false },
        // The way down.
        { DECOR_SIGN,  0, FX(7.7),  FX(1.4),  false },
        // Scattered small things, the cliff edge in front.
        { DECOR_BUSH,   0, FX(11.5), FX(9.6),  false },
        { DECOR_BUSH,   0, FX(2.4),  FX(11.5), false },
        { DECOR_ROCK,   0, FX(9.6),  FX(11.4), false },
        { DECOR_STONES, 0, FX(3.4),  FX(9.4),  false },
        { DECOR_STONES, 0, FX(8.8),  FX(6.6),  false },
        { DECOR_STUMP,  0, FX(2.6),  FX(2.4),  false },
        { DECOR_ROCK,   0, FX(1.3),  FX(7.4),  false },
        { DECOR_BENCH,  1, FX(2.8),  FX(4.6),  false },
    };
    for (int i = 0; i < ARRAY_LEN(props); i++) {
        add_decor(r, (DecorKind)props[i].kind, props[i].variant, SIDE_NONE,
                  props[i].x, props[i].z);
        if (props[i].solid)
            r->cells[FX_TO_INT(props[i].z)][FX_TO_INT(props[i].x)] = CELL_PROP;
    }

    // The training dummy.
    r->enemies[0] = (EnemySpawn){ ENEMY_DUMMY, 0, HUB_DUMMY_X, HUB_DUMMY_Z };
    r->enemy_count = 1;
}

void roomgen_generate(Room *r, uint32_t seed, int depth, RoomType type,
                      WallSide entry_side, int entry_pos)
{
    Rng rng;
    rng_seed(&rng, seed);
    room_clear(r);
    r->seed = seed;
    r->type = type;
    r->area = (uint8_t)room_area(depth);
    r->entry_side = entry_side;
    r->entry_pos = (int8_t)entry_pos;

    Reserved res = { { 0 } };

    // 1. Player spawn, just inside the entry door.
    fx ex, ez, nx, nz;
    door_center(entry_side, entry_pos, &ex, &ez);
    side_inward(entry_side, &nx, &nz);
    r->spawn_x = ex + fx_mul(nx, SPAWN_INSET);
    r->spawn_z = ez + fx_mul(nz, SPAWN_INSET);
    int scx = FX_TO_INT(r->spawn_x), scz = FX_TO_INT(r->spawn_z);
    reserve_area(res, scx, scz, 2);
    reserve_door(res, entry_side, entry_pos);

    // 2. Room feature in the centre, kept free with its neighbours.
    int mid = ROOM_SIZE / 2 - 1;
    if (RT_REST == type || RT_TREASURE == type) {
        reserve_area(res, mid, mid, 1);
        res[mid][mid] = RES_NO_ENEMY;
    }

    // 3. The room's shape: corners cut away, pits, chasms.
    shape_room(r, &rng, res, depth, scx, scz);

    // 4. Exit doors and their rewards (before obstacles, so doorways can be
    //    kept clear).
    place_doors(r, &rng, depth, res);

    // 5. Obstacles, rejecting any that disconnect the floor.
    place_obstacles(r, &rng, res, scx, scz);

    if (RT_TREASURE == type || RT_REST == type) {
        r->cells[mid][mid] = RT_TREASURE == type ? CELL_CHEST : CELL_FIRE;
        r->feature_x = (int8_t)mid;
        r->feature_z = (int8_t)mid;
    }

    // 6. Enemy spawn points away from the player.
    place_enemies(r, &rng, depth, res);

    // 7. Dressing: torches, clutter, floor decals. 8. Traps.
    place_decor(r, res, depth);
    place_hazards(r, res, depth);
}
