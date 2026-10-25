// Procedural parallax sky on the main engine's BG2 and BG3, scrolled per
// scanline by HBlank DMA (parallax.h).

#include <nds.h>

#include "config.h"
#include "fixed.h"
#include "parallax.h"
#include "pixel.h"
#include "rng.h"
#include "video.h"

static int bg_near;
static int bg_far;
static int32_t drift_x, drift_y; // 24.8 fixed
static int cam_x, cam_y;
static int ripple_t;
static bool shimmer;   // heat haze: a stronger, faster wave on both layers

// Scroll per scanline (HOFS | VOFS << 16), read by HBlank DMA. Line 0 is
// written at vblank; the DMA feeds lines 1-191 from [1].
static u32 far_lines[SCREEN_H];
static u32 near_lines[SCREEN_H];
static bool lines_ready;

#define DMA_FAR  1
#define DMA_NEAR 2

// Parallax tile set (shared by both layers, char base MAIN_PLX_TILE_BASE).
enum {
    PT_EMPTY = 0,
    PT_STAR_DIM,
    PT_STAR_SMALL,
    PT_STAR_WARM,
    PT_STAR_BIG,
    PT_FOG_1, // dither levels 1..4
    PT_FOG_2,
    PT_FOG_3,
    PT_FOG_4,
    PT_COUNT
};

static PixelTile star_dim = {
    "........", "........", "........", "...1....",
    "........", "........", "........", "........",
};
static PixelTile star_small = {
    "........", "........", "........", "........",
    "....2...", "........", "........", "........",
};
static PixelTile star_warm = {
    "........", ".3......", "........", "........",
    "........", "........", "........", "........",
};
static PixelTile star_big = {
    "........", "........", "...1....", "..141...",
    "...1....", "........", "........", "........",
};

// Smooth value noise on a wrapping 32x32 grid, 0..255.
static uint8_t lattice[8][8];

static int noise_at(int x, int y)
{
    // Lattice cell is 4x4 tiles; bilinear interpolation, wraps at 32.
    int cx = x >> 2, cy = y >> 2;
    int fx = x & 3, fy = y & 3;
    int a = lattice[cy & 7][cx & 7];
    int b = lattice[cy & 7][(cx + 1) & 7];
    int c = lattice[(cy + 1) & 7][cx & 7];
    int d = lattice[(cy + 1) & 7][(cx + 1) & 7];
    int top = a * (4 - fx) + b * fx;
    int bot = c * (4 - fx) + d * fx;
    return (top * (4 - fy) + bot * fy) >> 4;
}

