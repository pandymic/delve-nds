// Options: volumes, zoom, controls, effects, reset save (with confirm).
// Returns to whichever state opened it.

#include <stdio.h>
#include <string.h>

#include "../fixed.h"
#include "../game.h"
#include "../input.h"
#include "../save.h"
#include "../ui.h"
#include "../audio.h"
#include "states.h"

typedef enum { KIND_VOLUME, KIND_ZOOM, KIND_TOGGLE, KIND_ACTION } ItemKind;

enum {
    OPT_MUSIC, OPT_SFX, OPT_ZOOM, OPT_AUTO_ATTACK, OPT_LEFT_HANDED,
    OPT_SHAKE, OPT_NUMBERS, OPT_HINTS, OPT_RESET, OPT_COUNT
};

// The selected item's description shows beside the Back button, so it is
// at most 22 characters.
static const struct {
    ItemKind kind;
    const char *desc;
} items[OPT_COUNT] = {
    [OPT_MUSIC]       = { KIND_VOLUME, "Music volume" },
    [OPT_SFX]         = { KIND_VOLUME, "Sound effect volume" },
    [OPT_ZOOM]        = { KIND_ZOOM,   "Camera zoom at start" },
    [OPT_AUTO_ATTACK] = { KIND_TOGGLE, "Hold A: keep swinging" },
    [OPT_LEFT_HANDED] = { KIND_TOGGLE, "ABXY move, D-pad acts" },
    [OPT_SHAKE]       = { KIND_TOGGLE, "Shake on hits, blasts" },
    [OPT_NUMBERS]     = { KIND_TOGGLE, "Damage numbers" },
    [OPT_HINTS]       = { KIND_TOGGLE, "Tips for new players" },
    [OPT_RESET]       = { KIND_ACTION, "Erase this slot" },
};

static UiMenu menu = {
    .items = { "Music", "Sound FX", "Zoom", "Auto-attack", "Left-handed",
               "Screen shake", "Hit numbers", "Hints", "Reset save" },
    .count = OPT_COUNT,
    .x = 4, .y = 3, .w = 26, .step = 2,
    .value_x = 17,
    .has_back = true,
};

#define DESC_X 10
#define DESC_Y 22

static StateId return_to;
static bool confirming;
static Options entered_with;   // to save only when something changed
static bool hints_reset;       // ...or the tips were reset

static uint8_t *toggle_field(int item)
{
    Options *o = &game.options;
    switch (item) {
    case OPT_AUTO_ATTACK: return &o->auto_attack;
    case OPT_LEFT_HANDED: return &o->left_handed;
    case OPT_SHAKE:       return &o->screen_shake;
    case OPT_NUMBERS:     return &o->hit_numbers;
    case OPT_HINTS:       return &o->hints;
    default:              return NULL;
    }
}

static void draw_value(int item)
{
    const Options *o = &game.options;
    int y = menu.y + item * menu.step;
    TextColor c = item == menu.cursor ? TXT_GOLD : TXT_NORMAL;
    switch (items[item].kind) {
    case KIND_VOLUME: {
        int v = OPT_MUSIC == item ? o->music_volume : o->sfx_volume;
        char bar[VOLUME_MAX + 1];
        for (int i = 0; i < VOLUME_MAX; i++)
            bar[i] = i < v ? '=' : '.';
        bar[VOLUME_MAX] = '\0';
        ui_text(menu.value_x, y, c, "<%s>", bar);
        break;
    }
    case KIND_ZOOM:
        ui_text(menu.value_x, y, c, "< %-5s >",
                ZOOM_FAR == o->default_zoom ? "Far" : "Close");
        break;
    case KIND_TOGGLE:
        ui_text(menu.value_x, y, c, "< %-5s >",
                *toggle_field(item) ? "On" : "Off");
        break;
    default:
        break;
    }
}

