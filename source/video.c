// Video setup, the top-screen text layer, the logo and the HUD (video.h).

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <NEMain.h>

#include "art_layout.h"
#include "config.h"
#include "fixed.h"
#include "parallax.h"
#include "pixel.h"
#include "video.h"

// Backdrop colour, seen where every layer is transparent.
#define BACKDROP_COLOR RGB15(2, 1, 5)

static PrintConsole top_console;
static int top_text_bg;

// Custom BG1 tiles, after the console font.
enum {
    TILE_HEART_FULL = CUSTOM_TILE_FIRST,
    TILE_HEART_EMPTY,
    TILE_DASH_FULL,
    TILE_DASH_EMPTY,
    TILE_DAGGER_FULL,
    TILE_DAGGER_EMPTY,
    TILE_METER_FULL,
    TILE_METER_EMPTY,
    TILE_METER_READY,
    TILE_ARROW_DOWN,
    TILE_LOGO_FIRST,     // LOGO_TILES tiles of the title logo (art_logo.c)
};

static PixelTile heart_full = {
    ".11.11..",
    "1221221.",
    "1222221.",
    "1222231.",
    ".12231..",
    "..131...",
    "...1....",
    "........",
};
static PixelTile heart_empty = {
    ".11.11..",
    "1441441.",
    "1444441.",
    "1444441.",
    ".14441..",
    "..141...",
    "...1....",
    "........",
};
static PixelTile dash_full = {
    "........",
    "..1111..",
    ".155561.",
    ".155561.",
    ".156661.",
    "..1111..",
    "........",
    "........",
};
static PixelTile dash_empty = {
    "........",
    "..1111..",
    ".144441.",
    ".144441.",
    ".144441.",
    "..1111..",
    "........",
    "........",
};

static PixelTile dagger_full = {
    "......1.",
    ".....171",
    "....171.",
    "...171..",
    ".1171...",
    "..181...",
    ".181.1..",
    "..1.....",
};
static PixelTile dagger_empty = {
    "......1.",
    ".....141",
    "....141.",
    "...141..",
    ".1141...",
    "..141...",
    ".141.1..",
    "..1.....",
};
static PixelTile meter_full = {
    "........",
    "11111111",
    "99999999",
    "88888888",
    "88888888",
    "11111111",
    "........",
    "........",
};
static PixelTile meter_empty = {
    "........",
    "11111111",
    "44444444",
    "44444444",
    "44444444",
    "11111111",
    "........",
    "........",
};
static PixelTile meter_ready = {
    "........",
    "11111111",
    "66666666",
    "55555555",
    "55555555",
    "11111111",
    "........",
    "........",
};

// The choice banner's arrow toward the lower screen, in the font's style
// (body index 15, shadow 14 down and right) so it takes the text colour.
static PixelTile arrow_down = {
    "........",
    ".ffffff.",
    "..ffffee",
    "...ffee.",
    "....ee..",
    "........",
    "........",
    "........",
};

// The custom tiles and where they go.
static const PixelTileSlot custom_tiles[] = {
    { TILE_HEART_FULL, &heart_full },   { TILE_HEART_EMPTY, &heart_empty },
    { TILE_DASH_FULL, &dash_full },     { TILE_DASH_EMPTY, &dash_empty },
    { TILE_DAGGER_FULL, &dagger_full }, { TILE_DAGGER_EMPTY, &dagger_empty },
    { TILE_METER_FULL, &meter_full },   { TILE_METER_EMPTY, &meter_empty },
    { TILE_METER_READY, &meter_ready }, { TILE_ARROW_DOWN, &arrow_down },
};

static bool in_text_grid(int x, int y)
{
    return x >= 0 && y >= 0 && x < TEXT_COLS && y < TEXT_ROWS;
}

// Printable ASCII and char 127 (Code Genie's lamp, GLYPH_LAMP) have glyphs;
// anything else shows as a space.
static unsigned char glyph(char c)
{
    unsigned char u = (unsigned char)c;
    return u < ' ' || u > 127 ? ' ' : u;
}

void video_load_font(PrintConsole *con)
{
    // Overwrite the libnds font with Victoria Bold (art_font.c): same
    // layout (chars 32-127 from fontCharOffset), 4bpp, glyph = index 15,
    // shadow = index 14.
    u32 *gfx = (u32 *)bgGetGfxPtr(con->bgId) + con->fontCharOffset * 8;
    for (int i = 0; i < 96 * 8; i++)
        gfx[i] = art_font_tiles[i];
}

void video_retint_text(u16 *pal)
{
    // Console text uses index 15 of bank N for colour N, and index 14 for
    // the font's drop shadow.
    static const int banks[] = { TXT_NORMAL, TXT_DIM, TXT_GOLD, TXT_RED,
                                 TXT_GREEN, TXT_CYAN };
    for (int i = 0; i < ARRAY_LEN(banks); i++)
        pal[banks[i] * 16 + 14] = RGB15(1, 1, 3);
    pal[TXT_NORMAL * 16 + 15] = RGB15(30, 29, 24);
    pal[TXT_DIM * 16 + 15]    = RGB15(15, 14, 19);
    pal[TXT_GOLD * 16 + 15]   = RGB15(31, 25, 10);
    pal[TXT_RED * 16 + 15]    = RGB15(31, 11, 9);
    pal[TXT_GREEN * 16 + 15]  = RGB15(14, 28, 12);
    pal[TXT_CYAN * 16 + 15]   = RGB15(14, 27, 31);
}

