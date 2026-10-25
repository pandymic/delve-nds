// 3D drawing with Nitro Engine: rooms, lights, props, billboards, particles
// and the void around a room (render.h).

#include <NEMain.h>

#include "art_layout.h"
#include "render.h"
#include "rng.h"
#include "run.h"
#include "spr_archer.h"
#include "spr_brute.h"
#include "spr_caster.h"
#include "spr_clutter.h"
#include "spr_shaman.h"
#include "spr_slime.h"
#include "spr_dummy.h"
#include "spr_chaser.h"
#include "spr_fx.h"
#include "spr_items.h"
#include "spr_player.h"
#include "spr_props.h"
#include "spr_shooter.h"
#include "spr_world.h"
#include "spr_cave.h"
#include "spr_imp.h"
#include "spr_bull.h"
#include "spr_crab.h"
#include "spr_yeti.h"
#include "spr_dragon.h"
#include "spr_fire.h"
#include "tex_cave.h"
#include "tex_cavefloor.h"
#include "tex_basalt.h"
#include "tex_basaltcol.h"
#include "tex_basaltfloor.h"
#include "tex_lava.h"
#include "tex_murk.h"
#include "tex_cliff.h"
#include "tex_crate.h"
#include "tex_dirt.h"
#include "tex_grass.h"
#include "tex_floor.h"
#include "tex_floor2.h"
#include "tex_pillar.h"
#include "tex_wall.h"
#include "tex_wall2.h"
#include "tex_floor3.h"
#include "tex_walltop.h"

#define WALL_H      FX(1.5)   // back walls
#define LEDGE_H     FX(0.25)  // front walls, cut down so they never hide the room
#define WALL_T      FX(0.5)   // wall thickness

#define TEXELS_PER_TILE 16

#ifdef DELVE_DEBUG
int debug_room_sections[5];
#endif

// Depth nudges. In the orthographic view, moving a point straight towards
// the camera (camera.h CAM_DIR_*) keeps it on the same pixel and only
// brings it nearer, so these never shift anything on screen.
// - Sprites and particles: their lowest pixel rows touch the floor (walk
//   frames reach a row below the idle feet line), and a tie with the floor
//   in the depth test cut the feet off. 0.18 clears one texel row below
//   the floor (TEXEL_H / CAM_DIR_Y = 0.133) with room to spare.
// - Shadows and floor decals: a few depth steps above the floor tore into
//   stripes as they moved; this keeps them clearly in front.
#define SPRITE_BIAS     FX(0.18)
#define GROUND_BIAS     FX(0.05)
#define TOWARDS_CAMERA(x, y, z, bias) do {   \
        (x) += fx_mul(CAM_DIR_X, bias);      \
        (y) += fx_mul(CAM_DIR_Y, bias);      \
        (z) += fx_mul(CAM_DIR_Z, bias);      \
    } while (0)

// Player light: brightness falls off with squared distance.
#define LIGHT_RADIUS    7.0
#define LIGHT_INV_R2    FX(1.0 / (LIGHT_RADIUS * LIGHT_RADIUS))
// Torches: a smaller, flickering pool in front of their wall.
#define TORCH_RADIUS    3.5
#define TORCH_INV_R2    FX(1.0 / (TORCH_RADIUS * TORCH_RADIUS))
#define TORCH_POWER     FX(0.8)
#define TORCH_REACH     FX(0.6)   // light centre in front of the wall
// Burning fire jets.
#define FIRE_LIGHT_POWER FX(0.9)
#define FIRE_INV_R2      FX(1.0 / (3.0 * 3.0))
// A campfire (rest rooms, the camp) lights a wide circle.
#define CAMPFIRE_POWER   FX(0.95)
#define CAMPFIRE_INV_R2  FX(1.0 / (4.5 * 4.5))
// Wall crystals (the Caverns): a wider, softer glow than a torch.
#define CRYSTAL_POWER    FX(0.75)
#define CRYSTAL_INV_R2   FX(1.0 / (4.2 * 4.2))

// Floor is drawn as a grid of quads so vertex colours can carry the light.
#define FLOOR_STEP      2

typedef enum {
    MAT_FLOOR,
    MAT_FLOOR2,
    MAT_WALL,
    MAT_WALLTOP,
    MAT_PILLAR,
    MAT_CRATE,
    MAT_WALL2,      // the Sunken Sewers
    MAT_FLOOR3,
    MAT_GRASS,      // the camp
    MAT_DIRT,
    MAT_CLIFF,
    MAT_CAVE,       // the Caverns
    MAT_CAVEFLOOR,
    MAT_BASALT,     // the Molten Depths
    MAT_BASALTFLOOR,
    MAT_BASALTCOL,
    MAT_LAVA,       // liquids in pits
    MAT_MURK,
    MAT_COUNT
} MaterialId;

// How a place looks: its materials and its light. The light is tinted from
// `dark` (at the ambient level) to `lit` (in full light), RGB 0-31, so a
// night outdoors is blue in the shadows and warm by the fire.
typedef enum {
    LOOK_DUNGEON, LOOK_SEWERS, LOOK_CAVERNS, LOOK_MOLTEN, LOOK_CAMP,
    LOOK_COUNT
} LookId;

typedef struct {
    MaterialId floor;       // fights
    MaterialId floor_room;  // rest and treasure rooms
    MaterialId wall, walltop, cliff, pillar;
    uint8_t dark[3], lit[3];
    fx ambient;
    bool outdoor;           // no walls: forest behind, a cliff in front
    bool rough;             // raw rock: ragged wall heights, stalagmites
    int8_t liquid;          // MaterialId filling pits, or -1 for none
    bool glow;              // the liquid shines by itself (lava)
} Look;

static const Look looks[LOOK_COUNT] = {
    [LOOK_DUNGEON] = { MAT_FLOOR, MAT_FLOOR2, MAT_WALL, MAT_WALLTOP, MAT_WALL,
                       MAT_PILLAR, { 31, 28, 24 }, { 31, 28, 24 }, FX(0.32),
                       false, false, -1, false },
    [LOOK_SEWERS]  = { MAT_FLOOR3, MAT_FLOOR2, MAT_WALL2, MAT_WALLTOP,
                       MAT_WALL2, MAT_PILLAR, { 26, 30, 24 }, { 31, 29, 24 },
                       FX(0.32), false, false, MAT_MURK, false },
    // Crystal light: blue-black in the dark, cyan-white where it shines.
    [LOOK_CAVERNS] = { MAT_CAVEFLOOR, MAT_CAVEFLOOR, MAT_CAVE, MAT_CAVE,
                       MAT_CAVE, MAT_CAVE, { 6, 10, 20 }, { 22, 30, 31 },
                       FX(0.24), false, true, -1, false },
    // Lava glow: dark red in shadow, orange-white in full light.
    [LOOK_MOLTEN]  = { MAT_BASALTFLOOR, MAT_BASALTFLOOR, MAT_BASALT,
                       MAT_WALLTOP, MAT_BASALT, MAT_BASALTCOL, { 22, 8, 6 },
                       { 31, 24, 14 }, FX(0.36), false, false, MAT_LAVA, true },
    [LOOK_CAMP]    = { MAT_GRASS, MAT_GRASS, MAT_WALL, MAT_WALLTOP, MAT_CLIFF,
                       MAT_PILLAR, { 9, 12, 24 }, { 31, 25, 16 }, FX(0.30),
                       true, false, -1, false },
};

static const Look *look = &looks[LOOK_DUNGEON];
static fx ambient = FX(0.32);
static fx tint_span;    // 1 / (1 - ambient)

static LookId look_of(const Room *room)
{
    if (room->hub)
        return LOOK_CAMP;
    switch (room->area) {
    case AREA_SEWERS:  return LOOK_SEWERS;
    case AREA_CAVERNS: return LOOK_CAVERNS;
    case AREA_MOLTEN:  return LOOK_MOLTEN;
    default:           return LOOK_DUNGEON;
    }
}

static void set_look(const Room *room)
{
    look = &looks[look_of(room)];
    ambient = look->ambient;
    tint_span = fx_div(FX_ONE, FX_ONE - ambient);
}

static NE_Material *materials[MAT_COUNT];
static int material_h[MAT_COUNT];   // texture height in texels
static int tex_h;                   // height of the material in use
static NE_Material *sheets[SHEET_COUNT];
static NE_Palette *elite_pals[SHEET_COUNT];
static NE_Palette *variant_pals[SHEET_COUNT][SPAL_VARIANTS];   // area recolours
static NE_Palette *white_palette; // hit flash: every colour white
static u16 white_colors[16];

typedef struct {
    fx x, z, inv_r2, power;
} Light;

static Light lights[2 + MAX_TORCH_LIGHTS];   // + the player, a campfire
static int light_count;

static NE_Palette *load_palette(const void *colors)
{
    NE_Palette *p = NE_PaletteCreate();
    NE_PaletteLoad(p, colors, 16, NE_PAL16);
    return p;
}

// A 16-colour texture with its palette. Texture loading as in Nitro Engine
// examples/loading/paletted_texture.
static NE_Material *load_texture(const void *bitmap, const void *pal, int w,
                                 int h, int flags)
{
    NE_Material *m = NE_MaterialCreate();
    NE_MaterialTexLoad(m, NE_PAL16, w, h, NE_TEXGEN_TEXCOORD | flags, bitmap);
    NE_MaterialSetPalette(m, load_palette(pal));
    return m;
}

static void load_material(MaterialId id, const void *bitmap, const void *pal,
                          int w, int h)
{
    materials[id] = load_texture(bitmap, pal, w, h,
                                 NE_TEXTURE_WRAP_S | NE_TEXTURE_WRAP_T);
    material_h[id] = h;
}

static void load_sheet(SpriteSheet id, const void *bitmap, const void *pal,
                       int w, int h, const u16 *elite)
{
    sheets[id] = load_texture(bitmap, pal, w, h, NE_TEXTURE_COLOR0_TRANSPARENT);
    if (elite)
        elite_pals[id] = load_palette(elite);
}

