// Upgrades shop: spend meta-currency on permanent upgrades (meta.c).

#include <stdio.h>

#include "../cheat.h"
#include "../game.h"
#include "../hint.h"
#include "../input.h"
#include "../meta.h"
#include "../save.h"
#include "../ui.h"
#include "../audio.h"
#include "states.h"

// Each path's steps together, paths by their first step's cost.
static const UpgradeId order[UPG_COUNT] = {
    UPG_VITALITY, UPG_VITALITY2,
    UPG_SWIFT_DASH, UPG_QUIVER,
    UPG_HEAVY_BLADE, UPG_KEEN_EDGE,
    UPG_IRON_HIDE, UPG_THORNS, UPG_MENDING,
    UPG_DEEP_POCKETS,
    UPG_FLETCHING, UPG_BARBED_TIPS, UPG_HUNTERS_MARK,
    UPG_KINDLING, UPG_SECOND_WIND,
};

static UiMenu menu = {
    .count = UPG_COUNT,
    .x = 4, .y = 3, .w = 25, .step = 2,
    .has_back = true,
    .quiet_activate = true,   // try_buy: a purchase or a buzz
    .rows = 9,                // scrolls
};

// The line right of the Back button: the description, or a message.
#define INFO_X 10
#define INFO_Y 22

// Code Genie's all upgrades: everything shows as owned and nothing
// can be bought; what was really bought is untouched.
static bool genie(void)
{
    return cheat_on(cheat_active, CHEAT_UPGRADES);
}

static bool locked(UpgradeId id)
{
    return !genie() && !meta_prereq_met(&game.meta, id);
}

static void info_line(TextColor c, const char *text)
{
    ui_text(INFO_X, INFO_Y, c, "%-22s", text);
}

static void draw_info(void)
{
    ui_icon(0, ICON_GOLD, 20 * 8, 1 * 8 - 4);
    ui_text(23, 1, TXT_GOLD, "%-6lu", (unsigned long)game.meta.currency);

    for (int i = 0; i < UPG_COUNT; i++) {
        UpgradeId id = order[i];
        int y = ui_menu_row(&menu, i);
        if (y < 0)
            continue;
        TextColor c = i == menu.cursor ? TXT_GOLD : TXT_NORMAL;
        if (genie())
            ui_text(20, y, TXT_CYAN, "  genie");
        else if (meta_owned(&game.meta, id))
            ui_text(20, y, TXT_GREEN, "  owned");
        else if (locked(id))
            ui_text(20, y, TXT_DIM, " locked");
        else if (game.meta.currency < upgrade_defs[id].cost)
            ui_text(20, y, TXT_RED, "%5lu g", (unsigned long)upgrade_defs[id].cost);
        else
            ui_text(20, y, c, "%5lu g", (unsigned long)upgrade_defs[id].cost);
    }
    UpgradeId id = order[menu.cursor];
    if (locked(id)) {
        char need[32];
        snprintf(need, sizeof(need), "Needs %s",
                 upgrade_defs[upgrade_defs[id].requires].name);
        info_line(TXT_DIM, need);
    } else {
        info_line(TXT_DIM, upgrade_defs[id].desc);
    }
}

static void upgrades_enter(void)
{
    if (ST_PLAY != state_previous())   // the camp's shrine keeps its music
        audio_music(MUSIC_MENU);
    for (int i = 0; i < UPG_COUNT; i++)
        menu.items[i] = upgrade_defs[order[i]].name;
    menu.cursor = 0;
    menu.top = 0;
    hint_mark_seen(&game.meta, HINT_SHRINE);   // found the shop: no tip

    ui_clear();
    ui_panel(2, 0, 28, 21);
    ui_text(4, 1, TXT_DIM, "- UPGRADES -");
    ui_menu_draw(&menu);
    draw_info();
    ui_back_button();
}

static void try_buy(int i)
{
    UpgradeId id = order[i];
    if (genie()) {
        audio_sfx(SND_DENY);
        info_line(TXT_CYAN, "Code Genie: all owned");
        return;
    }
    BuyResult r = meta_buy(&game.meta, id);
    draw_info();
    char msg[32];
    switch (r) {
    case BUY_OWNED:
        audio_sfx(SND_DENY);
        info_line(TXT_DIM, "Already owned");
        break;
    case BUY_TOO_POOR:
        audio_sfx(SND_DENY);
        snprintf(msg, sizeof(msg), "Need %lu more gold",
                 (unsigned long)(upgrade_defs[id].cost - game.meta.currency));
        info_line(TXT_RED, msg);
        break;
    case BUY_LOCKED:
        audio_sfx(SND_DENY);
        snprintf(msg, sizeof(msg), "Buy %s first",
                 upgrade_defs[upgrade_defs[id].requires].name);
        info_line(TXT_RED, msg);
        break;
    case BUY_OK:
        audio_sfx(SND_BUY);
        snprintf(msg, sizeof(msg), "Bought %s!", upgrade_defs[id].name);
        info_line(TXT_GREEN, msg);
        save_write(&game);
        break;
    }
}

static void upgrades_update(void)
{
    scene_menu_update();

    switch (ui_menu_update(&menu)) {
    case UI_MOVED:
        draw_info();
        break;
    case UI_ACTIVATE:
        try_buy(menu.cursor);
        break;
    case UI_BACK:
        // Back to wherever it was opened from: the camp's shrine or the menu.
        state_set(ST_PLAY == state_previous() ? ST_PLAY : ST_MENU);
        break;
    default:
        break;
    }
}

const State state_upgrades = {
    .name = "upgrades",
    .enter = upgrades_enter,
    .update = upgrades_update,
};
