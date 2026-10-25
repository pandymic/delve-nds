// Global tunables, pool sizes and hardware resource layout.
//
// Every static pool size lives here so memory use can be audited in one
// place. Platform independent: safe to include from host tests.

#ifndef DELVE_CONFIG_H
#define DELVE_CONFIG_H

// The version and build type come from the Makefile: the version from the
// VERSION file at the project root (the only place it is written), the type
// from BUILD (debug, prod or release; "host" in the host tests).
#ifndef DELVE_VERSION_STRING
#error "DELVE_VERSION_STRING comes from the VERSION file: build with make"
#endif
#ifndef DELVE_BUILD_TYPE
#error "DELVE_BUILD_TYPE comes from the Makefile's BUILD: build with make"
#endif

// Build details for the crash screen and the bug report, when known.
#ifndef DELVE_GIT_HASH
#define DELVE_GIT_HASH "unknown"
#endif
#ifndef DELVE_BLOCKSDS_VERSION
#define DELVE_BLOCKSDS_VERSION "unknown"
#endif

// Element count of a fixed-size array.
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Pools

#define MENU_MAX_ITEMS          16
#define MAX_ENEMIES             12
#define MAX_PROJECTILES         24
#define MAX_DAGGERS             6
#define MAX_ZONES               12   // marks that burst: casters, bosses, fire
#define MAX_FLAMES              10   // Flame Dash trail
#define MAX_PARTICLES           96   // effects (fx.c)
#define MAX_FLOATERS            8    // damage numbers (fx.c)
#define MAX_DOORS               3
#define MAX_DECOR               24   // props per room (torches, clutter, decals)
#define MAX_TORCH_LIGHTS        4    // torches that also light the room
#define MAX_HAZARDS             4    // spike traps, fire jets, saws per room
#define MAX_PICKUPS             16   // coins and hearts lying in a room

// Rooms

// Square room, in tiles. Rendering supports up to 15 (vertex range limit).
#define ROOM_SIZE               12

// Screen

#define SCREEN_W                256
#define SCREEN_H                192
#define TEXT_COLS               32
#define TEXT_ROWS               24

// Video memory layout (see video.c)
//
// VRAM A, B, D  Nitro Engine textures (384 KB)
// VRAM C      Sub engine BG (bottom screen UI)
// VRAM I      Sub engine sprites (icons, 16 KB)
// VRAM E      Nitro Engine texture palettes
// VRAM F, G   Main engine BG, 32 KB at 0x06000000:
//               0x0000  BG1 tiles: console font (tiles 0-95) + custom (128+)
//               0x4000  BG2/BG3 parallax tiles
//               0x6800  BG1 map (base 13)
//               0x7000  BG2 map (base 14)
//               0x7800  BG3 map (base 15)
//
// Main engine layers: BG0 = 3D, BG1 = text/HUD/logo (priority 0),
// BG2 = near parallax, BG3 = far parallax.

#define MAIN_TXT_MAP_BASE       13
#define MAIN_TXT_TILE_BASE      0
#define MAIN_PLX_TILE_BASE      1
#define MAIN_NEAR_MAP_BASE      14
#define MAIN_FAR_MAP_BASE       15

// Sub engine: BG0 = console text, BG1 = panels, BG2 = brick backdrop
// (priority 3), BG3 = drifting motes over it (priority 2).
#define SUB_TXT_MAP_BASE        31
#define SUB_TXT_TILE_BASE       0
#define SUB_PANEL_MAP_BASE      30
#define SUB_BACK_MAP_BASE       29
#define SUB_MOTES_MAP_BASE      28
#define SUB_PANEL_TILE_BASE     1

// First tile index for custom graphics in a layer that also holds the
// console font (font uses tiles 0-95).
#define CUSTOM_TILE_FIRST       128

#endif // DELVE_CONFIG_H
