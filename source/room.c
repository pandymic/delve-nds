// Room geometry: doors, collision, sight lines, paths and the occlusion
// fade (room.h).

#include <string.h>

#include "camera.h"
#include "room.h"

void room_clear(Room *r)
{
    memset(r, 0, sizeof(*r));
    r->entry_side = SIDE_NONE;
    r->feature_x = r->feature_z = -1;
}

void door_center(WallSide side, int pos, fx *x, fx *z)
{
    const fx mid = FX_INT(pos) + FX_HALF;
    const fx size = FX_INT(ROOM_SIZE);
    switch (side) {
    case SIDE_NX: *x = 0;    *z = mid;  break;
    case SIDE_NZ: *x = mid;  *z = 0;    break;
    case SIDE_PX: *x = size; *z = mid;  break;
    default:      *x = mid;  *z = size; break;
    }
}

void side_inward(WallSide side, fx *nx, fx *nz)
{
    *nx = SIDE_NX == side ? FX_ONE : SIDE_PX == side ? -FX_ONE : 0;
    *nz = SIDE_NZ == side ? FX_ONE : SIDE_PZ == side ? -FX_ONE : 0;
}

void door_cell(WallSide side, int pos, int *cx, int *cz)
{
    switch (side) {
    case SIDE_NX: *cx = 0;             *cz = pos;           break;
    case SIDE_NZ: *cx = pos;           *cz = 0;             break;
    case SIDE_PX: *cx = ROOM_SIZE - 1; *cz = pos;           break;
    default:      *cx = pos;           *cz = ROOM_SIZE - 1; break;
    }
}

int room_door_at(const Room *r, WallSide side, int pos)
{
    for (int i = 0; i < r->door_count; i++)
        if (r->doors[i].side == side && r->doors[i].pos == pos)
            return i;
    return -1;
}

bool room_connected(const Room *r, int sx, int sz)
{
    uint8_t seen[ROOM_SIZE][ROOM_SIZE] = { { 0 } };
    uint8_t queue[ROOM_SIZE * ROOM_SIZE];
    int head = 0, tail = 0, reached = 0, floor = 0;

    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            floor += !room_solid(r, x, z);
    if (room_solid(r, sx, sz))
        return false;

    seen[sz][sx] = 1;
    queue[tail++] = (uint8_t)(sz * ROOM_SIZE + sx);
    while (head < tail) {
        int c = queue[head++];
        int cx = c % ROOM_SIZE, cz = c / ROOM_SIZE;
        reached++;
        static const int8_t nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int i = 0; i < 4; i++) {
            int nx = cx + nb[i][0], nz = cz + nb[i][1];
            if (room_solid(r, nx, nz) || seen[nz][nx])
                continue;
            seen[nz][nx] = 1;
            queue[tail++] = (uint8_t)(nz * ROOM_SIZE + nx);
        }
    }
    return reached == floor;
}

void room_make_test(Room *r)
{
    room_clear(r);
    r->cells[3][3] = CELL_PILLAR;
    r->cells[3][8] = CELL_PILLAR;
    r->cells[8][3] = CELL_PILLAR;
    r->cells[8][8] = CELL_PILLAR;
    r->cells[6][5] = CELL_CRATE;
    r->cells[6][6] = CELL_CRATE;
    r->cells[5][10] = CELL_CRATE;
}

bool room_solid(const Room *r, int x, int z)
{
    return !room_is_open(r, x, z);
}

bool room_over_pit(const Room *r, fx x, fx z)
{
    int cx = FX_TO_INT(x), cz = FX_TO_INT(z);
    return room_in_bounds(cx, cz) && CELL_PIT == r->cells[cz][cx];
}

fx room_cell_inset(CellType t)
{
    switch (t) {
    case CELL_PILLAR: return FX(0.12);
    case CELL_CRATE:  return FX(0.15);
    case CELL_CHEST:  return FX(0.2);
    case CELL_FIRE:   return FX(0.25);
    case CELL_PROP:   return FX(0.2);
    default:          return 0;
    }
}