void render_init(void)
{
    load_sheet(SHEET_PLAYER, spr_playerBitmap, spr_playerPal, 256, 256, NULL);
    load_sheet(SHEET_CHASER, spr_chaserBitmap, spr_chaserPal, 256, 256,
               spr_chaser_elite_pal);
    load_sheet(SHEET_SHOOTER, spr_shooterBitmap, spr_shooterPal, 256, 256,
               spr_shooter_elite_pal);
    load_sheet(SHEET_PROPS, spr_propsBitmap, spr_propsPal, 128,
               PROPS_SHEET_H, NULL);
    load_sheet(SHEET_ITEMS, spr_itemsBitmap, spr_itemsPal, 128, 32, NULL);
    // The effects sheet's "elite" palette: red and blue swapped, so the
    // blast ring turns into a red warning ring (caster marks).
    static u16 fx_red[16];
    for (int i = 0; i < 16; i++) {
        u16 c = spr_fxPal[i];
        fx_red[i] = 0 == i ? c : (u16)(((c >> 10) & 31) | (c & (31 << 5))
                                       | ((c & 31) << 10));
    }
    load_sheet(SHEET_FX, spr_fxBitmap, spr_fxPal, 128, 64, fx_red);
    load_sheet(SHEET_CLUTTER, spr_clutterBitmap, spr_clutterPal, 128, 64, NULL);
    load_sheet(SHEET_BRUTE, spr_bruteBitmap, spr_brutePal, 256, BF_SHEET_H,
               spr_brute_elite_pal);
    load_sheet(SHEET_ARCHER, spr_archerBitmap, spr_archerPal, 256, AF_SHEET_H,
               spr_archer_elite_pal);
    load_sheet(SHEET_CASTER, spr_casterBitmap, spr_casterPal, 256, KF_SHEET_H,
               spr_caster_elite_pal);
    load_sheet(SHEET_SHAMAN, spr_shamanBitmap, spr_shamanPal, 256, HF_SHEET_H,
               spr_shaman_elite_pal);
    load_sheet(SHEET_SLIME, spr_slimeBitmap, spr_slimePal, 256, MF_SHEET_H,
               spr_slime_elite_pal);
    load_sheet(SHEET_DUMMY, spr_dummyBitmap, spr_dummyPal, 256, DF_SHEET_H,
               NULL);
    load_sheet(SHEET_WORLD, spr_worldBitmap, spr_worldPal, 128, 64, NULL);
    load_sheet(SHEET_CAVE, spr_caveBitmap, spr_cavePal, 128, 32, NULL);
    load_sheet(SHEET_FIRE, spr_fireBitmap, spr_firePal, 128, FIRE_SHEET_H, NULL);
    // Miniworld monsters.
    load_sheet(SHEET_IMP, spr_impBitmap, spr_impPal, 256, IF_SHEET_H,
               spr_imp_elite_pal);
    load_sheet(SHEET_BULL, spr_bullBitmap, spr_bullPal, 256, UF_SHEET_H,
               spr_bull_elite_pal);
    load_sheet(SHEET_CRAB, spr_crabBitmap, spr_crabPal, 256, RF_SHEET_H,
               spr_crab_elite_pal);
    load_sheet(SHEET_YETI, spr_yetiBitmap, spr_yetiPal, 256, YF_SHEET_H, NULL);
    load_sheet(SHEET_DRAGON, spr_dragonBitmap, spr_dragonPal, 256, GF_SHEET_H,
               NULL);
    // Area recolours of shared sheets (art_palettes.c).
    static const struct {
        SpriteSheet sheet;
        SpritePal pal;
        const u16 *colors;
    } variants[] = {
        { SHEET_SLIME, SPAL_MAGMA, spr_slime_magma_pal },
    };
    for (int i = 0; i < ARRAY_LEN(variants); i++)
        variant_pals[variants[i].sheet][variants[i].pal] =
            load_palette(variants[i].colors);
    for (int i = 0; i < 16; i++)
        white_colors[i] = RGB15(31, 31, 31);
    white_palette = load_palette(white_colors);

    load_material(MAT_FLOOR, tex_floorBitmap, tex_floorPal, 64, 64);
    load_material(MAT_FLOOR2, tex_floor2Bitmap, tex_floor2Pal, 64, 64);
    load_material(MAT_WALL, tex_wallBitmap, tex_wallPal, 32, 32);
    load_material(MAT_WALLTOP, tex_walltopBitmap, tex_walltopPal, 16, 16);
    load_material(MAT_PILLAR, tex_pillarBitmap, tex_pillarPal, 16, 32);
    load_material(MAT_CRATE, tex_crateBitmap, tex_cratePal, 16, 16);
    load_material(MAT_WALL2, tex_wall2Bitmap, tex_wall2Pal, 32, 32);
    load_material(MAT_FLOOR3, tex_floor3Bitmap, tex_floor3Pal, 64, 64);
    load_material(MAT_GRASS, tex_grassBitmap, tex_grassPal, 64, 64);
    load_material(MAT_DIRT, tex_dirtBitmap, tex_dirtPal, 64, 64);
    load_material(MAT_CLIFF, tex_cliffBitmap, tex_cliffPal, 32, 32);
    load_material(MAT_CAVE, tex_caveBitmap, tex_cavePal, 32, 32);
    load_material(MAT_CAVEFLOOR, tex_cavefloorBitmap, tex_cavefloorPal, 64, 64);
    load_material(MAT_BASALT, tex_basaltBitmap, tex_basaltPal, 32, 32);
    load_material(MAT_BASALTFLOOR, tex_basaltfloorBitmap, tex_basaltfloorPal,
                  64, 64);
    load_material(MAT_BASALTCOL, tex_basaltcolBitmap, tex_basaltcolPal, 16, 32);
    load_material(MAT_LAVA, tex_lavaBitmap, tex_lavaPal, 64, 64);
    load_material(MAT_MURK, tex_murkBitmap, tex_murkPal, 64, 64);
}

void render_begin(const Camera *cam)
{
    fx hw = cam->view_w / 2;
    fx hh = camera_view_h(cam) / 2;

    MATRIX_CONTROL = GL_PROJECTION;
    MATRIX_IDENTITY = 0;
    glOrthof32(-hw, hw, -hh, hh, FX(1.0), FX(64.0));

    MATRIX_CONTROL = GL_MODELVIEW;
    MATRIX_IDENTITY = 0;
    gluLookAtf32(cam->x + fx_mul(CAM_DIR_X, CAM_DISTANCE),
                 fx_mul(CAM_DIR_Y, CAM_DISTANCE),
                 cam->z + fx_mul(CAM_DIR_Z, CAM_DISTANCE),
                 cam->x, 0, cam->z,
                 0, FX_ONE, 0);
    glScalef32(FX(2.0), FX(2.0), FX(2.0));

    NE_PolyFormat(31, 0, 0, NE_CULL_BACK, 0);
}

// Primitives

static inline void vtx(fx x, fx y, fx z)
{
    NE_PolyVertexI(x >> 1, y >> 1, z >> 1);
}

static void use_material(MaterialId id)
{
    NE_MaterialUse(materials[id]);
    tex_h = material_h[id];
}

// Light at whole-tile grid points, filled once per frame by set_lights:
// most vertices sit on the grid, and this saves recomputing every light
// for each of them.
#define LIGHT_GRID (ROOM_SIZE + 1)
static fx light_grid[LIGHT_GRID][LIGHT_GRID];

// Light level at a ground point, in [LIGHT_AMBIENT, 1]: the brightest of
// the lights reaching it.
static fx light_compute(fx x, fx z)
{
    fx best = 0;
    for (int i = 0; i < light_count; i++) {
        const Light *l = &lights[i];
        fx dx = x - l->x, dz = z - l->z;
        fx d2 = fx_mul(dx, dx) + fx_mul(dz, dz);
        fx k = FX_ONE - fx_mul(d2, l->inv_r2);
        if (k <= 0)
            continue;
        k = fx_mul(fx_mul(k, k), l->power);
        if (k > best)
            best = k;
    }
    return ambient + fx_mul(FX_ONE - ambient, best);
}

// Room geometry takes its light from the nearest grid point: light varies
// smoothly, so wall tops and ledges at half-tile offsets look the same, and
// no vertex ever computes every light again.
static fx light_at(fx x, fx z)
{
    int gx = iclamp(FX_TO_INT(x + FX_HALF), 0, LIGHT_GRID - 1);
    int gz = iclamp(FX_TO_INT(z + FX_HALF), 0, LIGHT_GRID - 1);
    return light_grid[gz][gx];
}

// Torch light at each grid point (before ambient), computed once per room:
// torches never move. Each frame only a shared flicker scales it.
static fx torch_grid[LIGHT_GRID][LIGHT_GRID];
static uint32_t torch_grid_seed;
static const Look *torch_grid_look;
static bool torch_grid_valid;

static void build_torch_grid(const Room *room)
{
    light_count = 0;
    if (room->feature_x >= 0
        && CELL_FIRE == room_cell(room, room->feature_x, room->feature_z))
        lights[light_count++] = (Light){ FX_INT(room->feature_x) + FX_HALF,
                                         FX_INT(room->feature_z) + FX_HALF,
                                         CAMPFIRE_INV_R2, CAMPFIRE_POWER };
    for (int i = 0; i < room->decor_count && light_count < MAX_TORCH_LIGHTS + 1;
         i++) {
        const Decor *d = &room->decor[i];
        if (DECOR_TORCH != d->kind && DECOR_CRYSTAL != d->kind)
            continue;
        fx nx, nz;
        side_inward(d->wall, &nx, &nz);
        bool crystal = DECOR_CRYSTAL == d->kind;
        lights[light_count++] = (Light){ d->x + fx_mul(nx, TORCH_REACH),
                                         d->z + fx_mul(nz, TORCH_REACH),
                                         crystal ? CRYSTAL_INV_R2 : TORCH_INV_R2,
                                         crystal ? CRYSTAL_POWER : TORCH_POWER };
    }
    for (int gz = 0; gz < LIGHT_GRID; gz++)
        for (int gx = 0; gx < LIGHT_GRID; gx++)
            torch_grid[gz][gx] = light_compute(FX_INT(gx), FX_INT(gz))
                               - ambient;
    torch_grid_seed = room->seed;
    torch_grid_look = look;
    torch_grid_valid = true;
}

// Player light, the room's torches (flickering together) and any burning
// fire jets, into light_grid.
static void set_lights(const Room *room, fx px, fx pz, int frame,
                       uint32_t room_frame)
{
    if (!torch_grid_valid || torch_grid_seed != room->seed
        || torch_grid_look != look)
        build_torch_grid(room);

    // The player and burning fire jets are the moving lights.
    lights[0] = (Light){ px, pz, LIGHT_INV_R2, FX_ONE };
    light_count = 1;
    for (int i = 0; i < room->hazard_count && light_count < 3; i++) {
        const Hazard *h = &room->hazards[i];
        if (HAZ_FIRE == h->kind
            && TRAP_ACTIVE == hazard_state(h, room_frame, NULL))
            lights[light_count++] = (Light){ h->x, h->z, FIRE_INV_R2,
                                             FIRE_LIGHT_POWER };
    }
    static const int8_t flicker[8] = { 0, 3, 1, 4, 2, 0, 5, 1 };
    fx flick = FX_ONE - flicker[(frame >> 2) & 7] * FX(0.025);
    for (int gz = 0; gz < LIGHT_GRID; gz++) {
        for (int gx = 0; gx < LIGHT_GRID; gx++) {
            fx torch = ambient + fx_mul(torch_grid[gz][gx], flick);
            fx moving = light_compute(FX_INT(gx), FX_INT(gz));
            light_grid[gz][gx] = fx_max(torch, moving);
        }
    }
}

// The look's tint for the light level, scaled by brightness (light *
// shade).
static void color(fx light, fx shade)
{
    fx b = fx_mul(light, shade);
    fx t = fx_clamp(fx_mul(light - ambient, tint_span), 0, FX_ONE);
    int c[3];
    for (int i = 0; i < 3; i++) {
        int tint = look->dark[i]
                 + (((look->lit[i] - look->dark[i]) * t) >> FX_SHIFT);
        c[i] = imin((tint * b) >> FX_SHIFT, 31);
    }
    NE_PolyColor(RGB15(c[0], c[1], c[2]));
}

// Face shades: tops brightest, +x faces (screen left) lit, +z faces dimmer.
#define SHADE_TOP   FX(1.0)
#define SHADE_PX    FX(0.85)
#define SHADE_PZ    FX(0.62)
#define SHADE_WALLTOP FX(0.45)

// Horizontal quad at height y over [x0,x1] x [z0,z1], facing +y.
static void quad_top(fx x0, fx z0, fx x1, fx z1, fx y, fx shade, int tex_scale)
{
    int u0 = FX_TO_INT(x0 * tex_scale), u1 = FX_TO_INT(x1 * tex_scale);
    int v0 = FX_TO_INT(z0 * tex_scale), v1 = FX_TO_INT(z1 * tex_scale);
    color(light_at(x0, z1), shade); NE_PolyTexCoord(u0, v1); vtx(x0, y, z1);
    color(light_at(x1, z1), shade); NE_PolyTexCoord(u1, v1); vtx(x1, y, z1);
    color(light_at(x1, z0), shade); NE_PolyTexCoord(u1, v0); vtx(x1, y, z0);
    color(light_at(x0, z0), shade); NE_PolyTexCoord(u0, v0); vtx(x0, y, z0);
}

// Texture v for height y on a vertical face: the base of the face meets
// the bottom row of the texture, so walls and pillars stand upright.
static inline int face_v(fx y)
{
    return tex_h - FX_TO_INT(y * TEXELS_PER_TILE);
}

// Vertical quad in the plane z = z, facing +z, spanning [x0,x1] x [y0,y1].
static void quad_pz(fx x0, fx x1, fx z, fx y0, fx y1, fx shade)
{
    const int t = TEXELS_PER_TILE;
    int u0 = FX_TO_INT(x0 * t), u1 = FX_TO_INT(x1 * t);
    int vb = face_v(y0), vt = face_v(y1);
    fx l0 = light_at(x0, z), l1 = light_at(x1, z);
    // Top edge a little darker than the base: light pools on the floor.
    color(l0, shade); NE_PolyTexCoord(u0, vb); vtx(x0, y0, z);
    color(l1, shade); NE_PolyTexCoord(u1, vb); vtx(x1, y0, z);
    color(l1, fx_mul(shade, FX(0.7))); NE_PolyTexCoord(u1, vt); vtx(x1, y1, z);
    color(l0, fx_mul(shade, FX(0.7))); NE_PolyTexCoord(u0, vt); vtx(x0, y1, z);
}

