// Run history: the last RUN_HISTORY runs (newest first) on the bottom
// screen; the selected one in full on the top screen. Opened from the main
// menu and from the run summary, and returns there.

#include <stdio.h>

#include "../fixed.h"
#include "../game.h"
#include "../input.h"
#include "../meta.h"
#include "../ui.h"
#include "../video.h"
#include "../audio.h"
#include "states.h"

static char labels[RUN_HISTORY][40];   // 26 shown

static UiMenu menu = {
    .count = 0,
    .x = 4, .y = 5, .w = 26, .step = 2,
    .has_back = true,
};

static StateId return_to;

static void draw_selected(void)
{
    const RunHistory *h = &game.history;
    char title[24];
    int i = menu.cursor;
    if (0 == i)
        snprintf(title, sizeof(title), "LAST RUN");
    else
        snprintf(title, sizeof(title), "%d RUNS BEFORE", i);
    scene_draw_run(&h->runs[i], title);
}

static void history_enter(void)
{
    const RunHistory *h = &game.history;
    return_to = ST_SUMMARY == state_previous() ? ST_SUMMARY : ST_MENU;
    if (ST_SUMMARY != return_to)
        audio_music(MUSIC_MENU);
    scene_menu_top(false);

    ui_clear();
    ui_panel(2, 0, 28, 21);
    ui_text_center(1, TXT_DIM, "- RUN HISTORY -");
    ui_text_center(3, TXT_DIM, "%lu kills, %lu bosses",
                   (unsigned long)game.meta.total_kills,
                   (unsigned long)game.meta.bosses_slain);
    menu.count = h->count;
    menu.cursor = 0;
    for (int i = 0; i < h->count; i++) {
        const RunResult *r = &h->runs[i];
        // "D12  Archer      +123*": depth, what ended it, gold earned, and
        // a star for a new best.
        snprintf(labels[i], sizeof(labels[i]), "D%-3d %-10s %+5d%s",
                 imin(r->depth, 999), meta_cause_short(r->cause),
                 imin(r->earned, 99999), r->new_best ? "*" : "");
        menu.items[i] = labels[i];
    }
    if (0 == h->count) {
        ui_text_center(10, TXT_NORMAL, "No runs yet.");
        ui_text_center(12, TXT_DIM, "Finished runs show here.");
        scene_draw_stats(12, &game.meta);
    } else {
        ui_menu_draw(&menu);
        // Runs played with Code Genie cheats: the lamp after them.
        for (int i = 0; i < h->count; i++)
            if (h->runs[i].cheated)
                ui_putc(menu.x + 22, menu.y + i * menu.step, GLYPH_LAMP, TXT_CYAN);
        draw_selected();
    }
    ui_back_button();
}

static void history_update(void)
{
    scene_menu_update();
    if (0 == menu.count) {   // nothing to move through (ui_menu_update
                             // needs at least one item)
        if ((input.down & KEY_B) || ui_back_touched()) {
            audio_sfx(SND_BACK);
            state_set(return_to);
        }
        return;
    }
    int old = menu.cursor;
    UiAction act = ui_menu_update(&menu);
    if (menu.cursor != old)
        draw_selected();
    if (UI_BACK == act)
        state_set(return_to);
}

const State state_history = {
    .name = "history",
    .enter = history_enter,
    .update = history_update,
};
