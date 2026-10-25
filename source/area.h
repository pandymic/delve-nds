// Areas: each run goes down through them, ten depths each, the last
// one a boss. Everything that differs between areas lives in one table
// (area.c): names, boss, music, sky, room shapes, enemy mix, traps. Pure
// logic.

#ifndef DELVE_AREA_H
#define DELVE_AREA_H

#include <stdbool.h>
#include <stdint.h>

#define AREA_DEPTHS 10

typedef enum {
    AREA_DUNGEON,    // depths 1-10
    AREA_SEWERS,     // 11-20
    AREA_CAVERNS,    // 21-30
    AREA_MOLTEN,     // 31-40
    AREA_COUNT
} AreaId;

// After the last area the order starts again (harder: difficulty() goes
// by depth).
static inline int room_area(int depth)
{
    return depth < 1 ? AREA_DUNGEON : ((depth - 1) / AREA_DEPTHS) % AREA_COUNT;
}

static inline bool depth_is_boss(int depth)
{
    return depth > 0 && 0 == depth % AREA_DEPTHS;
}

// First depth of an area on the first way down (waypoints start there).
static inline int area_first_depth(int area)
{
    return area * AREA_DEPTHS + 1;
}

// Room shapes (roomgen.c), weighted per area.
typedef enum {
    SHAPE_SQUARE,
    SHAPE_NOTCH,     // one or two corners cut away
    SHAPE_CROSS,     // all four corners: a plus
    SHAPE_TEE,       // two corners on one side
    SHAPE_RING,      // a pit in the middle, floor around it
    SHAPE_SPLIT,     // a gap or chasm across, crossed by a bridge
    SHAPE_PITS,      // a few small pits
    SHAPE_CAVE,      // ragged edges and rock outcrops
    SHAPE_RIVER,     // a winding river of lava across, with bridges
    SHAPE_COUNT
} RoomShape;

// Enemy types count up to here (entity.h checks it).
#define AREA_MAX_ENEMY_TYPES 24

typedef struct {
    const char *banner;      // shown on arrival: "THE SUNKEN SEWERS"
    const char *name;        // in text: "the Sewers"
    const char *waypoint;    // the crypt's choice: "The Sewers"
    uint8_t boss;            // EnemyType
    uint8_t music;           // Music (audio.h)
    uint8_t sky[2];          // parallax themes: first and second half
    uint8_t shapes[SHAPE_COUNT];   // shape weights
    // Spawn weights: difficulty()'s weights scaled by pct (100 = as is),
    // plus the area's own enemies at a flat weight.
    uint8_t pct[AREA_MAX_ENEMY_TYPES];
    uint8_t native[AREA_MAX_ENEMY_TYPES];
    // Trap kinds: difficulty() unlocks up to three kind slots by depth;
    // each area says which HazardKind each slot is.
    uint8_t traps[3];
    // Dressing: what lights the walls (DECOR_TORCH or DECOR_CRYSTAL),
    // whether banners and chains hang there, and whether the floor is
    // worked stone (grates and pressure plates).
    uint8_t wall_light;
    bool banners;
    bool masonry;
    uint8_t pit_cause;       // what falling in is (run.h CAUSE_*): a pit,
                             // the Sewers' water, lava
    uint8_t count_pct;       // enemies per room vs difficulty() (0 =
                             // 100): fewer where the room itself is the
                             // danger
} AreaDef;

extern const AreaDef area_defs[AREA_COUNT];

#endif // DELVE_AREA_H