// Vertical quad in the plane x = x, facing +x, spanning [z0,z1] x [y0,y1].
static void quad_px(fx x, fx z0, fx z1, fx y0, fx y1, fx shade)
{
    const int t = TEXELS_PER_TILE;
    int u0 = FX_TO_INT(z0 * t), u1 = FX_TO_INT(z1 * t);
    int vb = face_v(y0), vt = face_v(y1);
    fx l0 = light_at(x, z0), l1 = light_at(x, z1);
    color(l1, shade); NE_PolyTexCoord(u1, vb); vtx(x, y0, z1);
    color(l0, shade); NE_PolyTexCoord(u0, vb); vtx(x, y0, z0);
    color(l0, fx_mul(shade, FX(0.7))); NE_PolyTexCoord(u0, vt); vtx(x, y1, z0);
    color(l1, fx_mul(shade, FX(0.7))); NE_PolyTexCoord(u1, vt); vtx(x, y1, z1);
}

// Box over [x0,x1] x [z0,z1], height h. Only camera-facing faces.
static void box(fx x0, fx z0, fx x1, fx z1, fx h)
{
    quad_top(x0, z0, x1, z1, h, SHADE_TOP, TEXELS_PER_TILE);
    quad_px(x1, z0, z1, 0, h, SHADE_PX);
    quad_pz(x0, x1, z1, 0, h, SHADE_PZ);
}

// Room

// Cells with ground: everything except the void outside the room's shape
// and pits. (Obstacles and features stand on floor.)
static bool has_ground(const Room *room, int x, int z)
{
    if (!room_in_bounds(x, z))
        return false;
    int t = room->cells[z][x];
    return CELL_VOID != t && CELL_PIT != t;
}

static bool is_dirt(const Room *room, int x, int z)
{
    return (room->dirt[z] >> x) & 1;
}

// One floor material: cells with ground whose dirt bit is `dirt`.
static void draw_floor_pass(const Room *room, bool dirt)
{
    NE_PolyBegin(GL_QUADS);
    // Whole 2x2 blocks where possible, single cells around holes.
    for (int z = 0; z < ROOM_SIZE; z += FLOOR_STEP) {
        for (int x = 0; x < ROOM_SIZE; x += FLOOR_STEP) {
            bool full = true;
            for (int dz = 0; dz < FLOOR_STEP; dz++)
                for (int dx = 0; dx < FLOOR_STEP; dx++)
                    full &= has_ground(room, x + dx, z + dz)
                         && is_dirt(room, x + dx, z + dz) == dirt;
            if (full) {
                quad_top(FX_INT(x), FX_INT(z), FX_INT(x + FLOOR_STEP),
                         FX_INT(z + FLOOR_STEP), 0, FX_ONE, TEXELS_PER_TILE);
                continue;
            }
            for (int dz = 0; dz < FLOOR_STEP; dz++)
                for (int dx = 0; dx < FLOOR_STEP; dx++)
                    if (has_ground(room, x + dx, z + dz)
                        && is_dirt(room, x + dx, z + dz) == dirt)
                        quad_top(FX_INT(x + dx), FX_INT(z + dz),
                                 FX_INT(x + dx + 1), FX_INT(z + dz + 1), 0,
                                 FX_ONE, TEXELS_PER_TILE);
        }
    }
    NE_PolyEnd();
}

static void draw_floor(const Room *room)
{
    // Fights get the look's floor, rest and treasure rooms the patterned
    // tiles; the camp is grass with a dirt path.
    bool plain = RT_COMBAT == room->type || RT_BOSS == room->type;
    use_material(plain || look->outdoor ? look->floor : look->floor_room);
    draw_floor_pass(room, false);
    if (look->outdoor) {
        use_material(MAT_DIRT);
        draw_floor_pass(room, true);
    }
}

// Rooms are slabs floating in the void: wherever ground ends on a side the
// camera can see (+x or +z), a cliff face drops away into darkness. The
// same faces line the far sides of pits.
#define CLIFF_DEPTH FX(2.0)
#define RAIL_H      FX(0.3)   // low parapet along the outline
#define RAIL_T      FX(0.25)

static void cliff_px(fx x, fx z0, fx z1)
{
    const int t = TEXELS_PER_TILE;
    int u0 = FX_TO_INT(z0 * t), u1 = FX_TO_INT(z1 * t);
    int vt = face_v(0), vb = face_v(-CLIFF_DEPTH);
    fx l0 = light_at(x, z0), l1 = light_at(x, z1);
    color(l1, FX(0.5)); NE_PolyTexCoord(u1, vt); vtx(x, 0, z1);
    color(0, 0);        NE_PolyTexCoord(u1, vb); vtx(x, -CLIFF_DEPTH, z1);
    color(0, 0);        NE_PolyTexCoord(u0, vb); vtx(x, -CLIFF_DEPTH, z0);
    color(l0, FX(0.5)); NE_PolyTexCoord(u0, vt); vtx(x, 0, z0);
}

static void cliff_pz(fx x0, fx x1, fx z)
{
    const int t = TEXELS_PER_TILE;
    int u0 = FX_TO_INT(x0 * t), u1 = FX_TO_INT(x1 * t);
    int vt = face_v(0), vb = face_v(-CLIFF_DEPTH);
    fx l0 = light_at(x0, z), l1 = light_at(x1, z);
    color(l0, FX(0.4)); NE_PolyTexCoord(u0, vt); vtx(x0, 0, z);
    color(0, 0);        NE_PolyTexCoord(u0, vb); vtx(x0, -CLIFF_DEPTH, z);
    color(0, 0);        NE_PolyTexCoord(u1, vb); vtx(x1, -CLIFF_DEPTH, z);
    color(l1, FX(0.4)); NE_PolyTexCoord(u1, vt); vtx(x1, 0, z);
}

// True for an in-room cell of the given type (false outside the grid).
static bool cell_is(const Room *room, int x, int z, CellType t)
{
    return room_in_bounds(x, z) && room->cells[z][x] == t;
}

// Edges of the room's shape inside the 12x12 square (not the outer walls,
// which draw_back_walls and draw_front_ledges handle): low rails where the
// floor meets the void, cliffs on the camera side of voids and pits.
static void draw_outline(const Room *room)
{
    use_material(look->wall);
    NE_PolyBegin(GL_QUADS);
    for (int z = 0; z < ROOM_SIZE; z++) {
        for (int x = 0; x < ROOM_SIZE; x++) {
            if (!has_ground(room, x, z))
                continue;
            fx x0 = FX_INT(x), x1 = FX_INT(x + 1);
            fx z0 = FX_INT(z), z1 = FX_INT(z + 1);
            bool void_nx = cell_is(room, x - 1, z, CELL_VOID);
            bool void_nz = cell_is(room, x, z - 1, CELL_VOID);
            bool void_px = cell_is(room, x + 1, z, CELL_VOID);
            bool void_pz = cell_is(room, x, z + 1, CELL_VOID);
            // Back edges: a rail facing the camera.
            if (void_nx)
                quad_px(x0, z0, z1, 0, RAIL_H, SHADE_PX);
            if (void_nz)
                quad_pz(x0, x1, z0, 0, RAIL_H, SHADE_PZ);
            // Front edges: a rail on the void side, its face and a cliff.
            if (void_px) {
                quad_px(x1 + RAIL_T, z0, z1, 0, RAIL_H, SHADE_PX);
                cliff_px(x1 + RAIL_T, z0, z1);
            } else if (cell_is(room, x + 1, z, CELL_PIT)) {
                cliff_px(x1, z0, z1);
            }
            if (void_pz) {
                quad_pz(x0, x1, z1 + RAIL_T, 0, RAIL_H, SHADE_PZ);
                cliff_pz(x0, x1, z1 + RAIL_T);
            } else if (cell_is(room, x, z + 1, CELL_PIT)) {
                cliff_pz(x0, x1, z1);
            }
        }
    }
    NE_PolyEnd();

    // Rail tops.
    use_material(look->walltop);
    NE_PolyBegin(GL_QUADS);
    for (int z = 0; z < ROOM_SIZE; z++) {
        for (int x = 0; x < ROOM_SIZE; x++) {
            if (!has_ground(room, x, z))
                continue;
            fx x0 = FX_INT(x), x1 = FX_INT(x + 1);
            fx z0 = FX_INT(z), z1 = FX_INT(z + 1);
            if (cell_is(room, x - 1, z, CELL_VOID))
                quad_top(x0 - RAIL_T, z0, x0, z1, RAIL_H, SHADE_WALLTOP,
                         TEXELS_PER_TILE);
            if (cell_is(room, x, z - 1, CELL_VOID))
                quad_top(x0, z0 - RAIL_T, x1, z0, RAIL_H, SHADE_WALLTOP,
                         TEXELS_PER_TILE);
            if (cell_is(room, x + 1, z, CELL_VOID))
                quad_top(x1, z0, x1 + RAIL_T, z1, RAIL_H, SHADE_WALLTOP,
                         TEXELS_PER_TILE);
            if (cell_is(room, x, z + 1, CELL_VOID))
                quad_top(x0, z1, x1, z1 + RAIL_T, RAIL_H, SHADE_WALLTOP,
                         TEXELS_PER_TILE);
        }
    }
    NE_PolyEnd();
}

// Props sheet cell of a prop slot (art_layout.h).
static inline int prop_u(int slot) { return (slot % 8) * 16; }
static inline int prop_v(int slot) { return (slot / 8) * 16; }

static void flat_top(fx x0, fx z0, fx x1, fx z1, fx y, u32 c);

// A one-tile sheet cell lying flat on the floor, lit like the floor. The
// caller has bound the sheet and begun quads.
static void floor_decal(int slot, fx x, fx z)
{
    int u = prop_u(slot), v = prop_v(slot);
    fx x0 = x - FX_HALF, z0 = z - FX_HALF;
    fx x1 = x + FX_HALF, z1 = z + FX_HALF;
    fx bx = 0, by = FX(0.005), bz = 0;
    TOWARDS_CAMERA(bx, by, bz, GROUND_BIAS);
    color(light_at(x0, z1), FX_ONE); NE_PolyTexCoord(u, v + 16); vtx(x0 + bx, by, z1 + bz);
    color(light_at(x1, z1), FX_ONE); NE_PolyTexCoord(u + 16, v + 16); vtx(x1 + bx, by, z1 + bz);
    color(light_at(x1, z0), FX_ONE); NE_PolyTexCoord(u + 16, v); vtx(x1 + bx, by, z0 + bz);
    color(light_at(x0, z0), FX_ONE); NE_PolyTexCoord(u, v); vtx(x0 + bx, by, z0 + bz);
}

// Which sheet and cell a floor decal uses, or -1 for none.
static int decal_slot(const Decor *d, bool broken, SpriteSheet *sheet)
{
    *sheet = SHEET_CLUTTER;
    if (broken) {
        switch (d->kind) {
        case DECOR_POT:       return CLUT_POT_BROKEN;
        case DECOR_BARREL:    return CLUT_CRATE_DEBRIS;
        case DECOR_EXPLOSIVE: return CLUT_RUBBLE;
        default:              return -1;
        }
    }
    switch (d->kind) {
    case DECOR_GRATE:  *sheet = SHEET_PROPS; return PROP_GRATE;
    case DECOR_PLATE:  *sheet = SHEET_PROPS; return PROP_SWITCH;
    case DECOR_BONES:  return CLUT_BONES;
    case DECOR_CRACK:  return CLUT_CRACK;
    case DECOR_RUBBLE: return CLUT_RUBBLE;
    default:           return -1;
    }
}

// Spike frames: retracted, rising during the warning, fully out.
static int spike_frame(const Hazard *h, uint32_t room_frame)
{
    int t;
    switch (hazard_state(h, room_frame, &t)) {
    case TRAP_WARN:   return 1 + t * 3 / (SPIKE_UP - SPIKE_WARN);
    case TRAP_ACTIVE: return t < 4 ? 4 : PROP_SPIKES_FRAMES - 1;
    default:          return 0;
    }
}

// Starts a batch of floor quads (polygon id 3) with material m, the first
// time something needs one; *open tracks it so the caller can end it.
static void begin_floor_batch(NE_Material *m, bool *open)
{
    if (*open)
        return;
    NE_MaterialUse(m);
    NE_PolyFormat(31, 3, 0, NE_CULL_BACK, 0);
    NE_PolyBegin(GL_QUADS);
    *open = true;
}