fx room_cell_height(CellType t)
{
    switch (t) {
    case CELL_PILLAR: return FX(1.6);
    case CELL_CRATE:  return FX(0.8);
    case CELL_CHEST:  return FX(0.6);
    case CELL_FIRE:   return FX(0.5);
    case CELL_PROP:   return FX(0.9);
    default:          return 0;
    }
}

typedef struct {
    fx x0, z0, x1, z1;
} Box;

// The solid box of a cell, if it has one. Pits are solid only when
// pits_solid is set.
static bool cell_box(const Room *r, int cx, int cz, Box *b, bool pits_solid)
{
    if (!room_in_bounds(cx, cz))
        return false;
    CellType t = room_cell(r, cx, cz);
    if (CELL_FLOOR == t || (CELL_PIT == t && !pits_solid))
        return false;
    fx in = room_cell_inset(t);
    b->x0 = FX_INT(cx) + in;
    b->z0 = FX_INT(cz) + in;
    b->x1 = FX_INT(cx + 1) - in;
    b->z1 = FX_INT(cz + 1) - in;
    return true;
}

static bool overlaps(const Box *b, fx x, fx z, fx half)
{
    return x + half > b->x0 && x - half < b->x1
        && z + half > b->z0 && z - half < b->z1;
}

bool room_blocked(const Room *r, fx x, fx z, fx half)
{
    const fx size = FX_INT(ROOM_SIZE);
    if (x - half < 0 || z - half < 0 || x + half > size || z + half > size)
        return true;
    for (int cz = FX_TO_INT(z - half); cz <= FX_TO_INT(z + half); cz++) {
        for (int cx = FX_TO_INT(x - half); cx <= FX_TO_INT(x + half); cx++) {
            Box b;
            if (cell_box(r, cx, cz, &b, false) && overlaps(&b, x, z, half))
                return true;
        }
    }
    return false;
}

// Moves along one axis and pushes back out of anything hit.
static fx move_axis(const Room *r, fx x, fx z, fx d, fx half, bool along_x,
                    bool pits_solid)
{
    const fx size = FX_INT(ROOM_SIZE);
    fx p = (along_x ? x : z) + d;
    fx q = along_x ? z : x;
    p = fx_clamp(p, half, size - half);

    for (int cq = FX_TO_INT(q - half); cq <= FX_TO_INT(q + half); cq++) {
        for (int cp = FX_TO_INT(p - half); cp <= FX_TO_INT(p + half); cp++) {
            Box b;
            if (!cell_box(r, along_x ? cp : cq, along_x ? cq : cp, &b,
                          pits_solid))
                continue;
            fx bx = along_x ? p : q, bz = along_x ? q : p;
            if (!overlaps(&b, bx, bz, half))
                continue;
            fx lo = along_x ? b.x0 : b.z0;
            fx hi = along_x ? b.x1 : b.z1;
            // Push out on the side we came from.
            p = d > 0 ? lo - half : hi + half;
        }
    }
    return p;
}

static void move_box(const Room *r, fx *x, fx *z, fx dx, fx dz, fx half,
                     bool pits_solid)
{
    if (dx)
        *x = move_axis(r, *x, *z, dx, half, true, pits_solid);
    if (dz)
        *z = move_axis(r, *x, *z, dz, half, false, pits_solid);
}

void room_move(const Room *r, fx *x, fx *z, fx dx, fx dz, fx half)
{
    move_box(r, x, z, dx, dz, half, true);
}

void room_walk(const Room *r, fx *x, fx *z, fx dx, fx dz, fx half)
{
    move_box(r, x, z, dx, dz, half, false);
}

static bool line_test(const Room *r, fx x0, fx z0, fx x1, fx z1,
                      bool pits_block)
{
    // Samples every quarter tile; obstacles are at least half a tile wide.
    fx dx = x1 - x0, dz = z1 - z0;
    fx len = fx_abs(dx) > fx_abs(dz) ? fx_abs(dx) : fx_abs(dz);
    int steps = FX_TO_INT(len * 4) + 1;
    for (int i = 1; i < steps; i++) {
        fx x = x0 + dx * i / steps, z = z0 + dz * i / steps;
        int cx = FX_TO_INT(x), cz = FX_TO_INT(z);
        Box b;
        if (cell_box(r, cx, cz, &b, pits_block) && overlaps(&b, x, z, 0))
            return false;
    }
    return true;
}

