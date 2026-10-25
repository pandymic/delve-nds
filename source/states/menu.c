// Main menu: Start Run / Upgrades / History / Options / Credits / About.

#include "../game.h"
#include "../input.h"
#include "../ui.h"
#include "../audio.h"
#include "states.h"

enum {
    ITEM_START, ITEM_UPGRADES, ITEM_HISTORY, ITEM_OPTIONS, ITEM_CREDITS,
    ITEM_ABOUT
};

static UiMenu menu = {
    .items = { "Start Run", "Upgrades", "History", "Options", "Credits",
               "About" },
    .count = 6,
    .x = 10, .y = 4, .w = 14, .step = 3,
    .has_back = true,
};

static void menu_enter(void)
{
    audio_music(MUSIC_MENU);
    scene_menu_top(true);
    scene_draw_stats(12, &game.meta);

    ui_clear();
    ui_panel(6, 1, 20, 20);
    ui_text_center(2, TXT_DIM, "- DELVE -");
    ui_menu_draw(&menu);
    // An icon beside each item.
    static const UiIcon icons[] = { ICON_COMBAT, ICON_TREASURE, ICON_SKULL,
                                    ICON_GEAR, ICON_BANNER, ICON_IDOL };
    for (int i = 0; i < menu.count; i++)
        ui_icon(i, icons[i], (menu.x - 3) * 8, (menu.y + i * menu.step) * 8 - 4);
    ui_back_button();
    if (!game.storage_ok) {
        ui_text(10, 22, TXT_RED, "No SD card: not saved");
    }
}

static void menu_update(void)
{
    scene_menu_update();

    switch (ui_menu_update(&menu)) {
    case UI_ACTIVATE:
        switch (menu.cursor) {
        case ITEM_START:    state_set(ST_PLAY); break;
        case ITEM_UPGRADES: state_set(ST_UPGRADES); break;
        case ITEM_HISTORY:  state_set(ST_HISTORY); break;
        case ITEM_OPTIONS:  state_set(ST_OPTIONS); break;
        case ITEM_CREDITS:  state_set(ST_CREDITS); break;
        case ITEM_ABOUT:    state_set(ST_ABOUT); break;
        }
        break;
    case UI_BACK:
        state_set(ST_TITLE);
        break;
    default:
        break;
    }
}

const State state_menu = {
    .name = "menu",
    .enter = menu_enter,
    .update = menu_update,
};