// Decals, debris and the floor parts of traps, in two passes (one per
// sheet) to keep material switches down.
static void draw_decals(const Room *room, const PropState *ps)
{
    for (int pass = 0; pass < 2; pass++) {
        SpriteSheet want = pass ? SHEET_CLUTTER : SHEET_PROPS;
        bool any = false;
        for (int i = 0; i < room->decor_count; i++) {
            const Decor *d = &room->decor[i];
            SpriteSheet sheet;
            int slot = decal_slot(d, (ps->broken >> i) & 1, &sheet);
            if (slot < 0 || sheet != want)
                continue;
            begin_floor_batch(sheets[want], &any);
            floor_decal(slot, d->x, d->z);
        }
        if (1 == pass && ps->crate_broken) {
            for (int z = 0; z < ROOM_SIZE; z++) {
                for (int x = 0; x < ROOM_SIZE; x++) {
                    if (!ps->crate_broken[z][x])
                        continue;
                    begin_floor_batch(sheets[want], &any);
                    floor_decal(CLUT_CRATE_DEBRIS, FX_INT(x) + FX_HALF,
                                FX_INT(z) + FX_HALF);
                }
            }
        }
        // Where rock falls, rubble lies.
        for (int i = 0; 1 == pass && i < room->hazard_count; i++) {
            const Hazard *h = &room->hazards[i];
            if (HAZ_ROCKFALL != h->kind && HAZ_CRUST != h->kind)
                continue;
            if (HAZ_CRUST == h->kind && ps->crust && ps->crust[i] >= CRUST_CRACK)
                continue;   // open
            begin_floor_batch(sheets[want], &any);
            floor_decal(HAZ_CRUST == h->kind ? CLUT_CRACK : CLUT_RUBBLE, h->x,
                        h->z);
        }
        // Traps lying on the floor: spike plates and fire vents.
        for (int i = 0; 0 == pass && i < room->hazard_count; i++) {
            const Hazard *h = &room->hazards[i];
            int slot;
            if (HAZ_SPIKES == h->kind)
                slot = PROP_SPIKES + spike_frame(h, ps->room_frame);
            else if (HAZ_FIRE == h->kind)
                slot = PROP_GRATE;
            else
                continue;
            begin_floor_batch(sheets[want], &any);
            floor_decal(slot, h->x, h->z);
        }
        if (any)
            NE_PolyEnd();
    }
    // Saw tracks: a dark groove along the floor.
    bool any = false;
    for (int i = 0; i < room->hazard_count; i++) {
        const Hazard *h = &room->hazards[i];
        if (HAZ_SAW != h->kind)
            continue;
        begin_floor_batch(NULL, &any);
        // Narrow across the track, running a little past both ends.
        const fx w = FX(0.08), y = FX(0.006);
        fx x0 = fx_min(h->x, h->x2) - (h->x == h->x2 ? w : FX(0.3));
        fx x1 = fx_max(h->x, h->x2) + (h->x == h->x2 ? w : FX(0.3));
        fx z0 = fx_min(h->z, h->z2) - (h->z == h->z2 ? w : FX(0.3));
        fx z1 = fx_max(h->z, h->z2) + (h->z == h->z2 ? w : FX(0.3));
        flat_top(x0, z0, x1, z1, y, RGB15(4, 3, 4));
    }
    if (any)
        NE_PolyEnd();
    NE_PolyFormat(31, 0, 0, NE_CULL_BACK, 0);
}

// Doorways in the back walls.
#define DOOR_H      FX(1.1)

// Colour per reward type, used for portals and thresholds.
static u32 reward_color(RoomType t)
{
    switch (t) {
    case RT_REST:     return RGB15(28, 8, 10);
    case RT_TREASURE: return RGB15(31, 24, 6);
    default:          return RGB15(12, 18, 31);
    }
}

// Untextured gradient quad in the plane z = z (facing +z) or x = x (facing
// +x), from colour c0 at y0 to c1 at y1.
static void flat_pz(fx x0, fx x1, fx z, fx y0, fx y1, u32 c0, u32 c1)
{
    NE_PolyColor(c0); vtx(x0, y0, z);
    NE_PolyColor(c0); vtx(x1, y0, z);
    NE_PolyColor(c1); vtx(x1, y1, z);
    NE_PolyColor(c1); vtx(x0, y1, z);
}

static void flat_px(fx x, fx z0, fx z1, fx y0, fx y1, u32 c0, u32 c1)
{
    NE_PolyColor(c0); vtx(x, y0, z1);
    NE_PolyColor(c0); vtx(x, y0, z0);
    NE_PolyColor(c1); vtx(x, y1, z0);
    NE_PolyColor(c1); vtx(x, y1, z1);
}

static void flat_top(fx x0, fx z0, fx x1, fx z1, fx y, u32 c)
{
    NE_PolyColor(c); vtx(x0, y, z1);
    NE_PolyColor(c); vtx(x1, y, z1);
    NE_PolyColor(c); vtx(x1, y, z0);
    NE_PolyColor(c); vtx(x0, y, z0);
}

typedef enum {
    SEG_WALL,        // plain wall
    SEG_DOOR_SHUT,   // entry door, or an exit before the room is cleared
    SEG_DOOR_OPEN,   // exit, open
} SegmentKind;

static SegmentKind segment_kind(const Room *room, WallSide side, int pos,
                                bool doors_open)
{
    if (room_door_at(room, side, pos) >= 0)
        return doors_open ? SEG_DOOR_OPEN : SEG_DOOR_SHUT;
    if (room->entry_side == side && room->entry_pos == pos)
        return SEG_DOOR_SHUT;
    return SEG_WALL;
}

// Height of back-wall segment i on side (SIDE_NX / SIDE_NZ): even in
// built rooms, ragged in raw rock.
static fx wall_height(const Room *room, WallSide side, int i)
{
    if (!look->rough)
        return WALL_H;
    static const fx steps[5] = { FX(-0.2), 0, FX(0.15), FX(0.3), FX(0.45) };
    return WALL_H + steps[rng_hash3(room->seed, (uint32_t)side,
                                    (uint32_t)i) % 5];
}

// Back walls: brick faces and lintels, then the stone caps, then wooden
// panels in shut doorways.
static void draw_back_walls(const Room *room, bool doors_open)
{
    use_material(look->wall);
    NE_PolyBegin(GL_QUADS);
    for (int i = 0; i < ROOM_SIZE; i++) {
        fx a = FX_INT(i), b = FX_INT(i + 1);
        // z = 0 wall (upper right), faces +z, where the room reaches it.
        if (has_ground(room, i, 0)) {
            fx h = wall_height(room, SIDE_NZ, i);
            fx base = SEG_WALL == segment_kind(room, SIDE_NZ, i, doors_open)
                    ? 0 : DOOR_H;
            quad_pz(a, b, 0, base, h, SHADE_PZ);
            // Where this wall stops, its end shows (facing +x); where the
            // next segment is lower, the step does.
            if (!has_ground(room, i + 1, 0)) {
                quad_px(b, -WALL_T, 0, 0, h, SHADE_PX);
            } else {
                fx next = wall_height(room, SIDE_NZ, i + 1);
                if (next < h)
                    quad_px(b, -WALL_T, 0, next, h, SHADE_PX);
            }
        }
        // x = 0 wall (upper left), faces +x.
        if (has_ground(room, 0, i)) {
            fx h = wall_height(room, SIDE_NX, i);
            fx base = SEG_WALL == segment_kind(room, SIDE_NX, i, doors_open)
                    ? 0 : DOOR_H;
            quad_px(0, a, b, base, h, SHADE_PX);
            if (!has_ground(room, 0, i + 1)) {
                quad_pz(-WALL_T, 0, b, 0, h, SHADE_PZ);
            } else {
                fx next = wall_height(room, SIDE_NX, i + 1);
                if (next < h)
                    quad_pz(-WALL_T, 0, b, next, h, SHADE_PZ);
            }
        }
    }
    NE_PolyEnd();

    use_material(look->walltop);
    NE_PolyBegin(GL_QUADS);
    for (int i = 0; i < ROOM_SIZE; i++) {
        fx a = FX_INT(i), b = FX_INT(i + 1);
        if (has_ground(room, i, 0))
            quad_top(a, -WALL_T, b, 0, wall_height(room, SIDE_NZ, i),
                     SHADE_WALLTOP, TEXELS_PER_TILE);
        if (has_ground(room, 0, i))
            quad_top(-WALL_T, a, 0, b, wall_height(room, SIDE_NX, i),
                     SHADE_WALLTOP, TEXELS_PER_TILE);
    }
    if (has_ground(room, 0, 0))
        quad_top(-WALL_T, -WALL_T, 0, 0, wall_height(room, SIDE_NZ, 0),
                 SHADE_WALLTOP, TEXELS_PER_TILE);
    NE_PolyEnd();

    // Shut doors: wooden panels in the doorway.
    use_material(MAT_CRATE);
    NE_PolyBegin(GL_QUADS);
    for (int i = 0; i < ROOM_SIZE; i++) {
        fx a = FX_INT(i), b = FX_INT(i + 1);
        if (SEG_DOOR_SHUT == segment_kind(room, SIDE_NZ, i, doors_open))
            quad_pz(a, b, 0, 0, DOOR_H, SHADE_PZ);
        if (SEG_DOOR_SHUT == segment_kind(room, SIDE_NX, i, doors_open))
            quad_px(0, a, b, 0, DOOR_H, SHADE_PX);
    }
    NE_PolyEnd();
}

// Front ledges: closed segments are ledges, open exits are gaps. Below
// them the slab's edge drops away as a cliff.
static void draw_front_ledges(const Room *room, bool doors_open)
{
    const fx s = FX_INT(ROOM_SIZE);
    use_material(look->wall);
    NE_PolyBegin(GL_QUADS);
    for (int i = 0; i < ROOM_SIZE; i++) {
        fx a = FX_INT(i), b = FX_INT(i + 1);
        if (has_ground(room, i, ROOM_SIZE - 1)) {
            if (SEG_DOOR_OPEN != segment_kind(room, SIDE_PZ, i, doors_open))
                quad_pz(a, b, s + WALL_T, 0, LEDGE_H, SHADE_PZ);
            cliff_pz(a, b, s + WALL_T);
        }
        if (has_ground(room, ROOM_SIZE - 1, i)) {
            if (SEG_DOOR_OPEN != segment_kind(room, SIDE_PX, i, doors_open))
                quad_px(s + WALL_T, a, b, 0, LEDGE_H, SHADE_PX);
            cliff_px(s + WALL_T, a, b);
        }
    }
    if (has_ground(room, ROOM_SIZE - 1, ROOM_SIZE - 1)) {
        quad_pz(s, s + WALL_T, s + WALL_T, 0, LEDGE_H, SHADE_PZ);
        quad_px(s + WALL_T, s, s + WALL_T, 0, LEDGE_H, SHADE_PX);
        cliff_pz(s, s + WALL_T, s + WALL_T);
        cliff_px(s + WALL_T, s, s + WALL_T);
    }
    NE_PolyEnd();

    use_material(look->walltop);
    NE_PolyBegin(GL_QUADS);
    for (int i = 0; i < ROOM_SIZE; i++) {
        fx a = FX_INT(i), b = FX_INT(i + 1);
        if (has_ground(room, i, ROOM_SIZE - 1)
            && SEG_DOOR_OPEN != segment_kind(room, SIDE_PZ, i, doors_open))
            quad_top(a, s, b, s + WALL_T, LEDGE_H, SHADE_WALLTOP, TEXELS_PER_TILE);
        if (has_ground(room, ROOM_SIZE - 1, i)
            && SEG_DOOR_OPEN != segment_kind(room, SIDE_PX, i, doors_open))
            quad_top(s, a, s + WALL_T, b, LEDGE_H, SHADE_WALLTOP, TEXELS_PER_TILE);
    }
    if (has_ground(room, ROOM_SIZE - 1, ROOM_SIZE - 1))
        quad_top(s, s, s + WALL_T, s + WALL_T, LEDGE_H, SHADE_WALLTOP,
                 TEXELS_PER_TILE);
    NE_PolyEnd();
}

