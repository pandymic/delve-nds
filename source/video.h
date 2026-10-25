// Hardware video setup and top-screen 2D helpers.
//
// Top screen  = main engine: Nitro Engine 3D (BG0), text/logo (BG1),
//               parallax (BG2, BG3). See config.h for the VRAM layout.
// Bottom screen = sub engine, owned by ui.c.

#ifndef DELVE_VIDEO_H
#define DELVE_VIDEO_H

#include <stdarg.h>
#include <stdbool.h>

#include <nds.h>

// Text colours. Values are libnds console palette banks; video_init() retints
// them to the game palette.
typedef enum {
    TXT_NORMAL = CONSOLE_WHITE,
    TXT_DIM    = CONSOLE_GRAY,
    TXT_GOLD   = CONSOLE_LIGHT_YELLOW,
    TXT_RED    = CONSOLE_LIGHT_RED,
    TXT_GREEN  = CONSOLE_LIGHT_GREEN,
    TXT_CYAN   = CONSOLE_LIGHT_CYAN,
} TextColor;

// Palette banks for custom tiles (indices 1-14 of any bank are free; index 15
// belongs to the console text colour).
#define PAL_BANK_STARS  0
#define PAL_BANK_FOG    1
#define PAL_BANK_LOGO   2
#define PAL_BANK_PANEL  3
#define PAL_BANK_HILITE 4
#define PAL_BANK_HUD    5

// Sets up the 3D engine, the main engine's VRAM banks and the top text layer.
void video_init(void);

// Shared text helpers for any libnds console (used by ui.c too).
void video_retint_text(u16 *pal);          // the game's text colours
void video_load_font(PrintConsole *con);   // the game's pixel font
// One character straight into the map (never scrolls).
void video_console_putc(PrintConsole *con, int x, int y, char c,
                        TextColor color);
// printf-style text at a cell, or centred on a row, cut so the console
// never reaches its last cell (which would scroll it).
void video_console_vprint(PrintConsole *con, int x, int y, TextColor c,
                          const char *fmt, va_list ap);
void video_console_vcenter(PrintConsole *con, int y, TextColor c,
                           const char *fmt, va_list ap);

// Waits for vblank and commits BG scroll registers. Call once per frame.
void video_wait_vblank(void);

// Top-screen text layer: clear it, print at a cell, or centred on a row.
void video_top_clear(void);
void video_top_text(int x, int y, TextColor c, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void video_top_text_center(int y, TextColor c, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Pixel scroll of the top text layer (used by credits).
void video_top_scroll(int y);

// Replaces one of the text layer's 32 map rows (0-31, wrapping; the screen
// shows 24 of them, from the scroll) with text centred in it. Writes
// straight into the map: for rows scrolled off-screen (credits).
void video_top_map_row(int map_row, TextColor color, const char *text);

// One character cell, written straight into the text map (no printf), for
// screens that redraw many cells at once. Never scrolls.
void video_top_putc(int x, int y, char c, TextColor color);

// A down arrow (toward the lower screen) in one cell, in a text colour.
// The font has no free glyph, so it is a custom tile.
void video_top_arrow_down(int x, int y, TextColor color);

// Plain terminal look for the Easter egg: hides the parallax layers and
// turns the backdrop black. video_set_terminal(false) restores them.
void video_set_terminal(bool on);

// Draws the block-letter DELVE logo with its top row at text row y.
// Occupies 8 rows (7 + shadow).
void video_logo_draw(int y);

// Draws digits and dots ("2.4") in the logo's style, BIG_H rows tall from
// text row y, centred. Other characters are skipped.
void video_big_text(int y, const char *text);

// Code Genie's lamp: font char 127, the mark of a cheated save.
#define GLYPH_LAMP '\x7f'

// In-run HUD on the top text layer, row 0: hearts, dash charges, depth.
typedef struct {
    int hp, max_hp;
    int dash, dash_max;
    int daggers, dagger_max;
    int meter, meter_max;
    int depth;
    bool genie;       // Code Genie cheats are on: the lamp after the depth
    unsigned blink;   // HUD_BLINK_*: drawn blank this frame (feedback)
} Hud;

enum {
    HUD_BLINK_HEART  = 1 << 0,   // the last full heart (one heart left)
    HUD_BLINK_DASH   = 1 << 1,   // empty dash slots (dash with none left)
    HUD_BLINK_DAGGER = 1 << 2,   // empty dagger slots
    HUD_BLINK_METER  = 1 << 3,   // the meter (blast before it is full)
};

// In-run HUD on the top screen's first two text rows.
void video_hud_draw(const Hud *h);

#endif // DELVE_VIDEO_H
