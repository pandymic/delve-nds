// Run summary: how the run ended, what it achieved and the currency it
// earned, then descend again, go back to the camp or look at past runs.
// The run has already been recorded and saved by the play state.

#include "../game.h"
#include "../meta.h"
#include "../input.h"
#include "../ui.h"
#include "../video.h"
#include "../audio.h"
#include "states.h"

#define COUNT_FRAMES 60  // currency counts up over one second
#define INPUT_DELAY  45  // frames before the menu takes input, so A pressed
                         // to leave the death screen can't descend at once

enum { ITEM_DESCEND, ITEM_CAMP, ITEM_HISTORY };

static UiMenu menu = {
    .items = { "Descend again", "Back to camp", "Run history" },
    .count = 3,
    .x = 9, .y = 14, .w = 18, .step = 3,
    .has_back = true,     // B: back to camp
};

bool play_descend_now;

static int shown;

static void summary_enter(void)
{
    const RunResult *r = &game.last_run;
    // Back from the history screen: the same summary, cursor kept.
    if (ST_HISTORY != state_previous()) {
        audio_music(MUSIC_MENU);
        menu.cursor = ITEM_DESCEND;
    }
    scene_menu_top(false);
    scene_draw_run(r, r->abandoned ? "RUN ABANDONED" : "YOU DIED");

    ui_clear();
    ui_panel(3, 0, 26, 12);
    ui_text_center(1, TXT_DIM, "- RUN SUMMARY -");
    ui_text(5, 3, TXT_NORMAL, "Gold found");
    ui_text(21, 3, TXT_NORMAL, "%5d", r->gold);
    ui_text(5, 5, TXT_NORMAL, "Depth x%d", META_GOLD_PER_DEPTH);
    ui_text(21, 5, TXT_NORMAL, "%5d",
            meta_depths_descended(r) * META_GOLD_PER_DEPTH);
    ui_text(5, 7, TXT_NORMAL, "Kills x%d", META_GOLD_PER_KILL);
    ui_text(21, 7, TXT_NORMAL, "%5d", r->kills * META_GOLD_PER_KILL);
    ui_icon(0, ICON_GOLD, 4 * 8, 9 * 8 - 4);
    ui_text(7, 9, TXT_GOLD, "Earned");
    ui_panel(5, 12, 22, 11);
    ui_menu_draw(&menu);
    if (!game.storage_ok)
        ui_text_center(23, TXT_RED, "No SD card: not saved");
    shown = ST_HISTORY == state_previous() ? r->earned : -1;
    ui_text(21, 9, TXT_GOLD, "%5d", shown < 0 ? 0 : shown);
}

static void summary_update(void)
{
    scene_menu_update();
    const RunResult *r = &game.last_run;

    unsigned f = state_frames();
    int value = f >= COUNT_FRAMES || shown == r->earned ? r->earned
              : (int)(r->earned * f / COUNT_FRAMES);
    if (value != shown) {
        ui_text(21, 9, TXT_GOLD, "%5d", value);
        shown = value;
    }
    if (f <= INPUT_DELAY)
        return;

    switch (ui_menu_update(&menu)) {
    case UI_ACTIVATE:
        switch (menu.cursor) {
        case ITEM_DESCEND:
            play_descend_now = true;
            state_set(ST_PLAY);
            break;
        case ITEM_CAMP:
            state_set(ST_PLAY);
            break;
        case ITEM_HISTORY:
            state_set(ST_HISTORY);
            break;
        }
        break;
    case UI_BACK:
        state_set(ST_PLAY);   // back to the camp
        break;
    default:
        break;
    }
}

const State state_summary = {
    .name = "summary",
    .enter = summary_enter,
    .update = summary_update,
};