// Glowing portals in open back doorways and thresholds in open ledge gaps.
static void draw_open_doors(const Room *room, bool doors_open)
{
    if (!doors_open || 0 == room->door_count)
        return;
    const fx s = FX_INT(ROOM_SIZE);
    NE_MaterialUse(NULL);
    NE_PolyBegin(GL_QUADS);
    for (int i = 0; i < room->door_count; i++) {
        const Door *d = &room->doors[i];
        fx a = FX_INT(d->pos), b = FX_INT(d->pos + 1);
        u32 glow = reward_color(d->leads_to);
        const u32 dark = RGB15(1, 1, 2);
        switch (d->side) {
        case SIDE_NZ: flat_pz(a, b, 0, 0, DOOR_H, glow, dark); break;
        case SIDE_NX: flat_px(0, a, b, 0, DOOR_H, glow, dark); break;
        case SIDE_PZ: flat_top(a, s, b, s + WALL_T, FX(0.005), glow); break;
        case SIDE_PX: flat_top(s, a, s + WALL_T, b, FX(0.005), glow); break;
        default: break;
        }
    }
    NE_PolyEnd();
}

// Wall-mounted props: torches (animated) and chains, just off the wall.
#define WALL_PROP_OFFSET FX(0.1)

static void draw_wall_prop(const Decor *d, int index, int frame)
{
    fx nx, nz;
    side_inward(d->wall, &nx, &nz);
    fx x = d->x + fx_mul(nx, WALL_PROP_OFFSET);
    fx z = d->z + fx_mul(nz, WALL_PROP_OFFSET);
    SpriteSheet sheet = SHEET_PROPS;
    int slot;
    fx y;
    switch (d->kind) {
    case DECOR_TORCH:
        sheet = SHEET_FIRE;
        slot = FIRE_TORCH + ((frame / 4) + index * 5) % FIRE_TORCH_FRAMES;
        y = FX(0.5);
        break;
    case DECOR_BANNER:
        sheet = SHEET_CLUTTER;
        slot = d->variant ? CLUT_BANNER_BLUE : CLUT_BANNER_RED;
        y = FX(0.35);
        break;
    case DECOR_COBWEB:
        sheet = SHEET_CLUTTER;
        slot = CLUT_COBWEB;
        y = FX(0.75);
        break;
    case DECOR_CRYSTAL: {
        // A crystal set in the rock; now and then it glints.
        bool glint = ((frame / 6) + index * 7) % 40 < 3;
        render_billboard(SHEET_CAVE, glint ? CAVE_CRYSTAL_1_U : CAVE_CRYSTAL_U,
                         glint ? CAVE_CRYSTAL_1_V : CAVE_CRYSTAL_V, 16, 16,
                         false, x, FX(0.6), z, SPAL_NORMAL);
        return;
    }
    default:
        slot = PROP_CHAIN;
        y = FX(0.55);
        break;
    }
    render_prop(sheet, slot, false, x, y, z, SPAL_NORMAL);
}

// Upright parts of traps: spikes when out, flames, saw blades.
static void draw_traps(const Room *room, uint32_t room_frame, int frame)
{
    for (int i = 0; i < room->hazard_count; i++) {
        const Hazard *h = &room->hazards[i];
        int t;
        TrapState st = hazard_state(h, room_frame, &t);
        switch (h->kind) {
        case HAZ_SPIKES:
            if (TRAP_ACTIVE == st)
                render_billboard(SHEET_PROPS, prop_u(PROP_SPIKES + 5),
                                 prop_v(PROP_SPIKES + 5), 16, 8, false, h->x,
                                 0, h->z, SPAL_NORMAL);
            break;
        case HAZ_FIRE:
            if (TRAP_ACTIVE == st) {
                // One tall column of fire (16x32).
                int f = (frame / 3 + i) % FIRE_JET_FRAMES;
                render_billboard(SHEET_FIRE, f * 16, FIRE_JET_V, 16, 32,
                                 0 != (i & 1), h->x, 0, h->z, SPAL_NORMAL);
            }
            break;
        case HAZ_ROCKFALL:
            // The shadow grows; for the last frames the rock drops into
            // view; on impact it lies there a moment.
            if (TRAP_WARN == st) {
                int span = ROCK_HIT - ROCK_WARN;
                render_shadow(h->x, h->z, FX(0.3) + FX(0.9) * t / span);
                int left = span - t;
                if (left < 18) {
                    int slot = CLUT_BOULDER + (i % CLUT_BOULDER_FRAMES);
                    render_prop(SHEET_CLUTTER, slot, false, h->x,
                                FX(0.15) * left, h->z, SPAL_NORMAL);
                }
            } else if (TRAP_ACTIVE == st) {
                int slot = CLUT_BOULDER + (i % CLUT_BOULDER_FRAMES);
                render_prop(SHEET_CLUTTER, slot, false, h->x, 0, h->z,
                            SPAL_NORMAL);
            }
            break;
        case HAZ_SAW: {
            fx x, z;
            hazard_saw_pos(h, room_frame, &x, &z);
            int slot = CLUT_SAW + (frame / 2) % CLUT_SAW_FRAMES;
            render_prop(SHEET_CLUTTER, slot, false, x, -FX(0.25), z,
                        SPAL_NORMAL);
            break;
        }
        default:
            break;
        }
    }
}

static void billboard_scaled(SpriteSheet sheet, int u, int v, int w, int h,
                             bool mirror, fx x, fx y, fx z, SpritePal pal,
                             int alpha, int poly_id, int scale);

// A world-sheet item standing at (x, z).
typedef struct {
    uint8_t u, v, w, h, scale;
} WorldItem;

static const WorldItem world_trees[TREE_KIND_COUNT] = {
    [TREE_PINE]       = { WORLD_PINE_U, WORLD_PINE_V, 16, 16, 2 },
    [TREE_PINE2]      = { WORLD_PINE2_U, WORLD_PINE2_V, 16, 16, 2 },
    [TREE_PINE_SMALL] = { WORLD_PINE_SMALL_U, WORLD_PINE_SMALL_V, 16, 16, 1 },
    [TREE_FRUIT]      = { WORLD_TREE_FRUIT_U, WORLD_TREE_FRUIT_V, 16, 16, 2 },
    [TREE_ROUND]      = { WORLD_TREE_ROUND_U, WORLD_TREE_ROUND_V, 16, 16, 2 },
    [TREE_GREEN]      = { WORLD_TREE_GREEN_U, WORLD_TREE_GREEN_V, 16, 16, 2 },
    [TREE_PUNY]       = { WORLD_TREE_PUNY_U, WORLD_TREE_PUNY_V, 16, 16, 2 },
};

static bool world_item(const Decor *d, WorldItem *it)
{
    switch (d->kind) {
    case DECOR_TREE:
        *it = world_trees[d->variant < TREE_KIND_COUNT ? d->variant : 0];
        return true;
    case DECOR_BUSH:   *it = (WorldItem){ WORLD_BUSH_U, WORLD_BUSH_V, 16, 16, 1 }; return true;
    case DECOR_ROCK:   *it = (WorldItem){ WORLD_ROCK_U, WORLD_ROCK_V, 16, 16, 1 }; return true;
    case DECOR_STONES: *it = (WorldItem){ WORLD_STONES_U, WORLD_STONES_V, 16, 16, 1 }; return true;
    case DECOR_STUMP:  *it = (WorldItem){ WORLD_STUMP_U, WORLD_STUMP_V, 16, 16, 1 }; return true;
    case DECOR_TENT:   *it = (WorldItem){ WORLD_TENT_U, WORLD_TENT_V, 16, 16, 2 }; return true;
    case DECOR_WELL:   *it = (WorldItem){ WORLD_WELL_U, WORLD_WELL_V, 16, 16, 2 }; return true;
    case DECOR_SIGN:   *it = (WorldItem){ WORLD_SIGN_U, WORLD_SIGN_V, 16, 16, 1 }; return true;
    case DECOR_BENCH:
        if (d->variant)
            *it = (WorldItem){ WORLD_TABLE_U, WORLD_TABLE_V, 16, 16, 1 };
        else
            *it = (WorldItem){ WORLD_BENCH_U, WORLD_BENCH_V, 16, 16, 1 };
        return true;
    default:
        return false;
    }
}

// (No shadow ellipse: the tiles' trunks and legs stop above the cell's
// bottom edge, and a dark blob under them read as a hole.)
static void draw_world_item(const WorldItem *it, fx x, fx z, bool mirror)
{
    billboard_scaled(SHEET_WORLD, it->u, it->v, it->w, it->h, mirror, x, 0, z,
                     SPAL_NORMAL, 31, 1, it->scale);
}

static void crypt_span(const Room *room, fx *x0, fx *x1);

// The forest past the camp's -x and -z edges: rows of trees, blocks of
// pines furthest back. Laid out once, from a fixed seed; kept clear of the
// crypt.
#define FOREST_MAX 72

typedef struct {
    fx x, z;
    uint8_t kind;   // TREE_* kind, or TREE_KIND_COUNT for a pine block
    bool mirror;
} ForestTree;

static ForestTree forest[FOREST_MAX];
static int forest_count = -1;

static void forest_row(Rng *rng, fx depth, fx step, bool along_x, bool blocks,
                       fx skip0, fx skip1)
{
    static const uint8_t kinds[] = { TREE_PINE, TREE_PINE2, TREE_ROUND,
                                     TREE_GREEN, TREE_PINE, TREE_FRUIT };
    for (fx t = -FX(3.0); t < FX_INT(ROOM_SIZE) && forest_count < FOREST_MAX;
         t += step) {
        fx jitter = (fx)rng_range(rng, 4096) * 3 / 10 - FX(0.15);
        fx along = t + jitter;
        if (along > skip0 && along < skip1)
            continue;
        ForestTree *f = &forest[forest_count++];
        fx d = depth + (fx)rng_range(rng, 4096) / 5;
        f->x = along_x ? along : -d;
        f->z = along_x ? -d : along;
        f->kind = blocks ? TREE_KIND_COUNT
                         : kinds[rng_range(rng, sizeof(kinds))];
        f->mirror = rng_chance(rng, 50);
    }
}

static void draw_forest(const Room *room)
{
    if (forest_count < 0) {
        Rng rng;
        rng_seed(&rng, 0xF0E57u);
        forest_count = 0;
        fx cx0, cx1;
        crypt_span(room, &cx0, &cx1);
        fx s0 = cx0 - FX(0.6), s1 = cx1 + FX(0.6);
        // Behind -z (above the crypt's roof only the far blocks).
        forest_row(&rng, FX(0.5), FX(1.1), true, false, s0, s1);
        forest_row(&rng, FX(1.6), FX(1.3), true, false, s0, s1);
        forest_row(&rng, FX(3.0), FX(1.7), true, true, 0, 0);
        // Behind -x, the corner included.
        forest_row(&rng, FX(0.5), FX(1.1), false, false, 0, 0);
        forest_row(&rng, FX(1.6), FX(1.3), false, false, 0, 0);
        forest_row(&rng, FX(3.0), FX(1.7), false, true, 0, 0);
    }
    for (int i = 0; i < forest_count; i++) {
        const ForestTree *f = &forest[i];
        if (TREE_KIND_COUNT == f->kind) {
            billboard_scaled(SHEET_WORLD, WORLD_FOREST_U, WORLD_FOREST_V,
                             WORLD_FOREST_W, WORLD_FOREST_H, f->mirror, f->x, 0,
                             f->z, SPAL_NORMAL, 31, 1, 1);
        } else {
            WorldItem it = world_trees[f->kind];
            billboard_scaled(SHEET_WORLD, it.u, it.v, it.w, it.h, f->mirror,
                             f->x, 0, f->z, SPAL_NORMAL, 31, 1, it.scale);
        }
    }
}

