// Bottom-screen UI (sub engine): text, framed panels, menus, touch.
//
// Coordinates are text cells (32x24, 8x8 px) unless noted.

#ifndef DELVE_UI_H
#define DELVE_UI_H

#include <stdbool.h>

#include "config.h"
#include "video.h"

typedef enum {
    UI_NONE,
    UI_MOVED,     // cursor moved
    UI_ACTIVATE,  // A or tap on the item
    UI_BACK,      // B or tap on the back button
    UI_LEFT,      // left on a value item
    UI_RIGHT,     // right on a value item
} UiAction;

typedef struct {
    const char *items[MENU_MAX_ITEMS];
    int count;
    int cursor;
    int x, y;      // first item text cell
    int w;         // highlight width in cells
    int step;      // rows between items
    int value_x;   // column where item values start, 0 if none
    bool has_back; // B / back button returns UI_BACK
    bool quiet_activate; // the caller plays the sound for UI_ACTIVATE
    int rows;      // items shown at once (0 = all); more scroll
    int top;       // first item shown when scrolling
} UiMenu;

// Sets up the sub engine: console, panel and backdrop layers, icons.
void ui_init(void);

// Clears text and panels (keeps the backdrop).
void ui_clear(void);

// printf-style text at a cell, or centred on a row (cut at the right edge).
void ui_text(int x, int y, TextColor c, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void ui_text_center(int y, TextColor c, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
// Blanks one text row.
void ui_clear_row(int y);

// Drifts the bottom screen's backdrop layers (bricks and motes). Once per
// frame.
void ui_backdrop_update(void);

// One character cell straight into the text map (see video_top_putc).
void ui_putc(int x, int y, char c, TextColor color);

// Plain terminal look: hides panels and the brick backdrop, black
// background. ui_set_terminal(false) restores them.
void ui_set_terminal(bool on);

// Framed panel covering the given cells, border included.
void ui_panel(int x, int y, int w, int h);

// Highlight bar behind one text row (inside a panel).
void ui_highlight(int x, int y, int w, bool on);

// Draws a "B Back" button in the bottom-left corner.
void ui_back_button(void);
bool ui_back_touched(void);

// A yes/no dialog over a cleared screen: the question in red, a dim note
// under it, then "A Yes" and "B No".
void ui_confirm_draw(const char *question, const char *note);
// UI_ACTIVATE for yes (A, or a tap on the left half), UI_BACK for no (B, or
// the right half), else UI_NONE. Plays no sound.
UiAction ui_confirm_update(void);

// 16x16 icons as sub-engine sprites (pixel coordinates).
typedef enum {
    ICON_COMBAT,
    ICON_REST,
    ICON_TREASURE,
    ICON_GOLD,
    ICON_GEAR,       // menu: Options
    ICON_BANNER,     // menu: Credits
    ICON_IDOL,       // menu: About
    ICON_SKULL,      // menu: Start Run
    ICON_COUNT
} UiIcon;

#define UI_ICON_SLOTS 8

// Shows an icon in sprite slot 0..UI_ICON_SLOTS-1; ui_icons_clear hides all.
void ui_icon(int slot, UiIcon icon, int x, int y);
void ui_icons_clear(void);
// Commits sprite changes; called once per frame by the main loop.
void ui_oam_update(void);

// A scrolling menu (rows > 0) marks hidden items with ^ / v in the column
// right of the highlight; its item names are padded to clear the row.
void ui_menu_draw(const UiMenu *m);
// Visible row of item i (scrolling menus), or -1 when scrolled out of view.
int ui_menu_row(const UiMenu *m, int i);
UiAction ui_menu_update(UiMenu *m);

#endif // DELVE_UI_H
