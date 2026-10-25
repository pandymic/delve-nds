// The areas table: everything that differs between the four areas (area.h).

#include "area.h"
#include "audio.h"
#include "entity.h"
#include "room.h"
#include "run.h"

_Static_assert(ENEMY_TYPE_COUNT <= AREA_MAX_ENEMY_TYPES, "area enemy tables");

// Sky themes (parallax.c): 0 indigo, 1 teal, 2 ember, 3 moss, 4 violet,
// 6 crystal cave, 7 deep cave, 8 inferno, 9 ash.
const AreaDef area_defs[AREA_COUNT] = {
    [AREA_DUNGEON] = {
        .banner = "THE DUNGEON DEPTHS", .name = "the Dungeon",
        .waypoint = "The Dungeon",
        .boss = ENEMY_WARLORD, .music = MUSIC_DUNGEON, .sky = { 0, 1 },
        // square notch cross tee ring split pits
        .shapes = { 20, 20, 10, 10, 12, 10, 18, 0, 0 },
        .pct = {
            [ENEMY_CHASER] = 100, [ENEMY_SHOOTER] = 100, [ENEMY_BRUTE] = 100,
            [ENEMY_ARCHER] = 100, [ENEMY_CASTER] = 100, [ENEMY_SHAMAN] = 100,
            [ENEMY_TURRET] = 100,
        },
        .traps = { HAZ_SPIKES, HAZ_FIRE, HAZ_SAW },
        .wall_light = DECOR_TORCH, .banners = true, .masonry = true,
        .pit_cause = CAUSE_PIT,
    },
    // More pits and chasms; shamans and archers over orcs.
    [AREA_SEWERS] = {
        .banner = "THE SUNKEN SEWERS", .name = "the Sewers",
        .waypoint = "The Sewers",
        .boss = ENEMY_SLIME_KING, .music = MUSIC_SEWERS, .sky = { 3, 4 },
        .shapes = { 10, 14, 8, 8, 18, 18, 24, 0, 0 },
        .pct = {
            [ENEMY_CHASER] = 60, [ENEMY_SHOOTER] = 80, [ENEMY_BRUTE] = 80,
            [ENEMY_ARCHER] = 150, [ENEMY_CASTER] = 100, [ENEMY_SHAMAN] = 200,
            [ENEMY_TURRET] = 100,
        },
        .traps = { HAZ_SPIKES, HAZ_FIRE, HAZ_SAW },
        .wall_light = DECOR_TORCH, .banners = true, .masonry = true,
        .pit_cause = CAUSE_WATER,
    },
    // Raw rock lit by crystals: ragged caves, chasms, falling rock. Bats,
    // diggers and crystals; archers and brutes from above.
    [AREA_CAVERNS] = {
        .banner = "THE CRYSTAL CAVERNS", .name = "the Caverns",
        .waypoint = "The Caverns",
        .boss = ENEMY_GOLEM, .music = MUSIC_CAVERNS, .sky = { 6, 7 },
        .shapes = { 4, 10, 4, 4, 10, 16, 12, 40, 0 },
        .pct = {
            [ENEMY_CHASER] = 40, [ENEMY_SHOOTER] = 50, [ENEMY_BRUTE] = 90,
            [ENEMY_ARCHER] = 110, [ENEMY_CASTER] = 60,
        },
        .native = { [ENEMY_BAT] = 26, [ENEMY_DIGGER] = 16, [ENEMY_CRYSTAL] = 10 },
        .traps = { HAZ_ROCKFALL, HAZ_SPIKES, HAZ_ROCKFALL },
        .wall_light = DECOR_CRYSTAL, .banners = false, .masonry = false,
        .pit_cause = CAUSE_PIT, .count_pct = 90,
    },
    // Basalt halls over lava: rivers, pools, crust that gives way, fire
    // jets. Fire slimes, magma brutes and imps; warlocks and idols.
    [AREA_MOLTEN] = {
        .banner = "THE MOLTEN DEPTHS", .name = "the Molten Depths",
        .waypoint = "Molten Depths",
        .boss = ENEMY_MAGMA_LORD, .music = MUSIC_MOLTEN, .sky = { 8, 9 },
        .shapes = { 16, 14, 6, 6, 12, 8, 14, 0, 20 },
        .pct = {
            [ENEMY_CHASER] = 50, [ENEMY_BRUTE] = 30, [ENEMY_ARCHER] = 50,
            [ENEMY_CASTER] = 110, [ENEMY_SHAMAN] = 60, [ENEMY_TURRET] = 70,
        },
        .native = { [ENEMY_FIRE_SLIME] = 18, [ENEMY_MAGMA_BRUTE] = 7,
                    [ENEMY_IMP] = 22 },
        .traps = { HAZ_CRUST, HAZ_FIRE, HAZ_CRUST },
        .wall_light = DECOR_TORCH, .banners = false, .masonry = true,
        .pit_cause = CAUSE_LAVA, .count_pct = 85,
    },
};