void render_room_props(const Room *room, bool doors_open, bool chest_open,
                       const PropState *ps, int frame)
{
    if (look->outdoor)
        draw_forest(room);
    // Reward icons hover over exits once they are open, bobbing gently
    // (not over the camp's crypt).
    static const int8_t bob[8] = { 0, 1, 2, 2, 2, 1, 0, -1 };
    fx lift = bob[(frame / 6) & 7] * FX(0.02);
    if (doors_open && !room->hub) {
        for (int i = 0; i < room->door_count; i++) {
            const Door *d = &room->doors[i];
            fx x, z, nx, nz;
            door_center(d->side, d->pos, &x, &z);
            side_inward(d->side, &nx, &nz);
            bool back = SIDE_NX == d->side || SIDE_NZ == d->side;
            x += fx_mul(nx, back ? FX(0.45) : FX(0.6));
            z += fx_mul(nz, back ? FX(0.45) : FX(0.6));
            if (RT_BOSS == d->leads_to) {
                render_prop(SHEET_CLUTTER, CLUT_SKULL, false, x,
                            (back ? FX(1.0) : FX(0.2)) + lift, z, SPAL_NORMAL);
                continue;
            }
            render_billboard(SHEET_ITEMS, d->leads_to * 16, 0, 16, 16, false,
                             x, (back ? FX(1.15) : FX(0.35)) + lift, z, SPAL_NORMAL);
        }
    }

    if (room->feature_x >= 0) {
        fx x = FX_INT(room->feature_x) + FX_HALF;
        fx z = FX_INT(room->feature_z) + FX_HALF;
        render_shadow(x, z, FX(1.1));
        if (CELL_CHEST == room_cell(room, room->feature_x, room->feature_z)) {
            int slot = chest_open ? PROP_CHEST_OPEN : PROP_CHEST;
            render_prop(SHEET_PROPS, slot, false, x, 0, z, SPAL_NORMAL);
        } else {
            int slot = FIRE_CAMP + (frame / 6) % FIRE_CAMP_FRAMES;
            render_prop(SHEET_FIRE, slot, false, x, 0, z, SPAL_NORMAL);
        }
    }

    for (int i = 0; i < room->decor_count; i++) {
        const Decor *d = &room->decor[i];
        if (decor_is_wall_mounted(d->kind)) {
            draw_wall_prop(d, i, frame);
            continue;
        }
        if (ps->broken & (1u << i))
            continue;   // debris is drawn with the floor
        WorldItem it;
        if (world_item(d, &it)) {
            draw_world_item(&it, d->x, d->z, 0 != (i & 1));
            continue;
        }
        SpriteSheet sheet = SHEET_PROPS;
        SpritePal pal = SPAL_NORMAL;
        int slot;
        switch (d->kind) {
        case DECOR_BARREL:
            slot = d->variant ? PROP_BARREL2 : PROP_BARREL;
            break;
        case DECOR_POT:
            slot = PROP_POT;
            break;
        case DECOR_EXPLOSIVE:
            sheet = SHEET_CLUTTER;
            slot = CLUT_BARREL_RED;
            // A lit fuse flashes, faster as it burns down.
            if (ps->fuse && ps->fuse[i]) {
                int rate = ps->fuse[i] > 20 ? 4 : 2;
                if ((frame / rate) & 1)
                    pal = SPAL_FLASH;
            }
            break;
        case DECOR_SKULL:
            sheet = SHEET_CLUTTER;
            slot = CLUT_SKULL;
            break;
        default:
            continue; // decals are drawn with the floor
        }
        render_prop(sheet, slot, false, d->x, 0, d->z, pal);
    }
    draw_traps(room, ps->room_frame, frame);
}

// Fallback if translucent obstacles glitch on some target: build with
// -DFADE_USE_ALPHA=0 to draw faded obstacles as short stumps instead.
#ifndef FADE_USE_ALPHA
#define FADE_USE_ALPHA 1
#endif
#define FADE_STUMP_H FX(0.2)

static MaterialId cell_material(CellType t)
{
    return CELL_PILLAR == t ? look->pillar : MAT_CRATE;
}

// A rock spire (stalagmite): a box whose camera-facing sides lean in to a
// narrow top. Same windings as quad_px / quad_pz.
static void spire(fx x0, fx z0, fx x1, fx z1, fx h)
{
    const int t = TEXELS_PER_TILE;
    fx k = (x1 - x0) / 3;
    fx tx0 = x0 + k, tx1 = x1 - k, tz0 = z0 + k, tz1 = z1 - k;
    int vb = face_v(0), vt = face_v(h);
    quad_top(tx0, tz0, tx1, tz1, h, SHADE_TOP, t);
    // +x side: the base along x = x1, the top along x = tx1.
    fx l0 = light_at(x1, z0), l1 = light_at(x1, z1);
    color(l1, SHADE_PX); NE_PolyTexCoord(FX_TO_INT(z1 * t), vb); vtx(x1, 0, z1);
    color(l0, SHADE_PX); NE_PolyTexCoord(FX_TO_INT(z0 * t), vb); vtx(x1, 0, z0);
    color(l0, fx_mul(SHADE_PX, FX(0.7)));
    NE_PolyTexCoord(FX_TO_INT(tz0 * t), vt); vtx(tx1, h, tz0);
    color(l1, fx_mul(SHADE_PX, FX(0.7)));
    NE_PolyTexCoord(FX_TO_INT(tz1 * t), vt); vtx(tx1, h, tz1);
    // +z side.
    l0 = light_at(x0, z1);
    l1 = light_at(x1, z1);
    color(l0, SHADE_PZ); NE_PolyTexCoord(FX_TO_INT(x0 * t), vb); vtx(x0, 0, z1);
    color(l1, SHADE_PZ); NE_PolyTexCoord(FX_TO_INT(x1 * t), vb); vtx(x1, 0, z1);
    color(l1, fx_mul(SHADE_PZ, FX(0.7)));
    NE_PolyTexCoord(FX_TO_INT(tx1 * t), vt); vtx(tx1, h, tz1);
    color(l0, fx_mul(SHADE_PZ, FX(0.7)));
    NE_PolyTexCoord(FX_TO_INT(tx0 * t), vt); vtx(tx0, h, tz1);
}

static void cell_box(int x, int z, CellType t, fx h)
{
    fx in = room_cell_inset(t);
    fx x0 = FX_INT(x) + in, z0 = FX_INT(z) + in;
    fx x1 = FX_INT(x + 1) - in, z1 = FX_INT(z + 1) - in;
    if (look->rough && CELL_PILLAR == t)
        spire(x0 - in / 2, z0 - in / 2, x1 + in / 2, z1 + in / 2, h);
    else
        box(x0, z0, x1, z1, h);
}

static void draw_obstacles(const Room *room, const RoomAlpha alpha,
                           CellType type)
{
    bool any = false;
    for (int z = 0; z < ROOM_SIZE; z++) {
        for (int x = 0; x < ROOM_SIZE; x++) {
            if (room_cell(room, x, z) != type)
                continue;
            fx h = room_cell_height(type);
            if (alpha[z][x] < FADE_ALPHA_OPAQUE) {
                if (FADE_USE_ALPHA)
                    continue;
                h = FADE_STUMP_H;
            }
            if (!any) {
                use_material(cell_material(type));
                NE_PolyBegin(GL_QUADS);
                any = true;
            }
            cell_box(x, z, type, h);
        }
    }
    if (any)
        NE_PolyEnd();
}

// Outdoors (the camp): ground running back under the forest, a rock
// cliff along the front edges, and the crypt in place of a door.

#define FOREST_DEPTH FX(3.5)   // ground behind the -x and -z edges
#define CRYPT_HALF   1         // tiles either side of the doorway
#define CRYPT_DEPTH  FX(2.4)
#define CRYPT_H      FX(1.6)
#define CRYPT_ROOF_H FX(0.5)   // the upper tier

// The crypt's footprint along the z = 0 edge, around the door cell pos.
static void crypt_span(const Room *room, fx *x0, fx *x1)
{
    int pos = room->door_count ? room->doors[0].pos : ROOM_SIZE / 2;
    *x0 = FX_INT(pos - CRYPT_HALF);
    *x1 = FX_INT(pos + 1 + CRYPT_HALF);
}

static void draw_outdoor_ground(const Room *room)
{
    const fx s = FX_INT(ROOM_SIZE), d = FOREST_DEPTH;
    const fx shade = FX(0.75);
    fx cx0, cx1;
    crypt_span(room, &cx0, &cx1);
    use_material(look->floor);
    NE_PolyBegin(GL_QUADS);
    // Behind the -x edge, the corner included; in 2-tile strips so the
    // light can vary along them.
    for (int i = -4; i < ROOM_SIZE; i += 2) {
        fx a = FX_INT(i), b = fx_min(FX_INT(i + 2), s);
        quad_top(-d, fx_max(a, -d), 0, b, 0, shade, TEXELS_PER_TILE);
    }
    // Behind the -z edge, leaving the crypt's footprint open (its stairs go
    // down there).
    for (int i = 0; i < ROOM_SIZE; i += 2) {
        fx a = FX_INT(i), b = FX_INT(i + 2);
        quad_top(a, -d, b, -CRYPT_DEPTH, 0, shade, TEXELS_PER_TILE);
        if (b <= cx0 || a >= cx1) {
            quad_top(a, -CRYPT_DEPTH, b, 0, 0, shade, TEXELS_PER_TILE);
        } else {
            if (a < cx0)
                quad_top(a, -CRYPT_DEPTH, cx0, 0, 0, shade, TEXELS_PER_TILE);
            if (b > cx1)
                quad_top(cx1, -CRYPT_DEPTH, b, 0, 0, shade, TEXELS_PER_TILE);
        }
    }
    NE_PolyEnd();

    // The hilltop's front edges drop away as rock.
    use_material(look->cliff);
    NE_PolyBegin(GL_QUADS);
    for (int i = 0; i < ROOM_SIZE; i++) {
        fx a = FX_INT(i), b = FX_INT(i + 1);
        cliff_pz(a, b, s);
        cliff_px(s, a, b);
    }
    NE_PolyEnd();
}

// A stone mausoleum over the way down: walls around the doorway, a stepped
// roof, and stairs sinking into the dark behind the door.
static void draw_crypt(const Room *room)
{
    fx x0, x1;
    crypt_span(room, &x0, &x1);
    fx dx0 = x0 + FX_INT(CRYPT_HALF), dx1 = dx0 + FX_ONE;   // the doorway
    const fx z0 = -CRYPT_DEPTH, h = CRYPT_H;
    const fx in = FX(0.4), top = h + CRYPT_ROOF_H;

    use_material(MAT_WALL);
    NE_PolyBegin(GL_QUADS);
    quad_pz(x0, dx0, 0, 0, h, SHADE_PZ);
    quad_pz(dx1, x1, 0, 0, h, SHADE_PZ);
    quad_pz(dx0, dx1, 0, DOOR_H, h, SHADE_PZ);
    quad_px(x1, z0, 0, 0, h, SHADE_PX);
    // The upper tier.
    quad_pz(x0 + in, x1 - in, -in, h, top, SHADE_PZ);
    quad_px(x1 - in, z0 + in, -in, h, top, SHADE_PX);
    NE_PolyEnd();

    use_material(MAT_WALLTOP);
    NE_PolyBegin(GL_QUADS);
    quad_top(x0, z0, x1, -in, h, SHADE_WALLTOP, TEXELS_PER_TILE);
    quad_top(x0, -in, x1, 0, h, SHADE_WALLTOP, TEXELS_PER_TILE);
    quad_top(x0 + in, z0 + in, x1 - in, -in, top, SHADE_WALLTOP,
             TEXELS_PER_TILE);
    NE_PolyEnd();

    // Inside the doorway: steps going down, darker each one, to a black
    // back wall.
    NE_MaterialUse(NULL);
    NE_PolyBegin(GL_QUADS);
    static const u32 step_col[4] = {
        RGB15(9, 8, 10), RGB15(6, 5, 7), RGB15(3, 3, 4), RGB15(1, 1, 2),
    };
    const fx depth = FX(0.35), drop = FX(0.22);
    for (int k = 0; k < 4; k++) {
        fx za = -depth * k, zb = -depth * (k + 1), y = -drop * k;
        flat_top(dx0, zb, dx1, za, y, step_col[k]);
        if (k < 3)   // the riser down to the next step
            flat_pz(dx0, dx1, zb, y - drop, y, step_col[k + 1], step_col[k]);
    }
    flat_pz(dx0, dx1, -depth * 4, -drop * 4, DOOR_H, RGB15(0, 0, 1),
            RGB15(0, 0, 1));
    // The passage's left wall, seen through the door.
    flat_px(dx0, -depth * 4, 0, -drop * 4, DOOR_H, RGB15(1, 1, 2),
            RGB15(4, 4, 5));
    NE_PolyEnd();
}

