// Save slots: between the title and the main menu. Three cards on
// the bottom screen, the chosen slot's records on the top; A plays (an
// empty slot starts a new game), X erases a slot (with confirm), B goes
// back to the title. Without an SD card the title skips this screen.

#include <stdio.h>
#include <string.h>

#include "../audio.h"
#include "../game.h"
#include "../input.h"
#include "../meta.h"
#include "../save.h"
#include "../ui.h"
#include "../video.h"
#include "states.h"

#define CARD_X   2
#define CARD_W   28
#define CARD_H   5
#define CARD_Y0  2
#define CARD_STEP 6    // card height plus a blank row

#define ERASE_X  22
#define ERASE_Y  21
#define ERASE_W  9

#define MSG_X    10
#define MSG_Y    22
#define MSG_W    11    // between the Back and Erase buttons

static GameData slots[SAVE_SLOTS];
static bool used[SAVE_SLOTS];
static int cursor;
static bool confirming;

static int card_y(int i)
{
    return CARD_Y0 + i * CARD_STEP;
}

static int upgrades_owned(const MetaProgress *m)
{
    int n = 0;
    for (int i = 0; i < UPG_COUNT; i++)
        n += meta_owned(m, (UpgradeId)i);
    return n;
}

static void message(TextColor c, const char *text)
{
    ui_text(MSG_X, MSG_Y, c, "%-*s", MSG_W, text);
}

static void draw_card(int i)
{
    int y = card_y(i);
    bool sel = i == cursor;
    TextColor c = sel ? TXT_GOLD : TXT_NORMAL;
    ui_panel(CARD_X, y, CARD_W, CARD_H);
    ui_highlight(CARD_X + 1, y + 1, CARD_W - 2, sel);
    ui_text(CARD_X + 2, y + 1, c, "Slot %d", i + 1);
    if (used[i] && slots[i].meta.cheats_used)   // Code Genie's mark
        ui_putc(CARD_X + 9, y + 1, GLYPH_LAMP, TXT_CYAN);
    if (!used[i]) {
        ui_text(CARD_X + CARD_W - 8, y + 1, TXT_DIM, "Empty");
        ui_text(CARD_X + 2, y + 3, TXT_DIM, "Begin a new descent");
        return;
    }
    const MetaProgress *m = &slots[i].meta;
    char depth[12];
    snprintf(depth, sizeof(depth), "Depth %lu", (unsigned long)m->best_depth);
    ui_text(CARD_X + CARD_W - 2 - (int)strlen(depth), y + 1, c, "%s", depth);
    char time[12];
    scene_format_time(time, sizeof(time), m->play_seconds);
    ui_text(CARD_X + 2, y + 3, TXT_NORMAL, "%lu run%s", (unsigned long)m->total_runs,
            1 == m->total_runs ? "" : "s");
    ui_text(CARD_X + 11, y + 3, TXT_NORMAL, "%s", time);
    ui_text(CARD_X + 18, y + 3, TXT_NORMAL, "Upg %d", upgrades_owned(m));
}

// The chosen slot's records on the top screen, under the logo.
static void draw_top(void)
{
    for (int y = 12; y < 21; y++)
        video_top_text(0, y, TXT_NORMAL, "%31s", "");
    if (used[cursor]) {
        scene_draw_stats(12, &slots[cursor].meta);
        video_top_text(0, 12, TXT_NORMAL, "%31s", "");
    } else {
        video_top_text_center(16, TXT_NORMAL, "An empty slot");
    }
    video_top_text_center(12, TXT_DIM, "- slot %d -", cursor + 1);
}