static void init_top_text(void)
{
    consoleInit(&top_console, 1, BgType_Text4bpp, BgSize_T_256x256,
                MAIN_TXT_MAP_BASE, MAIN_TXT_TILE_BASE, true, true);
    top_text_bg = top_console.bgId;
    video_load_font(&top_console);
    bgSetPriority(top_text_bg, 0);

    video_retint_text(BG_PALETTE);

    u32 *gfx = (u32 *)bgGetGfxPtr(top_text_bg);
    for (int i = 0; i < LOGO_TILES * 8; i++)
        gfx[TILE_LOGO_FIRST * 8 + i] = art_logo_tiles[i];

    pixel_tiles4(gfx, custom_tiles,
                 ARRAY_LEN(custom_tiles));

    u16 *hud = BG_PALETTE + PAL_BANK_HUD * 16;
    hud[1] = RGB15(2, 1, 3);
    hud[2] = RGB15(29, 6, 8);
    hud[3] = RGB15(17, 2, 5);
    hud[4] = RGB15(7, 5, 10);
    hud[5] = RGB15(10, 24, 31);
    hud[6] = RGB15(24, 30, 31);
    hud[7] = RGB15(26, 27, 29);   // dagger steel
    hud[8] = RGB15(28, 20, 6);    // meter gold
    hud[9] = RGB15(31, 29, 16);   // meter highlight

    u16 *pal = BG_PALETTE + PAL_BANK_LOGO * 16;
    for (int i = 1; i < 16; i++)
        pal[i] = art_logo_pal[i];
}

void video_init(void)
{
    // Interrupt setup and NE init follow the Nitro Engine 3d_single_screen
    // template (nitro-engine v0.16.0).
    irqEnable(IRQ_HBLANK);
    irqSet(IRQ_VBLANK, NE_VBLFunc);
    irqSet(IRQ_HBLANK, NE_HBLFunc);

    NE_Init3D();
    // NE_Init3D() claims VRAM A-D for textures; keep A, B and D (384 KB),
    // leaving C for the sub engine's BG. Sub sprites live in VRAM I.
    NE_TextureSystemReset(0, 0, NE_VRAM_ABD);

    // Transparent clear colour (alpha 0) so the parallax BGs show behind 3D.
    NE_ClearColorSet(RGB15(0, 0, 0), 0, 63);

    vramSetBankF(VRAM_F_MAIN_BG_0x06000000);
    vramSetBankG(VRAM_G_MAIN_BG_0x06004000);
    bgSetPriority(0, 1);

    init_top_text();

    BG_PALETTE[0] = BACKDROP_COLOR;
}

void video_wait_vblank(void)
{
    NE_WaitForVBL(0);
    bgUpdate();
    parallax_vblank();
}

void video_top_clear(void)
{
    consoleSelect(&top_console);
    consoleClear();
}

void video_console_vprint(PrintConsole *con, int x, int y, TextColor c,
                          const char *fmt, va_list ap)
{
    char buf[TEXT_COLS + 1];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    consoleSelect(con);
    consoleSetCursor(con, x, y);
    consoleSetColor(con, (ConsoleColor)c);
    // Never let the console reach the bottom-right cell, it would scroll.
    int room = TEXT_COLS - x;
    if (TEXT_ROWS - 1 == y)
        room--;
    if ((int)strlen(buf) > room)
        buf[room > 0 ? room : 0] = '\0';
    fputs(buf, stdout);
}

void video_top_text(int x, int y, TextColor c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    video_console_vprint(&top_console, x, y, c, fmt, ap);
    va_end(ap);
}

static void console_print(PrintConsole *con, int x, int y, TextColor c,
                          const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    video_console_vprint(con, x, y, c, fmt, ap);
    va_end(ap);
}

void video_console_vcenter(PrintConsole *con, int y, TextColor c,
                           const char *fmt, va_list ap)
{
    char buf[TEXT_COLS + 1];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    int x = (TEXT_COLS - (int)strlen(buf)) / 2;
    console_print(con, imax(x, 0), y, c, "%s", buf);
}

void video_top_text_center(int y, TextColor c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    video_console_vcenter(&top_console, y, c, fmt, ap);
    va_end(ap);
}

// Same tile encoding as the libnds console (source/arm9/console.c).
void video_console_putc(PrintConsole *con, int x, int y, char c,
                        TextColor color)
{
    if (!in_text_grid(x, y))
        return;
    u16 tile = (u16)(glyph(c) + con->fontCharOffset - con->font.asciiOffset);
    con->fontBgMap[x + y * con->consoleWidth] = TILE_PALETTE(color) | tile;
}

void video_top_putc(int x, int y, char c, TextColor color)
{
    video_console_putc(&top_console, x, y, c, color);
}