// Liquids, floods and crust

#define LIQUID_Y FX(-0.3)

// The look's liquid in every pit, its texture drifting. Lava shines by
// itself, pulsing a little; water takes the room's light.
static void draw_liquid(const Room *room, int frame)
{
    if (look->liquid < 0)
        return;
    use_material((MaterialId)look->liquid);
    NE_PolyBegin(GL_QUADS);
    int du = (frame / 4) % 64, dv = (frame / 7) % 64;
    static const uint8_t pulse[8] = { 31, 30, 28, 27, 27, 28, 30, 31 };
    int glow = pulse[(frame / 10) & 7];
    for (int z = 0; z < ROOM_SIZE; z++) {
        for (int x = 0; x < ROOM_SIZE; x++) {
            if (CELL_PIT != room->cells[z][x])
                continue;
            fx x0 = FX_INT(x), x1 = FX_INT(x + 1);
            fx z0 = FX_INT(z), z1 = FX_INT(z + 1);
            int u0 = x * 16 + du, v0 = z * 16 + dv;
            const fx corner[4][2] = { { x0, z1 }, { x1, z1 }, { x1, z0 },
                                      { x0, z0 } };
            const int uv[4][2] = { { u0, v0 + 16 }, { u0 + 16, v0 + 16 },
                                   { u0 + 16, v0 }, { u0, v0 } };
            for (int k = 0; k < 4; k++) {
                if (look->glow)
                    NE_PolyColor(RGB15(glow, glow, glow));
                else
                    color(light_at(corner[k][0], corner[k][1]), FX(0.8));
                NE_PolyTexCoord(uv[k][0], uv[k][1]);
                vtx(corner[k][0], LIQUID_Y, corner[k][1]);
            }
        }
    }
    NE_PolyEnd();
}

// The ring about to flood throbs red; crust glows through its cracks,
// brighter as it gives way.
static void draw_heat(const Room *room, const PropState *ps, int frame)
{
    bool any = false;
    if (ps->flood_ring >= 0) {
        u32 c = (frame / 6) & 1 ? RGB15(31, 10, 2) : RGB15(18, 4, 1);
        for (int z = 0; z < ROOM_SIZE; z++) {
            for (int x = 0; x < ROOM_SIZE; x++) {
                if (run_flood_ring_of(x, z) != ps->flood_ring
                    || CELL_FLOOR != room->cells[z][x])
                    continue;
                if (!any) {
                    NE_MaterialUse(NULL);
                    NE_PolyBegin(GL_QUADS);
                    any = true;
                }
                flat_top(FX_INT(x) + FX(0.06), FX_INT(z) + FX(0.06),
                         FX_INT(x + 1) - FX(0.06), FX_INT(z + 1) - FX(0.06),
                         FX(0.004), c);
            }
        }
    }
    for (int i = 0; ps->crust && i < room->hazard_count; i++) {
        const Hazard *h = &room->hazards[i];
        if (HAZ_CRUST != h->kind || ps->crust[i] >= CRUST_CRACK)
            continue;   // open: the lava shows
        int t = ps->crust[i];
        u32 c = 0 == t ? RGB15(10, 3, 1)
              : ((frame / (t > CRUST_CRACK / 2 ? 2 : 4)) & 1)
                ? RGB15(31, 16, 2) : RGB15(20, 6, 1);
        if (!any) {
            NE_MaterialUse(NULL);
            NE_PolyBegin(GL_QUADS);
            any = true;
        }
        flat_top(h->x - FX(0.42), h->z - FX(0.42), h->x + FX(0.42),
                 h->z + FX(0.42), FX(0.003), c);
    }
    if (any)
        NE_PolyEnd();
}

void render_room(const Room *room, const RoomAlpha alpha, fx lx, fx lz,
                 bool doors_open, int frame, const PropState *ps)
{
    set_look(room);
#ifdef DELVE_DEBUG
    int t0 = REG_VCOUNT;
#define RSECTION(i) do { int t = REG_VCOUNT; \
        debug_room_sections[i] = (t + 263 - t0) % 263; t0 = t; } while (0)
#else
#define RSECTION(i) do { } while (0)
#endif
    set_lights(room, lx, lz, frame, ps->room_frame);
    RSECTION(0);
    draw_floor(room);
    draw_liquid(room, frame);
    draw_heat(room, ps, frame);
    RSECTION(1);
    draw_decals(room, ps);
    RSECTION(2);
    if (look->outdoor) {
        draw_outdoor_ground(room);
        draw_crypt(room);
        RSECTION(3);
        RSECTION(4);
    } else {
        draw_back_walls(room, doors_open);
        draw_front_ledges(room, doors_open);
        RSECTION(3);
        draw_outline(room);
        RSECTION(4);
        draw_open_doors(room, doors_open);
    }
    draw_obstacles(room, alpha, CELL_PILLAR);
    draw_obstacles(room, alpha, CELL_CRATE);
}

void render_room_faded(const Room *room, const RoomAlpha alpha)
{
    if (!FADE_USE_ALPHA)
        return;
    // Each faded box gets its own polygon ID: the DS does not blend a
    // translucent polygon over another with the same ID.
    int id = 8;
    for (int z = 0; z < ROOM_SIZE; z++) {
        for (int x = 0; x < ROOM_SIZE; x++) {
            CellType t = room_cell(room, x, z);
            if ((CELL_PILLAR != t && CELL_CRATE != t)
                || alpha[z][x] >= FADE_ALPHA_OPAQUE)
                continue;
            use_material(cell_material(t));
            NE_PolyFormat(alpha[z][x], id, 0, NE_CULL_BACK, 0);
            NE_PolyBegin(GL_QUADS);
            cell_box(x, z, t, room_cell_height(t));
            NE_PolyEnd();
            id = id < 62 ? id + 1 : 8;
        }
    }
    NE_PolyFormat(31, 0, 0, NE_CULL_BACK, 0);
}

// Sprites

// 16 texels per tile, like the floor and walls. At the close zoom (8 tiles
// over 256 px) one texel is 2 screen pixels; at the far zoom (16 tiles), 1.
#define TEXEL_W   FX(0.0625)
#define TEXEL_H   FX(0.0763)    // TEXEL_W / cos(35)
#define SPRITE_MIN_LIGHT FX(0.7)

// A billboard any size: scale in fixed point (FX_ONE = 16 texels a tile).
static void billboard_fx(SpriteSheet sheet, int u, int v, int w, int h,
                         bool mirror, fx x, fx y, fx z, SpritePal pal,
                         int alpha, int poly_id, fx scale)
{
    int u0 = u, u1 = u + w;
    if (mirror) {
        u0 = u + w;
        u1 = u;
    }
    // Half width along screen-right: (w/2 * TEXEL_W) * (0.70711, -0.70711).
    fx rx = fx_mul(fx_mul(w * TEXEL_W / 2, scale), FX(0.70711));
    fx lx = x, lz = z;   // the light is taken where it stands
    TOWARDS_CAMERA(x, y, z, SPRITE_BIAS);
    fx top = y + fx_mul(h * TEXEL_H, scale);

    NE_MaterialUse(sheets[sheet]);
    if (SPAL_FLASH == pal)
        NE_PaletteUse(white_palette);
    else if (SPAL_ELITE == pal && elite_pals[sheet])
        NE_PaletteUse(elite_pals[sheet]);
    else if (pal >= SPAL_MAGMA && variant_pals[sheet][pal])
        NE_PaletteUse(variant_pals[sheet][pal]);
    NE_PolyFormat(alpha, poly_id, 0, NE_CULL_NONE, 0);
    NE_PolyBegin(GL_QUADS);
    // Sprites never drop below SPRITE_MIN_LIGHT so enemies stay readable in
    // the dark corners.
    fx l = fx_max(light_at(lx, lz), SPRITE_MIN_LIGHT);
    color(SPAL_FLASH == pal ? FX_ONE : l, FX_ONE);
    NE_PolyTexCoord(u0, v + h); vtx(x - rx, y, z + rx);
    NE_PolyTexCoord(u1, v + h); vtx(x + rx, y, z - rx);
    NE_PolyTexCoord(u1, v);     vtx(x + rx, top, z - rx);
    NE_PolyTexCoord(u0, v);     vtx(x - rx, top, z + rx);
    NE_PolyEnd();
    NE_PolyFormat(31, 0, 0, NE_CULL_BACK, 0);
}

static void billboard_scaled(SpriteSheet sheet, int u, int v, int w, int h,
                             bool mirror, fx x, fx y, fx z, SpritePal pal,
                             int alpha, int poly_id, int scale)
{
    billboard_fx(sheet, u, v, w, h, mirror, x, y, z, pal, alpha, poly_id,
                 scale * FX_ONE);
}

static void billboard(SpriteSheet sheet, int u, int v, int w, int h,
                      bool mirror, fx x, fx y, fx z, SpritePal pal, int alpha,
                      int poly_id)
{
    billboard_scaled(sheet, u, v, w, h, mirror, x, y, z, pal, alpha, poly_id,
                     1);
}

// Code Genie's giant mode (cheat.h): characters, monsters and shadows
// drawn larger. Monsters already drawn large stop at CHAR_SCALE_MAX.
#define CHAR_SCALE_MAX FX(3.0)
static fx char_scale = FX_ONE;

// Top-left texel of a slot in a 24x24-cell character sheet.
static void char_cell_uv(int slot, int *u, int *v)
{
    *u = (slot % CHAR_SHEET_COLS) * CHAR_CELL;
    *v = (slot / CHAR_SHEET_COLS) * CHAR_CELL;
}

void render_set_char_scale(fx scale)
{
    char_scale = scale;
}

void render_char_scaled(SpriteSheet sheet, int slot, bool mirror,
                        int feet_row, fx x, fx lift, fx z, SpritePal pal,
                        int scale)
{
    int u, v;
    char_cell_uv(slot, &u, &v);
    fx y = lift - (CHAR_CELL - feet_row) * scale * TEXEL_H;
    billboard_scaled(sheet, u, v, CHAR_CELL, CHAR_CELL, mirror, x, y, z, pal,
                     31, 1, scale);
}

void render_monster(SpriteSheet sheet, int cell, int slot, bool mirror,
                    int feet_row, fx x, fx lift, fx z, SpritePal pal, fx scale)
{
    int cols = 256 / cell;
    int u = (slot % cols) * cell;
    int v = (slot / cols) * cell;
    if (FX_ONE != char_scale)
        scale = fx_min(fx_mul(scale, char_scale), CHAR_SCALE_MAX);
    fx y = lift - fx_mul((cell - feet_row) * TEXEL_H, scale);
    billboard_fx(sheet, u, v, cell, cell, mirror, x, y, z, pal, 31, 1, scale);
}

void render_billboard(SpriteSheet sheet, int u, int v, int w, int h,
                      bool mirror, fx x, fx y, fx z, SpritePal pal)
{
    billboard(sheet, u, v, w, h, mirror, x, y, z, pal, 31, 1);
}

void render_prop(SpriteSheet sheet, int slot, bool mirror, fx x, fx y, fx z,
                 SpritePal pal)
{
    render_billboard(sheet, prop_u(slot), prop_v(slot), 16, 16, mirror, x, y, z,
                     pal);
}

void render_billboard_alpha(SpriteSheet sheet, int u, int v, int w, int h,
                            bool mirror, fx x, fx y, fx z, SpritePal pal,
                            int alpha, int poly_id)
{
    billboard(sheet, u, v, w, h, mirror, x, y, z, pal, alpha, poly_id);
}

void render_char_alpha(SpriteSheet sheet, int slot, bool mirror, int feet_row,
                       fx x, fx z, int alpha, int poly_id)
{
    int u, v;
    char_cell_uv(slot, &u, &v);
    fx y = -fx_mul((CHAR_CELL - feet_row) * TEXEL_H, char_scale);
    billboard_fx(sheet, u, v, CHAR_CELL, CHAR_CELL, mirror, x, y, z,
                 SPAL_FLASH, alpha, poly_id, char_scale);
}