static void draw_desc(void)
{
    ui_text(DESC_X, DESC_Y, TXT_DIM, "%-22s", items[menu.cursor].desc);
}

static void draw_all(void)
{
    ui_clear();
    ui_panel(2, 0, 28, 21);
    ui_text_center(1, TXT_DIM, "- OPTIONS -");
    ui_menu_draw(&menu);
    for (int i = 0; i < menu.count; i++)
        draw_value(i);
    ui_back_button();
    draw_desc();
}

static void draw_confirm(void)
{
    char question[32];
    int slot = save_selected_slot();
    if (slot >= 0)
        snprintf(question, sizeof(question), "Erase slot %d progress?",
                 slot + 1);
    else
        snprintf(question, sizeof(question), "Erase all progress?");
    ui_confirm_draw(question, "(options are kept)");
}

static void options_enter(void)
{
    return_to = ST_NONE == state_previous() ? ST_MENU : state_previous();
    confirming = false;
    entered_with = game.options;
    hints_reset = false;
    menu.cursor = 0;
    draw_all();
}

static void adjust(int item, int delta)
{
    Options *o = &game.options;
    switch (items[item].kind) {
    case KIND_VOLUME: {
        uint8_t *v = OPT_MUSIC == item ? &o->music_volume : &o->sfx_volume;
        *v = (uint8_t)iclamp(*v + delta, 0, VOLUME_MAX);
        break;
    }
    case KIND_ZOOM:
        o->default_zoom = ZOOM_FAR == o->default_zoom ? ZOOM_CLOSE : ZOOM_FAR;
        break;
    case KIND_TOGGLE: {
        uint8_t *v = toggle_field(item);
        *v = !*v;
        break;
    }
    default:
        return;
    }
    draw_value(item);
    audio_apply_volume();
    // Hints back on: every tip shows again.
    if (OPT_HINTS == item && o->hints && game.meta.hints_seen) {
        game.meta.hints_seen = 0;
        hints_reset = true;
        ui_text(DESC_X, DESC_Y, TXT_GREEN, "%-22s", "All tips will show");
    }
}

static void update_confirm(void)
{
    UiAction act = ui_confirm_update();
    bool yes = UI_ACTIVATE == act, no = UI_BACK == act;
    if (yes) {
        Options keep = game.options;
        bool storage = game.storage_ok;
        uint32_t played = game.meta.last_played;
        uint32_t cheats = game.meta.cheats_used;
        game_set_defaults(&game);
        game.options = keep;
        game.storage_ok = storage;
        game.meta.last_played = played;   // still the slot played last
        game.meta.cheats_used = cheats;   // Code Genie's mark stays
        save_write(&game);
    }
    if (yes || no) {
        confirming = false;
        draw_all();
        if (yes)
            ui_text(DESC_X, DESC_Y, TXT_GREEN, "%-22s", "Progress erased");
    }
}

static void options_update(void)
{
    scene_menu_update();

    if (confirming) {
        update_confirm();
        return;
    }

    int old = menu.cursor;
    UiAction act = ui_menu_update(&menu);
    if (menu.cursor != old) {
        draw_value(old);
        draw_value(menu.cursor);
        draw_desc();
    }
    switch (act) {
    case UI_LEFT:
        adjust(menu.cursor, -1);
        break;
    case UI_RIGHT:
        adjust(menu.cursor, +1);
        break;
    case UI_ACTIVATE:
        if (OPT_RESET == menu.cursor) {
            confirming = true;
            draw_confirm();
        } else if (KIND_VOLUME != items[menu.cursor].kind) {
            adjust(menu.cursor, +1);
        }
        break;
    case UI_BACK:
        if (0 != memcmp(&game.options, &entered_with, sizeof(Options))
            || hints_reset)
            save_write(&game);
        state_set(return_to);
        break;
    default:
        break;
    }
}

const State state_options = {
    .name = "options",
    .enter = options_enter,
    .update = options_update,
};