void parallax_init(uint32_t seed)
{
    Rng rng;
    rng_seed(&rng, seed);

    bg_near = bgInit(2, BgType_Text4bpp, BgSize_T_256x256,
                     MAIN_NEAR_MAP_BASE, MAIN_PLX_TILE_BASE);
    bg_far = bgInit(3, BgType_Text4bpp, BgSize_T_256x256,
                    MAIN_FAR_MAP_BASE, MAIN_PLX_TILE_BASE);
    bgSetPriority(bg_near, 2);
    bgSetPriority(bg_far, 3);

    static const PixelTileSlot stars[] = {
        { PT_STAR_DIM, &star_dim },   { PT_STAR_SMALL, &star_small },
        { PT_STAR_WARM, &star_warm }, { PT_STAR_BIG, &star_big },
    };
    u32 *gfx = (u32 *)bgGetGfxPtr(bg_far);
    pixel_tile4_fill(gfx + PT_EMPTY * 8, 0);
    pixel_tiles4(gfx, stars, ARRAY_LEN(stars));
    for (int i = 0; i < 4; i++)
        pixel_tile4_dither(gfx + (PT_FOG_1 + i) * 8, 0, 1, (unsigned)(i + 1));

    // Index 4 (big stars' centres) is white in every theme.
    BG_PALETTE[PAL_BANK_STARS * 16 + 4] = RGB15(31, 31, 31);
    parallax_set_theme(0);

    // Far layer: sparse stars.
    u16 *map = bgGetMapPtr(bg_far);
    const u16 star_pal = TILE_PALETTE(PAL_BANK_STARS);
    for (int i = 0; i < 32 * 32; i++) {
        uint32_t r = rng_range(&rng, 100);
        u16 t = PT_EMPTY;
        if (r < 6)
            t = PT_STAR_DIM;
        else if (r < 9)
            t = PT_STAR_SMALL;
        else if (r < 10)
            t = PT_STAR_WARM;
        else if (r < 11)
            t = PT_STAR_BIG;
        // Random flips multiply the variety for free.
        if (rng_chance(&rng, 50))
            t |= TILE_FLIP_H;
        if (rng_chance(&rng, 50))
            t |= TILE_FLIP_V;
        map[i] = t | star_pal;
    }

    // Near layer: dithered fog banks from value noise.
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            lattice[y][x] = (uint8_t)rng_range(&rng, 256);

    map = bgGetMapPtr(bg_near);
    const u16 fog_pal = TILE_PALETTE(PAL_BANK_FOG);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int n = noise_at(x, y);
            int level = (n - 110) / 28; // most of the sky stays clear
            u16 t = PT_EMPTY;
            if (level >= 1)
                t = (u16)(PT_FOG_1 + imin(level, 4) - 1);
            map[y * 32 + x] = t | fog_pal;
        }
    }

    drift_x = drift_y = 0;
    cam_x = cam_y = 0;
}

// Star bands, top to bottom: from far and slow to near and fast, in 1/16.
static const uint8_t band_speed[PLX_BANDS] = { 3, 5, 7, 9, 11, 13, 16, 20 };
// Fog ripple: a small sideways wave down the screen, in pixels.
static const int8_t wave[16] = { 0, 1, 1, 2, 2, 2, 1, 1, 0, -1, -1, -2, -2, -2, -1, -1 };

static void apply_scroll(void)
{
    int fx = (drift_x >> 8) + cam_x / 4;
    int fy = (drift_y >> 8) + cam_y / 4;
    int nx = ((drift_x * 3) >> 8) + cam_x / 2;
    int ny = ((drift_y * 3) >> 8) + cam_y / 2;
    bgSetScroll(bg_far, fx, fy);
    bgSetScroll(bg_near, nx, ny);

    // Per-line tables. Each band of stars scrolls by its own fraction of
    // the base offset; band edges are invisible in a sparse starfield.
    int bx = (drift_x >> 4) + cam_x * 4;   // base, in 1/16 px
    int by = (drift_y >> 4) + cam_y * 4;
    int band_h = SCREEN_H / PLX_BANDS;
    for (int y = 0; y < SCREEN_H; y++) {
        int k = band_speed[y / band_h];
        int haze = shimmer ? wave[((y >> 2) + (ripple_t >> 1)) & 15] : 0;
        u32 h = (u32)(((bx * k) >> 8) + haze) & 0x1FF;
        u32 v = (u32)((by * k) >> 8) & 0x1FF;
        far_lines[y] = h | (v << 16);
        // The wave moves down the screen one step every 8 frames (heat
        // haze: faster, doubled).
        int w = shimmer ? 2 * haze : wave[((y >> 3) + (ripple_t >> 3)) & 15];
        u32 nh = (u32)(nx + w) & 0x1FF;
        near_lines[y] = nh | ((u32)ny & 0x1FF) << 16;
    }
    // The DMA reads main RAM directly: push the tables out of the cache.
    DC_FlushRange(far_lines, sizeof(far_lines));
    DC_FlushRange(near_lines, sizeof(near_lines));
    lines_ready = true;
}

// Writes line 0's scroll now and has DMA channel ch feed one word per
// HBlank from lines[1] onwards into the layer's HOFS/VOFS pair.
static void start_line_dma(int ch, const u32 *lines, vu16 *hofs)
{
    *(vu32 *)hofs = lines[0];
    DMA_SRC(ch) = (u32)&lines[1];
    DMA_DEST(ch) = (u32)hofs;
    DMA_CR(ch) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_32_BIT
               | DMA_SRC_INC | DMA_DST_FIX | 1;
}

