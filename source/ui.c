// Bottom-screen UI on the sub engine: text, panels, menus, icons (ui.h).

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "audio.h"
#include "input.h"
#include "pixel.h"
#include "ui.h"
#include "ui_icons.h"

static PrintConsole sub_console;
static u16 *icon_gfx[ICON_COUNT];
static int bg_panel;
static int bg_back;
static int bg_motes;
static int32_t back_scroll;   // 24.8, shared drift for both layers

enum {
    UT_EMPTY = 0,
    UT_CORNER,
    UT_EDGE_T,
    UT_EDGE_L,
    UT_FILL,
    UT_HL_FILL,
    UT_HL_CAP,
    UT_BRICK,
    UT_MOTE_1,
    UT_MOTE_2,
};

static PixelTile t_mote_1 = {
    "........", "........", "........", "...7....",
    "........", "........", "........", "........",
};
static PixelTile t_mote_2 = {
    "........", ".....8..", "....878.", ".....8..",
    "........", "........", "........", "........",
};

// Panel frame: a double gold line with a small red gem at each corner.
static PixelTile t_corner = {
    "..111111",
    ".1222222",
    "12ba9999",
    "12aa9333",
    "12993444",
    "12934444",
    "12934444",
    "12934444",
};
static PixelTile t_edge_t = {
    "11111111",
    "22222222",
    "99999999",
    "33333333",
    "44444444",
    "44444444",
    "44444444",
    "44444444",
};
static PixelTile t_edge_l = {
    "12934444",
    "12934444",
    "12934444",
    "12934444",
    "12934444",
    "12934444",
    "12934444",
    "12934444",
};
static PixelTile t_hl_cap = {
    "44444444",
    "44111111",
    "41222222",
    "41222222",
    "41222222",
    "41222222",
    "44333333",
    "44444444",
};
static PixelTile t_hl_fill = {
    "44444444",
    "11111111",
    "22222222",
    "22222222",
    "22222222",
    "22222222",
    "33333333",
    "44444444",
};
static PixelTile t_brick = {
    "66666666",
    "55555565",
    "55555565",
    "55555565",
    "66666666",
    "55655555",
    "55655555",
    "55655555",
};

#define PANEL(t)  ((u16)((t) | TILE_PALETTE(PAL_BANK_PANEL)))
#define HILITE(t) ((u16)((t) | TILE_PALETTE(PAL_BANK_HILITE)))