bool room_line_clear(const Room *r, fx x0, fx z0, fx x1, fx z1)
{
    return line_test(r, x0, z0, x1, z1, false);
}

bool room_line_walkable(const Room *r, fx x0, fx z0, fx x1, fx z1)
{
    return line_test(r, x0, z0, x1, z1, true);
}

void room_path_map(const Room *r, int cx, int cz,
                   uint8_t dist[ROOM_SIZE][ROOM_SIZE])
{
    uint8_t queue[ROOM_SIZE * ROOM_SIZE];
    int head = 0, tail = 0;
    memset(dist, 0xFF, ROOM_SIZE * ROOM_SIZE);
    if (!room_is_open(r, cx, cz))
        return;
    dist[cz][cx] = 0;
    queue[tail++] = (uint8_t)(cz * ROOM_SIZE + cx);
    while (head < tail) {
        int c = queue[head++];
        int x = c % ROOM_SIZE, z = c / ROOM_SIZE;
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, nz = z + dz;
                if ((!dx && !dz) || !room_is_open(r, nx, nz)
                    || 0xFF != dist[nz][nx])
                    continue;
                // No cutting corners past something solid.
                if (dx && dz && (!room_is_open(r, x + dx, z)
                                 || !room_is_open(r, x, z + dz)))
                    continue;
                dist[nz][nx] = (uint8_t)(dist[z][x] + 1);
                queue[tail++] = (uint8_t)(nz * ROOM_SIZE + nx);
            }
        }
    }
}

bool room_occludes(const Room *r, int cx, int cz, fx x, fx z,
                   fx sprite_half_w, fx sprite_h)
{
    Box b;
    CellType t = room_cell(r, cx, cz);
    if ((CELL_PILLAR != t && CELL_CRATE != t) || !cell_box(r, cx, cz, &b, true))
        return false;

    // Only obstacles nearer the camera than the entity can hide it. The
    // camera looks along -(x + z), so a larger x + z is nearer.
    fx box_depth = (b.x0 + b.x1 + b.z0 + b.z1) / 2;
    if (box_depth <= x + z)
        return false;

    // Screen rectangle of the box: right spans the corners, up spans the
    // ground footprint plus the projected height.
    fx r0 = camera_screen_right(b.x0, b.z1);
    fx r1 = camera_screen_right(b.x1, b.z0);
    fx u_low = camera_screen_up(b.x1, b.z1);
    fx u_high = camera_screen_up(b.x0, b.z0)
              + fx_mul(room_cell_height(room_cell(r, cx, cz)), CAM_COS_PITCH);

    // Screen rectangle of the sprite.
    fx sr = camera_screen_right(x, z);
    fx su = camera_screen_up(x, z);
    fx sh = fx_mul(sprite_h, CAM_COS_PITCH);

    return sr + sprite_half_w > r0 && sr - sprite_half_w < r1
        && su + sh > u_low && su < u_high;
}

void room_fade_reset(RoomAlpha alpha)
{
    memset(alpha, FADE_ALPHA_OPAQUE, sizeof(RoomAlpha));
}

void room_fade_update(const Room *r, RoomAlpha alpha, fx x, fx z,
                      fx sprite_half_w, fx sprite_h)
{
    for (int cz = 0; cz < ROOM_SIZE; cz++) {
        for (int cx = 0; cx < ROOM_SIZE; cx++) {
            int a = alpha[cz][cx];
            if (room_occludes(r, cx, cz, x, z, sprite_half_w, sprite_h))
                a = a - FADE_ALPHA_STEP < FADE_ALPHA_MIN
                  ? FADE_ALPHA_MIN : a - FADE_ALPHA_STEP;
            else
                a = a + FADE_ALPHA_STEP > FADE_ALPHA_OPAQUE
                  ? FADE_ALPHA_OPAQUE : a + FADE_ALPHA_STEP;
            alpha[cz][cx] = (uint8_t)a;
        }
    }
}