void parallax_vblank(void)
{
    DMA_CR(DMA_FAR) = 0;
    DMA_CR(DMA_NEAR) = 0;
    if (!lines_ready)
        return;
    start_line_dma(DMA_FAR, far_lines, &REG_BG3HOFS);
    start_line_dma(DMA_NEAR, near_lines, &REG_BG2HOFS);
}

// Sky themes: star tints and fog colour, darker deeper down.
typedef struct {
    u16 star_dim, star_small, star_warm, fog, backdrop;
} Theme;

static const Theme themes[PLX_THEMES] = {
    // Indigo (depths 1-5), the original look.
    { RGB15(10, 10, 18), RGB15(24, 24, 30), RGB15(30, 22, 12), RGB15(6, 4, 11),
      RGB15(2, 1, 5) },
    // Deep teal (6-10).
    { RGB15(6, 14, 16), RGB15(18, 28, 28), RGB15(28, 26, 14), RGB15(2, 8, 10),
      RGB15(0, 3, 4) },
    // Ember (11-15).
    { RGB15(16, 8, 8), RGB15(30, 22, 18), RGB15(31, 16, 6), RGB15(10, 3, 3),
      RGB15(4, 1, 1) },
    // Moss (16-20).
    { RGB15(8, 14, 8), RGB15(20, 28, 18), RGB15(28, 28, 10), RGB15(3, 8, 4),
      RGB15(1, 3, 1) },
    // Violet abyss (21+).
    { RGB15(14, 8, 18), RGB15(28, 20, 31), RGB15(31, 18, 26), RGB15(8, 2, 12),
      RGB15(3, 0, 5) },
    // Night over the camp: clear, deep blue, bright stars.
    { RGB15(14, 16, 24), RGB15(28, 30, 31), RGB15(31, 28, 18), RGB15(3, 5, 12),
      RGB15(1, 2, 7) },
    // Crystal cave: cyan glints in a blue-black dark.
    { RGB15(6, 14, 18), RGB15(16, 30, 31), RGB15(24, 31, 31), RGB15(1, 6, 9),
      RGB15(0, 2, 4) },
    // Deep cave: violet glints, darker still.
    { RGB15(10, 6, 16), RGB15(22, 18, 31), RGB15(30, 20, 31), RGB15(4, 2, 8),
      RGB15(1, 0, 3) },
    // Inferno: embers on a red glow.
    { RGB15(24, 10, 4), RGB15(31, 22, 8), RGB15(31, 28, 14), RGB15(12, 3, 1),
      RGB15(6, 1, 0) },
    // Ash: dull red-brown smoke.
    { RGB15(16, 8, 6), RGB15(28, 16, 10), RGB15(31, 20, 8), RGB15(7, 3, 2),
      RGB15(3, 1, 1) },
};

void parallax_set_shimmer(bool on)
{
    shimmer = on;
}

void parallax_set_theme(int theme)
{
    const Theme *t = &themes[iclamp(theme, 0, PLX_THEMES - 1)];
    u16 *pal = BG_PALETTE + PAL_BANK_STARS * 16;
    pal[1] = t->star_dim;
    pal[2] = t->star_small;
    pal[3] = t->star_warm;
    BG_PALETTE[PAL_BANK_FOG * 16 + 1] = t->fog;
    BG_PALETTE[0] = t->backdrop;
}

void parallax_update(int speed_x, int speed_y)
{
    ripple_t++;
    // Wrap at 256 px (in 24.8) so the values never overflow.
    drift_x = (drift_x + speed_x) & ((256 << 8) - 1);
    drift_y = (drift_y + speed_y) & ((256 << 8) - 1);
    apply_scroll();
}

void parallax_set_camera(int x, int y)
{
    cam_x = x;
    cam_y = y;
    apply_scroll();
}

