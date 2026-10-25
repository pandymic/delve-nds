// 3D rendering of rooms with Nitro Engine (DS only).
//
// Vertices are emitted in half world units and scaled back up by the
// modelview matrix, because DS vertex coordinates are limited to +/-8.0.

#ifndef DELVE_RENDER_H
#define DELVE_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "camera.h"
#include "fx.h"
#include "room.h"

// Loads every texture, sprite sheet and palette into VRAM.
void render_init(void);

#ifdef DELVE_DEBUG
// Debug builds: scanlines spent in each part of render_room() last frame
// (lights, floor, decals, walls, obstacles), for the CPU meter.
extern int debug_room_sections[5];
#endif

// Sets the orthographic projection and the camera view. Call first inside
// the NE_Process draw callback.
void render_begin(const Camera *cam);

// What has happened to a room's props during a run (from run.c).
typedef struct {
    uint32_t broken;              // decor bits: broken pots and barrels
    const uint8_t *fuse;          // per decor: frames left on a lit fuse
    const bool (*crate_broken)[ROOM_SIZE];   // debris where crates were
    uint32_t room_frame;          // frames in the room (trap timing)
    const uint8_t *crust;         // per hazard: frames since the crust
                                  // cracked (0 = whole), or NULL
    int flood_ring;               // ring about to flood, or -1
} PropState;

// Draws floor, decals, debris, traps, walls and opaque obstacles. The
// player at (lx, lz) carries a light, and so do the room's torches and
// burning fire jets; frame drives the flicker. Obstacles whose alpha is
// below FADE_ALPHA_OPAQUE are skipped here.
void render_room(const Room *room, const RoomAlpha alpha, fx lx, fx lz,
                 bool doors_open, int frame, const PropState *ps);

// Billboard props: reward icons over exit doors, the room feature (chest /
// campfire), wall props, clutter (lit explosive barrels flash) and the
// upright parts of traps. Call with the other sprites.
void render_room_props(const Room *room, bool doors_open, bool chest_open,
                       const PropState *ps, int frame);

// Draws the faded obstacles. Call after every opaque thing (sprites
// included): the DS blends translucent polygons in submission order.
void render_room_faded(const Room *room, const RoomAlpha alpha);

// Character size as drawn, for occlusion tests: about 12 x 17 texels of a
// 24 x 24 cell.
#define SPRITE_HALF_W   FX(0.375)
#define SPRITE_HEIGHT   FX(1.3)

typedef enum {
    SHEET_PLAYER,
    SHEET_CHASER,
    SHEET_SHOOTER,
    SHEET_PROPS,
    SHEET_ITEMS,
    SHEET_FX,       // dagger, fireball, coin, particles, digits, blast ring
    SHEET_CLUTTER,  // bones, banners, debris, explosive barrel, saw, fire
    SHEET_BRUTE,
    SHEET_ARCHER,
    SHEET_CASTER,
    SHEET_SHAMAN,
    SHEET_SLIME,
    SHEET_DUMMY,
    SHEET_WORLD,    // the camp's trees and props
    SHEET_CAVE,     // crystals, shards, bats
    // Miniworld monsters: 16x16 cells (the dragon 32x32), three
    // directions.
    SHEET_IMP,
    SHEET_BULL,     // the magma brute
    SHEET_CRAB,     // the digger
    SHEET_YETI,     // the Caverns' boss
    SHEET_DRAGON,   // the Magma Lord
    SHEET_FIRE,     // torches, campfires, fire jets, flames (128x128)
    SHEET_COUNT
} SpriteSheet;

// Palette variants for sprites.
typedef enum {
    SPAL_NORMAL,
    SPAL_FLASH,   // solid white silhouette (hits, telegraphs)
    SPAL_ELITE,   // the sheet's elite recolour (the effects sheet: red)
    SPAL_MAGMA,   // the Molten Depths' recolour (the fire slime)
    SPAL_VARIANTS
} SpritePal;

// Any rectangle of a sheet as an upright billboard with its bottom edge at
// height y. All billboards use the world's texel size, 16 texels per tile
// (2 screen pixels per texel at the close zoom, 1 at the far zoom). Height
// is stretched by 1/cos(pitch) so pixels come out square on screen.
void render_billboard(SpriteSheet sheet, int u, int v, int w, int h,
                      bool mirror, fx x, fx y, fx z, SpritePal pal);

// One 16x16 cell of an 8-cell-wide sheet (props, clutter, fire; slot from
// art_layout.h) as a billboard.
void render_prop(SpriteSheet sheet, int slot, bool mirror, fx x, fx y, fx z,
                 SpritePal pal);

// A character frame: slot of a 24x24-cell sheet (art_layout.h), drawn so
// texel row feet_row of the cell touches the ground at (x, z).
void render_char(SpriteSheet sheet, int slot, bool mirror, int feet_row,
                 fx x, fx z, SpritePal pal);

// A character frame drawn scale times larger (bosses), lifted by lift.
void render_char_scaled(SpriteSheet sheet, int slot, bool mirror,
                        int feet_row, fx x, fx lift, fx z, SpritePal pal,
                        int scale);

// A frame of a sheet of cell x cell slots, 256 texels across (the
// Miniworld monsters), any size (scale: FX_ONE = the world's texel size).
void render_monster(SpriteSheet sheet, int cell, int slot, bool mirror,
                    int feet_row, fx x, fx lift, fx z, SpritePal pal,
                    fx scale);

// Flat shadow ellipse on the floor under an entity.
void render_shadow(fx x, fx z, fx scale);

// Code Genie's giant mode: render_char, render_char_alpha,
// render_monster and render_shadow draw at this scale (FX_ONE = normal).
void render_set_char_scale(fx scale);

// Translucent billboard (alpha 1-30) for afterimages. Each translucent
// polygon that may overlap another needs its own polygon id (1-62).
void render_billboard_alpha(SpriteSheet sheet, int u, int v, int w, int h,
                            bool mirror, fx x, fx y, fx z, SpritePal pal,
                            int alpha, int poly_id);
// A translucent white silhouette of a character frame (dash afterimages).
void render_char_alpha(SpriteSheet sheet, int slot, bool mirror, int feet_row,
                       fx x, fx z, int alpha, int poly_id);

// A sheet rectangle lying flat on the floor, centred on (x, z), half-size
// half (world units), alpha 1-31.
void render_ground(SpriteSheet sheet, int u, int v, int w, int h, fx x, fx z,
                   fx half, int alpha, int poly_id, SpritePal pal);

// Particles and damage numbers.
void render_fx(const Fx *f);

// Rocks, broken pillars and chains floating in the void around a room,
// each shifting with the camera by its own depth so they read as layers.
// render_void_init places them from the room seed; render_void draws them
// (call first, before the room).
void render_void_init(uint32_t seed);
void render_void(const Camera *cam, int frame);

#endif // DELVE_RENDER_H