void render_ground(SpriteSheet sheet, int u, int v, int w, int h, fx x, fx z,
                   fx half, int alpha, int poly_id, SpritePal pal)
{
    fx y = FX(0.02);
    TOWARDS_CAMERA(x, y, z, GROUND_BIAS);
    NE_MaterialUse(sheets[sheet]);
    if (SPAL_ELITE == pal && elite_pals[sheet])
        NE_PaletteUse(elite_pals[sheet]);
    NE_PolyFormat(alpha, poly_id, 0, NE_CULL_NONE, 0);
    NE_PolyBegin(GL_QUADS);
    NE_PolyColor(RGB15(31, 31, 31));
    NE_PolyTexCoord(u, v + h);     vtx(x - half, y, z + half);
    NE_PolyTexCoord(u + w, v + h); vtx(x + half, y, z + half);
    NE_PolyTexCoord(u + w, v);     vtx(x + half, y, z - half);
    NE_PolyTexCoord(u, v);         vtx(x - half, y, z - half);
    NE_PolyEnd();
    NE_PolyFormat(31, 0, 0, NE_CULL_BACK, 0);
}


// A particle's cell in the effects sheet. Fire and smoke age through a run
// of cells (art_layout.h FX_PART_*): an ember's own cell, then its cooling
// ones.
static int particle_cell(const Particle *p)
{
    int first, phases;
    switch (p->kind) {
    case PART_MOTE:   return PART_DUST;
    case PART_EMBER:
        // Bright for the first half of its life, then cooling.
        if (p->life * 2 >= p->max_life)
            return PART_EMBER;
        first = FX_PART_EMBER_OLD;
        phases = FX_PART_EMBER_OLD_PHASES;
        return first + (p->max_life / 2 - p->life) * phases
                       / (p->max_life / 2 + 1);
    case PART_CINDER: first = FX_PART_CINDER; phases = FX_PART_CINDER_PHASES; break;
    case PART_FLAME:  first = FX_PART_FLAME; phases = FX_PART_FLAME_PHASES; break;
    case PART_WISP:   first = FX_PART_WISP; phases = FX_PART_WISP_PHASES; break;
    default:          return p->kind;
    }
    int age = p->max_life - p->life;
    return first + age * phases / (p->max_life + 1);
}

void render_fx(const Fx *f)
{
    // All particles in one batch: one material, one polygon format.
    const int sz = FX_PARTICLE_SIZE;
    const fx rx = fx_mul(sz * TEXEL_W / 2, FX(0.70711));
    const fx h = sz * TEXEL_H;
    bool any = false;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &f->parts[i];
        if (!p->active)
            continue;
        if (!any) {
            NE_MaterialUse(sheets[SHEET_FX]);
            NE_PolyFormat(31, 1, 0, NE_CULL_NONE, 0);
            NE_PolyBegin(GL_QUADS);
            any = true;
        }
        int u = particle_cell(p) * sz, v = FX_PARTICLE_V;
        // The nearest grid light: particles are tiny, so this is close
        // enough and far cheaper than computing every light.
        fx l = light_at(p->x, p->z);
        fx x = p->x, y = p->y, z = p->z;
        TOWARDS_CAMERA(x, y, z, SPRITE_BIAS);
        // Fire makes its own light.
        bool glows = PART_EMBER == p->kind || PART_CINDER == p->kind
                  || PART_FLAME == p->kind || PART_SPARK == p->kind;
        color(glows ? FX_ONE : fx_max(l, SPRITE_MIN_LIGHT), FX_ONE);
        NE_PolyTexCoord(u, v + sz);      vtx(x - rx, y, z + rx);
        NE_PolyTexCoord(u + sz, v + sz); vtx(x + rx, y, z - rx);
        NE_PolyTexCoord(u + sz, v);      vtx(x + rx, y + h, z - rx);
        NE_PolyTexCoord(u, v);           vtx(x - rx, y + h, z + rx);
    }
    if (any) {
        NE_PolyEnd();
        NE_PolyFormat(31, 0, 0, NE_CULL_BACK, 0);
    }
    for (int i = 0; i < MAX_FLOATERS; i++) {
        const Floater *fl = &f->floaters[i];
        if (!fl->active || (fl->life < 10 && (fl->life & 1)))
            continue;   // blink out at the end
        int v = fl->kill ? FX_DIGIT_GOLD_V : FX_DIGIT_V;
        int digits = fl->value >= 10 ? 2 : 1;
        // Each digit is offset along screen-right.
        for (int d = 0; d < digits; d++) {
            int value = 0 == d && 2 == digits ? fl->value / 10 : fl->value % 10;
            fx off = (d * 2 - (digits - 1)) * (FX_DIGIT_W * TEXEL_W / 2);
            fx ox = fx_mul(off, FX(0.70711)), oz = -ox;
            render_billboard(SHEET_FX, value * FX_DIGIT_W, v, FX_DIGIT_W,
                             FX_DIGIT_H, false, fl->x + ox, fl->y, fl->z + oz,
                             SPAL_NORMAL);
        }
    }
}

void render_char(SpriteSheet sheet, int slot, bool mirror, int feet_row,
                 fx x, fx z, SpritePal pal)
{
    int u, v;
    char_cell_uv(slot, &u, &v);
    fx y = -fx_mul((CHAR_CELL - feet_row) * TEXEL_H, char_scale);
    billboard_fx(sheet, u, v, CHAR_CELL, CHAR_CELL, mirror, x, y, z, pal, 31, 1,
                 char_scale);
}

void render_shadow(fx x, fx z, fx scale)
{
    // Shadow ellipse at (CHAR_SHADOW_U, CHAR_SHADOW_V), 16x8, in the
    // character sheets. Its texture u axis is laid along screen-right and v
    // along screen-down on the ground, which makes the corners of the quad
    // sit on the world axes.
    fx s = fx_mul(fx_mul(FX(0.45), scale), char_scale);
    fx y = FX(0.01);
    TOWARDS_CAMERA(x, y, z, GROUND_BIAS);
    const int u = CHAR_SHADOW_U, v = CHAR_SHADOW_V;

    NE_MaterialUse(sheets[SHEET_PLAYER]);
    NE_PolyFormat(31, 2, 0, NE_CULL_NONE, 0);
    NE_PolyBegin(GL_QUADS);
    NE_PolyColor(RGB15(31, 31, 31));
    NE_PolyTexCoord(u, v);          vtx(x - s, y, z);
    NE_PolyTexCoord(u + 16, v);     vtx(x, y, z - s);
    NE_PolyTexCoord(u + 16, v + 8); vtx(x + s, y, z);
    NE_PolyTexCoord(u, v + 8);      vtx(x, y, z + s);
    NE_PolyEnd();
    NE_PolyFormat(31, 0, 0, NE_CULL_BACK, 0);
}

// The void around a room

#define VOID_PROPS 10

typedef enum {
    VP_ROCK,      // a chunk of masonry: a small textured box
    VP_PILLAR,    // a broken column: a tall box
    VP_BOULDER,   // a round stone: billboard
    VP_KIND_COUNT
    // Hanging chains (three stacked billboards) were dropped after 2.1.0:
    // drifting behind the back wall they read as a glitch.
} VoidKind;

typedef struct {
    uint8_t kind;
    uint8_t frame;   // boulder variant
    uint8_t depth;   // parallax: 0 = moves with the camera (far) .. 16 = fixed
    uint8_t phase;   // bobbing
    fx x, y, z;      // position with the camera at the room centre
    fx size;
} VoidProp;

static VoidProp void_props[VOID_PROPS];

void render_void_init(uint32_t seed)
{
    Rng rng;
    rng_seed(&rng, rng_hash3(seed, 0x701Du, 0));
    for (int i = 0; i < VOID_PROPS; i++) {
        VoidProp *v = &void_props[i];
        v->kind = (uint8_t)rng_range(&rng, VP_KIND_COUNT);
        v->frame = (uint8_t)rng_range(&rng, CLUT_BOULDER_FRAMES);
        v->depth = (uint8_t)rng_between(&rng, 4, 12);
        v->phase = (uint8_t)rng_range(&rng, 64);
        v->size = FX(0.5) + (fx)rng_range(&rng, 1001) * FX(0.001);
        // Beyond the back walls (the -x and -z sides, up the screen), or
        // well below the room all round, so nothing floats over the floor.
        if (rng_chance(&rng, 50)) {
            int across = rng_between(&rng, -4, ROOM_SIZE);
            int out = rng_between(&rng, 3, 7);
            bool nx = rng_chance(&rng, 50);
            v->x = FX_INT(nx ? -out : across);
            v->z = FX_INT(nx ? across : -out);
            v->y = FX_INT(rng_between(&rng, -2, 2));
        } else {
            int side = (int)rng_range(&rng, 4);
            int along = rng_between(&rng, -2, ROOM_SIZE + 2);
            int out = rng_between(&rng, 2, 6);
            v->x = FX_INT(0 == side ? -out : 1 == side ? ROOM_SIZE + out : along);
            v->z = FX_INT(2 == side ? -out : 3 == side ? ROOM_SIZE + out : along);
            v->y = -FX_INT(rng_between(&rng, 4, 8));
        }
    }
}

void render_void(const Camera *cam, int frame)
{
    const fx mid = FX_INT(ROOM_SIZE) / 2;
    for (int i = 0; i < VOID_PROPS; i++) {
        const VoidProp *v = &void_props[i];
        // A far prop follows the camera most of the way, so it barely moves
        // on screen; a near one stays put in the world.
        fx follow = FX_ONE - v->depth * (FX_ONE / 16);
        fx x = v->x + fx_mul(cam->x - mid, follow);
        fx z = v->z + fx_mul(cam->z - mid, follow);
        static const int8_t bob[8] = { 0, 1, 2, 2, 1, 0, -1, -1 };
        fx y = v->y + bob[((frame >> 4) + v->phase) & 7] * FX(0.04);
        // Parallax can carry a near prop over the room: skip it there.
        const fx edge = FX_ONE;
        if (y > -FX_INT(3) && x > -edge && z > -edge
            && x < FX_INT(ROOM_SIZE) + edge && z < FX_INT(ROOM_SIZE) + edge)
            continue;
        // Darker the further away, into the fog.
        fx shade = FX(0.15) + v->depth * FX(0.025);
        switch (v->kind) {
        case VP_ROCK:
        case VP_PILLAR: {
            fx w = VP_PILLAR == v->kind ? FX(0.7) : v->size;
            fx h = VP_PILLAR == v->kind ? v->size * 4 : v->size;
            use_material(VP_PILLAR == v->kind ? MAT_PILLAR : MAT_WALL);
            NE_PolyBegin(GL_QUADS);
            fx x0 = x - w / 2, x1 = x + w / 2, z0 = z - w / 2, z1 = z + w / 2;
            // Flat shaded: these sit outside the light grid.
            NE_PolyColor(RGB15(FX_TO_INT(31 * shade), FX_TO_INT(28 * shade),
                               FX_TO_INT(30 * shade)));
            NE_PolyTexCoord(0, 0);   vtx(x0, y + h, z1);
            NE_PolyTexCoord(16, 0);  vtx(x1, y + h, z1);
            NE_PolyTexCoord(16, 16); vtx(x1, y + h, z0);
            NE_PolyTexCoord(0, 16);  vtx(x0, y + h, z0);
            NE_PolyColor(RGB15(FX_TO_INT(24 * shade), FX_TO_INT(22 * shade),
                               FX_TO_INT(24 * shade)));
            NE_PolyTexCoord(16, 32); vtx(x1, y, z1);
            NE_PolyTexCoord(0, 32);  vtx(x1, y, z0);
            NE_PolyTexCoord(0, 0);   vtx(x1, y + h, z0);
            NE_PolyTexCoord(16, 0);  vtx(x1, y + h, z1);
            NE_PolyColor(RGB15(FX_TO_INT(17 * shade), FX_TO_INT(16 * shade),
                               FX_TO_INT(18 * shade)));
            NE_PolyTexCoord(0, 32);  vtx(x0, y, z1);
            NE_PolyTexCoord(16, 32); vtx(x1, y, z1);
            NE_PolyTexCoord(16, 0);  vtx(x1, y + h, z1);
            NE_PolyTexCoord(0, 0);   vtx(x0, y + h, z1);
            NE_PolyEnd();
            break;
        }
        case VP_BOULDER: {
            int slot = CLUT_BOULDER + v->frame;
            render_prop(SHEET_CLUTTER, slot, 0 != (v->phase & 1), x, y, z,
                        SPAL_NORMAL);
            break;
        }
        default:
            break;
        }
    }
}