static void draw_all(void)
{
    ui_clear();
    ui_text_center(0, TXT_DIM, "- CHOOSE A SAVE -");
    for (int i = 0; i < SAVE_SLOTS; i++)
        draw_card(i);
    ui_back_button();
    ui_panel(ERASE_X, ERASE_Y, ERASE_W, 3);
    ui_text(ERASE_X + 1, ERASE_Y + 1, TXT_GOLD, "X");
    ui_text(ERASE_X + 3, ERASE_Y + 1, TXT_NORMAL, "Erase");
    message(TXT_DIM, "A: play");
    draw_top();
}

static void draw_confirm(void)
{
    char question[24];
    snprintf(question, sizeof(question), "Erase slot %d?", cursor + 1);
    ui_confirm_draw(question, "(cannot be undone)");
}

static void read_slots(void)
{
    for (int i = 0; i < SAVE_SLOTS; i++)
        used[i] = save_peek(i, &slots[i]);
}

static void slots_enter(void)
{
    audio_music(MUSIC_MENU);
    scene_menu_top(true);
    read_slots();
    confirming = false;
    int last = save_selected_slot();
    cursor = last >= 0 ? last : 0;
    draw_all();
}

static void play(void)
{
    bool storage = game.storage_ok;
    if (used[cursor]) {
        game = slots[cursor];
    } else {
        // A new game keeps the options in use (volumes, controls).
        Options keep = game.options;
        game_set_defaults(&game);
        game.options = keep;
    }
    game.storage_ok = storage;
    save_mark_played(cursor, &game);
    // Written at once: a new game's slot is taken, and the order is kept.
    save_write(&game);
    audio_apply_volume();
    state_set(ST_MENU);
}

static void update_confirm(void)
{
    UiAction act = ui_confirm_update();
    if (UI_NONE == act)
        return;
    bool yes = UI_ACTIVATE == act;
    bool erased = false;
    if (yes) {
        erased = save_erase(cursor);
        audio_sfx(erased ? SND_SELECT : SND_DENY);
    } else {
        audio_sfx(SND_BACK);
    }
    confirming = false;
    read_slots();
    draw_all();
    if (yes)
        message(erased ? TXT_GREEN : TXT_RED, erased ? "Slot erased" : "Erase failed");
}

static int card_at(int px, int py)
{
    if (px < CARD_X * 8 || px >= (CARD_X + CARD_W) * 8)
        return -1;
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (py >= card_y(i) * 8 && py < (card_y(i) + CARD_H) * 8)
            return i;
    }
    return -1;
}

static void slots_update(void)
{
    scene_menu_update();

    if (confirming) {
        update_confirm();
        return;
    }

    int old = cursor;
    bool activate = false;
    if (input.repeat & KEY_UP) {
        cursor = (cursor + SAVE_SLOTS - 1) % SAVE_SLOTS;
    } else if (input.repeat & KEY_DOWN) {
        cursor = (cursor + 1) % SAVE_SLOTS;
    } else if (input.down & KEY_A) {
        activate = true;
    } else if ((input.down & KEY_B) || ui_back_touched()) {
        audio_sfx(SND_BACK);
        state_set(ST_TITLE);
        return;
    } else if ((input.down & KEY_X)
               || input_touched_rect(ERASE_X * 8, ERASE_Y * 8, ERASE_W * 8, 3 * 8)) {
        if (used[cursor]) {
            audio_sfx(SND_SELECT);
            confirming = true;
            draw_confirm();
        } else {
            audio_sfx(SND_DENY);
            message(TXT_DIM, "Empty slot");
        }
        return;
    } else if (input.touch_down) {
        int i = card_at(input.touch_x, input.touch_y);
        if (i >= 0) {
            cursor = i;
            activate = true;
        }
    }

    if (cursor != old) {
        draw_card(old);
        draw_card(cursor);
        draw_top();
        message(TXT_DIM, "A: play");
        if (!activate)
            audio_sfx(SND_SELECT);
    }
    if (activate) {
        audio_sfx(SND_SELECT);
        play();
    }
}

const State state_slots = {
    .name = "slots",
    .enter = slots_enter,
    .update = slots_update,
};