void ui_init(void)
{
    videoSetModeSub(MODE_0_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankI(VRAM_I_SUB_SPRITE);   // 16 KB is plenty for the icons

    consoleInit(&sub_console, 0, BgType_Text4bpp, BgSize_T_256x256,
                SUB_TXT_MAP_BASE, SUB_TXT_TILE_BASE, false, true);
    bgSetPriority(sub_console.bgId, 0);
    video_retint_text(BG_PALETTE_SUB);
    video_load_font(&sub_console);

    bg_panel = bgInitSub(1, BgType_Text4bpp, BgSize_T_256x256,
                         SUB_PANEL_MAP_BASE, SUB_PANEL_TILE_BASE);
    bg_back = bgInitSub(2, BgType_Text4bpp, BgSize_T_256x256,
                        SUB_BACK_MAP_BASE, SUB_PANEL_TILE_BASE);
    // Motes drift over the bricks at a different speed (parallax).
    bg_motes = bgInitSub(3, BgType_Text4bpp, BgSize_T_256x256,
                         SUB_MOTES_MAP_BASE, SUB_PANEL_TILE_BASE);
    bgSetPriority(bg_panel, 1);
    bgSetPriority(bg_motes, 2);
    bgSetPriority(bg_back, 3);

    static const PixelTileSlot tiles[] = {
        { UT_CORNER, &t_corner },   { UT_EDGE_T, &t_edge_t },
        { UT_EDGE_L, &t_edge_l },   { UT_HL_FILL, &t_hl_fill },
        { UT_HL_CAP, &t_hl_cap },   { UT_BRICK, &t_brick },
        { UT_MOTE_1, &t_mote_1 },   { UT_MOTE_2, &t_mote_2 },
    };
    u32 *gfx = (u32 *)bgGetGfxPtr(bg_panel);
    pixel_tile4_fill(gfx + UT_EMPTY * 8, 0);
    pixel_tile4_fill(gfx + UT_FILL * 8, 4);
    pixel_tiles4(gfx, tiles, ARRAY_LEN(tiles));

    u16 *pal = BG_PALETTE_SUB + PAL_BANK_PANEL * 16;
    pal[1] = RGB15(2, 1, 4);
    pal[2] = RGB15(21, 15, 6);
    pal[3] = RGB15(9, 6, 12);
    pal[4] = RGB15(5, 4, 9);
    pal[5] = RGB15(4, 3, 7);
    pal[6] = RGB15(2, 2, 4);
    pal[7] = RGB15(9, 7, 14);   // motes
    pal[8] = RGB15(14, 11, 20);
    pal[9] = RGB15(13, 8, 3);   // frame: dark gold
    pal[10] = RGB15(28, 6, 8);  // corner gem
    pal[11] = RGB15(31, 22, 22);

    // Highlight: index 4 matches the panel fill so caps blend in.
    pal = BG_PALETTE_SUB + PAL_BANK_HILITE * 16;
    pal[1] = RGB15(20, 12, 26);
    pal[2] = RGB15(13, 7, 19);
    pal[3] = RGB15(8, 4, 12);
    pal[4] = RGB15(5, 4, 9);

    BG_PALETTE_SUB[0] = RGB15(1, 1, 3);

    u16 *map = bgGetMapPtr(bg_back);
    for (int i = 0; i < 32 * 32; i++)
        map[i] = PANEL(UT_BRICK);
    // A sparse, fixed pattern of motes (hash of the cell, no RNG state).
    map = bgGetMapPtr(bg_motes);
    for (int i = 0; i < 32 * 32; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        int r = (int)((h >> 24) % 100);
        map[i] = r < 5 ? PANEL(UT_MOTE_1) : r < 7 ? PANEL(UT_MOTE_2) : 0;
    }

    // Icon sprites (libnds OAM API; same pattern as the BlocksDS
    // graphics_2d sprite examples).
    oamInit(&oamSub, SpriteMapping_1D_32, false);
    for (int i = 0; i < ICON_COUNT; i++) {
        icon_gfx[i] = oamAllocateGfx(&oamSub, SpriteSize_16x16,
                                     SpriteColorFormat_16Color);
        dmaCopy((const u8 *)ui_iconsTiles + i * 128, icon_gfx[i], 128);
    }
    dmaCopy(ui_iconsPal, SPRITE_PALETTE_SUB, 32);

    ui_clear();
}

void ui_icon(int slot, UiIcon icon, int x, int y)
{
    oamSet(&oamSub, slot, x, y, 0, 0, SpriteSize_16x16,
           SpriteColorFormat_16Color, icon_gfx[icon], -1, false, false,
           false, false, false);
}

void ui_icons_clear(void)
{
    for (int i = 0; i < UI_ICON_SLOTS; i++)
        oamSetHidden(&oamSub, i, true);
}

void ui_oam_update(void)
{
    oamUpdate(&oamSub);
}

void ui_clear(void)
{
    ui_icons_clear();
    consoleSelect(&sub_console);
    consoleClear();
    u16 *map = bgGetMapPtr(bg_panel);
    for (int i = 0; i < 32 * 32; i++)
        map[i] = 0;
}

void ui_backdrop_update(void)
{
    back_scroll += 40;   // 24.8: the bricks creep, the motes drift faster
    int b = back_scroll >> 8;
    bgSetScroll(bg_back, b / 4, b / 8);
    bgSetScroll(bg_motes, b, b / 2);
}

void ui_putc(int x, int y, char c, TextColor color)
{
    video_console_putc(&sub_console, x, y, c, color);
}

void ui_set_terminal(bool on)
{
    static u16 backdrop;
    if (on) {
        backdrop = BG_PALETTE_SUB[0];
        bgHide(bg_panel);
        bgHide(bg_back);
        bgHide(bg_motes);
        BG_PALETTE_SUB[0] = RGB15(0, 0, 0);
    } else {
        bgShow(bg_panel);
        bgShow(bg_back);
        bgShow(bg_motes);
        if (backdrop)
            BG_PALETTE_SUB[0] = backdrop;
    }
}

void ui_text(int x, int y, TextColor c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    video_console_vprint(&sub_console, x, y, c, fmt, ap);
    va_end(ap);
}

void ui_text_center(int y, TextColor c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    video_console_vcenter(&sub_console, y, c, fmt, ap);
    va_end(ap);
}

void ui_clear_row(int y)
{
    ui_text(0, y, TXT_NORMAL, "%*s", TEXT_ROWS - 1 == y ? 31 : 32, "");
}

void ui_panel(int x, int y, int w, int h)
{
    u16 *map = bgGetMapPtr(bg_panel);
    for (int ty = y; ty < y + h; ty++) {
        for (int tx = x; tx < x + w; tx++) {
            bool l = tx == x, r = tx == x + w - 1;
            bool t = ty == y, b = ty == y + h - 1;
            u16 tile;
            if ((l || r) && (t || b))
                tile = UT_CORNER;
            else if (t || b)
                tile = UT_EDGE_T;
            else if (l || r)
                tile = UT_EDGE_L;
            else
                tile = UT_FILL;
            if (r)
                tile |= TILE_FLIP_H;
            if (b)
                tile |= TILE_FLIP_V;
            map[ty * 32 + tx] = PANEL(tile);
        }
    }
}

void ui_highlight(int x, int y, int w, bool on)
{
    u16 *map = bgGetMapPtr(bg_panel);
    for (int tx = x; tx < x + w; tx++) {
        u16 tile;
        if (!on)
            tile = PANEL(UT_FILL);
        else if (tx == x)
            tile = HILITE(UT_HL_CAP);
        else if (tx == x + w - 1)
            tile = HILITE(UT_HL_CAP | TILE_FLIP_H);
        else
            tile = HILITE(UT_HL_FILL);
        map[y * 32 + tx] = tile;
    }
}

#define BACK_X 1
#define BACK_Y 21
#define BACK_W 8

void ui_back_button(void)
{
    ui_panel(BACK_X, BACK_Y, BACK_W, 3);
    ui_text(BACK_X + 1, BACK_Y + 1, TXT_GOLD, "B");
    ui_text(BACK_X + 3, BACK_Y + 1, TXT_NORMAL, "Back");
}

bool ui_back_touched(void)
{
    return input_touched_rect(BACK_X * 8, BACK_Y * 8, BACK_W * 8, 3 * 8);
}

void ui_confirm_draw(const char *question, const char *note)
{
    ui_clear();
    ui_panel(4, 7, 24, 9);
    ui_text_center(9, TXT_RED, "%s", question);
    ui_text_center(11, TXT_DIM, "%s", note);
    ui_text(7, 13, TXT_GOLD, "A");
    ui_text(9, 13, TXT_NORMAL, "Yes");
    ui_text(18, 13, TXT_GOLD, "B");
    ui_text(20, 13, TXT_NORMAL, "No");
}

UiAction ui_confirm_update(void)
{
    // The dialog's left half is Yes, its right half No.
    if ((input.down & KEY_A)
        || input_touched_rect(4 * 8, 12 * 8, 12 * 8, 3 * 8))
        return UI_ACTIVATE;
    if ((input.down & KEY_B)
        || input_touched_rect(16 * 8, 12 * 8, 12 * 8, 3 * 8))
        return UI_BACK;
    return UI_NONE;
}

static int menu_rows(const UiMenu *m)
{
    return m->rows > 0 && m->rows < m->count ? m->rows : m->count;
}

int ui_menu_row(const UiMenu *m, int i)
{
    int r = i - (menu_rows(m) < m->count ? m->top : 0);
    return r >= 0 && r < menu_rows(m) ? m->y + r * m->step : -1;
}

void ui_menu_draw(const UiMenu *m)
{
    bool scrolls = menu_rows(m) < m->count;
    for (int i = 0; i < m->count; i++) {
        int y = ui_menu_row(m, i);
        if (y < 0)
            continue;
        bool sel = i == m->cursor;
        ui_highlight(m->x - 1, y, m->w, sel);
        ui_text(m->x, y, sel ? TXT_GOLD : TXT_NORMAL, "%-*s",
                scrolls ? m->w - 2 : 0, m->items[i]);
    }
    if (scrolls) {
        int x = m->x - 1 + m->w;
        int last = m->y + (menu_rows(m) - 1) * m->step;
        ui_text(x, m->y, TXT_DIM, "%c", m->top > 0 ? '^' : ' ');
        ui_text(x, last, TXT_DIM, "%c",
                m->top + menu_rows(m) < m->count ? 'v' : ' ');
    }
}

// Scrolls so the cursor is in view.
static void menu_follow(UiMenu *m)
{
    int rows = menu_rows(m);
    if (m->cursor < m->top)
        m->top = m->cursor;
    else if (m->cursor >= m->top + rows)
        m->top = m->cursor - rows + 1;
}

static int menu_item_at(const UiMenu *m, int px, int py)
{
    int left = (m->x - 1) * 8;
    int right = left + m->w * 8;
    if (px < left || px >= right)
        return -1;
    for (int i = 0; i < m->count; i++) {
        int y = ui_menu_row(m, i);
        if (y < 0)
            continue;
        // Each item's touch area spans its whole step, centred on its row.
        int top = y * 8 - (m->step - 1) * 4;
        if (py >= top && py < top + m->step * 8)
            return i;
    }
    return -1;
}

UiAction ui_menu_update(UiMenu *m)
{
    int old = m->cursor;
    UiAction act = UI_NONE;

    if (input.repeat & KEY_UP)
        m->cursor = (m->cursor + m->count - 1) % m->count;
    else if (input.repeat & KEY_DOWN)
        m->cursor = (m->cursor + 1) % m->count;
    else if (m->value_x && (input.repeat & KEY_LEFT))
        act = UI_LEFT;
    else if (m->value_x && (input.repeat & KEY_RIGHT))
        act = UI_RIGHT;
    else if (input.down & KEY_A)
        act = UI_ACTIVATE;
    else if (m->has_back && ((input.down & KEY_B) || ui_back_touched()))
        act = UI_BACK;
    else if (input.touch_down) {
        int i = menu_item_at(m, input.touch_x, input.touch_y);
        if (i >= 0) {
            m->cursor = i;
            if (m->value_x && input.touch_x >= m->value_x * 8) {
                int mid = (m->value_x + (m->x - 1 + m->w)) * 4;
                act = input.touch_x < mid ? UI_LEFT : UI_RIGHT;
            } else {
                act = UI_ACTIVATE;
            }
        }
    }

    if (m->cursor != old) {
        menu_follow(m);
        ui_menu_draw(m);
        if (UI_NONE == act)
            act = UI_MOVED;
    }
    if (UI_BACK == act)
        audio_sfx(SND_BACK);
    else if (UI_NONE != act && !(UI_ACTIVATE == act && m->quiet_activate))
        audio_sfx(SND_SELECT);
    return act;
}