void video_top_map_row(int map_row, TextColor color, const char *text)
{
    u16 *row = top_console.fontBgMap + (map_row & 31) * 32;
    int len = imin((int)strlen(text), TEXT_COLS);
    int x0 = (TEXT_COLS - len) / 2;
    int base = top_console.fontCharOffset - top_console.font.asciiOffset;
    for (int x = 0; x < 32; x++) {
        int i = x - x0;
        unsigned char c = glyph(i >= 0 && i < len ? text[i] : ' ');
        row[x] = (u16)(TILE_PALETTE(color) | (c + base));
    }
}

void video_top_arrow_down(int x, int y, TextColor color)
{
    if (!in_text_grid(x, y))
        return;
    top_console.fontBgMap[x + y * top_console.consoleWidth] =
        (u16)(TILE_PALETTE(color) | TILE_ARROW_DOWN);
}

void video_set_terminal(bool on)
{
    if (on) {
        bgHide(2);
        bgHide(3);
        BG_PALETTE[0] = RGB15(0, 0, 0);
    } else {
        bgShow(2);
        bgShow(3);
        BG_PALETTE[0] = BACKDROP_COLOR;
    }
}

void video_top_scroll(int y)
{
    bgSetScroll(top_text_bg, 0, y);
}

void video_logo_draw(int y)
{
    // The illustrated logo (tools/import_art.py), centred.
    const int x0 = (TEXT_COLS - LOGO_W) / 2;
    u16 *map = bgGetMapPtr(top_text_bg);
    const u16 pal = TILE_PALETTE(PAL_BANK_LOGO);
    for (int ty = 0; ty < LOGO_H; ty++)
        for (int tx = 0; tx < LOGO_W; tx++)
            map[(y + ty) * 32 + x0 + tx] =
                (u16)(TILE_LOGO_FIRST + art_logo_map[ty * LOGO_W + tx]) | pal;
}

void video_big_text(int y, const char *text)
{
    static const char glyphs[] = "0123456789.";
    int width = 0;
    for (const char *c = text; *c; c++) {
        const char *g = strchr(glyphs, *c);
        if (g)
            width += art_big_w[g - glyphs];
    }
    u16 *map = bgGetMapPtr(top_text_bg);
    const u16 pal = TILE_PALETTE(PAL_BANK_LOGO);
    int x = (TEXT_COLS - width) / 2;
    for (const char *c = text; *c; c++) {
        const char *g = strchr(glyphs, *c);
        if (!g)
            continue;
        int i = (int)(g - glyphs);
        const unsigned short *m = art_big_map + i * BIG_W * BIG_H;
        for (int ty = 0; ty < BIG_H; ty++)
            for (int tx = 0; tx < art_big_w[i]; tx++)
                map[(y + ty) * 32 + x + tx] =
                    (u16)(TILE_LOGO_FIRST + m[ty * BIG_W + tx]) | pal;
        x += art_big_w[i];
    }
}

void video_hud_draw(const Hud *h)
{
    u16 *map = bgGetMapPtr(top_text_bg);
    const u16 pal = TILE_PALETTE(PAL_BANK_HUD);
    // Row 0: hearts, dash charges, depth.
    int x = 1;
    for (int i = 0; i < h->max_hp && x < 20; i++, x++) {
        bool full = i < h->hp && !((h->blink & HUD_BLINK_HEART) && i == h->hp - 1);
        map[x] = (full ? TILE_HEART_FULL : TILE_HEART_EMPTY) | pal;
    }
    map[x++] = 0;
    for (int i = 0; i < h->dash_max && x < 24; i++, x++)
        map[x] = i < h->dash ? TILE_DASH_FULL | pal
               : (h->blink & HUD_BLINK_DASH) ? 0 : TILE_DASH_EMPTY | pal;
    while (x < 24)
        map[x++] = 0;
    if (h->depth > 0)
        video_top_text(24, 0, TXT_GOLD, "D%3d", h->depth);
    else
        video_top_text(24, 0, TXT_GOLD, "CAMP");
    video_top_putc(29, 0, h->genie ? GLYPH_LAMP : ' ', TXT_CYAN);

    // Row 1: daggers, then the blast meter (gold when ready).
    u16 *row = map + 32;
    x = 1;
    for (int i = 0; i < h->dagger_max && x < 8; i++, x++)
        row[x] = i < h->daggers ? TILE_DAGGER_FULL | pal
               : (h->blink & HUD_BLINK_DAGGER) ? 0 : TILE_DAGGER_EMPTY | pal;
    row[x++] = 0;
    bool ready = h->meter >= h->meter_max;
    for (int i = 0; i < h->meter_max && x < 20; i++, x++)
        row[x] = (h->blink & HUD_BLINK_METER) && !ready && i >= h->meter ? 0
               : (ready ? TILE_METER_READY
                  : i < h->meter ? TILE_METER_FULL : TILE_METER_EMPTY) | pal;
    while (x < 24)
        row[x++] = 0;
    if (ready)
        video_top_text(x - 3, 1, TXT_CYAN, " X!");
}
