// About: the game and its version (top screen), with any Code Genie cheats
// on, and who made the music and art (bottom; LICENSES.md has it all). The
// bug report (build, device, save) is behind a Code Genie code (egg.c).

#include <string.h>

#include <nds.h>

#include "../cheat.h"
#include "../config.h"
#include "../game.h"
#include "../input.h"
#include "../ui.h"
#include "../video.h"
#include "../audio.h"
#include "states.h"

// Easter egg: tap the version number EGG_TAPS times, each within EGG_GAP
// frames of the last, for the version splash (states/egg.c). Each tap
// floats "N taps to go" up from the number.
#define EGG_TAPS        5
#define EGG_GAP         60
#define EGG_HINT_FRAMES 48
#define VERSION_ROW     22
#define HINT_ROW        20    // the hint rises from here...
#define HINT_RISE       3     // ...this many rows

static const char version_label[] = "v" DELVE_VERSION_STRING;
static int taps;
static unsigned last_tap;
static int hint_timer;
static int hint_row = -1;
static char hint[24];

static int version_x(void)
{
    return TEXT_COLS - 1 - (int)(sizeof(version_label) - 1);
}

static void clear_hint(void)
{
    if (hint_row >= 0)
        ui_text(TEXT_COLS - 1 - (int)strlen(hint), hint_row, TXT_NORMAL, "%*s",
                (int)strlen(hint), "");
    hint_row = -1;
}

static void draw_hint(void)
{
    int step = (EGG_HINT_FRAMES - hint_timer) * (HINT_RISE + 1) / EGG_HINT_FRAMES;
    int row = HINT_ROW - step;
    if (row != hint_row) {
        clear_hint();
        hint_row = row;
    }
    TextColor c = hint_timer > EGG_HINT_FRAMES / 3 ? TXT_GOLD : TXT_DIM;
    ui_text(TEXT_COLS - 1 - (int)strlen(hint), row, c, "%s", hint);
}

static void version_tapped(void)
{
    unsigned now = state_frames();
    taps = taps > 0 && now - last_tap <= EGG_GAP ? taps + 1 : 1;
    last_tap = now;
    if (taps >= EGG_TAPS) {
        taps = 0;
        clear_hint();
        audio_sfx(SND_PICKUP);
        state_set(ST_EGG);
        return;
    }
    audio_sfx(SND_SELECT);
    clear_hint();
    int left = EGG_TAPS - taps;
    snprintf(hint, sizeof(hint), "%d tap%s to go", left, 1 == left ? "" : "s");
    hint_timer = EGG_HINT_FRAMES;
    draw_hint();
}

static void update_egg(void)
{
    int x = version_x();
    if (input_touched_rect((x - 1) * 8, (VERSION_ROW - 1) * 8,
                           (TEXT_COLS - x + 1) * 8, 3 * 8))
        version_tapped();
    if (hint_timer > 0) {
        if (0 == --hint_timer)
            clear_hint();
        else
            draw_hint();
    }
}


static void about_enter(void)
{
    audio_music(MUSIC_MENU);
    scene_menu_top(true);
    video_top_text_center(0, TXT_GOLD, "- ABOUT DELVE -");
    video_top_text_center(9, TXT_NORMAL, "An endless dungeon crawl");
    video_top_text_center(11, TXT_NORMAL, "for the Nintendo DS");
    video_top_text_center(13, TXT_DIM, "Version %s", DELVE_VERSION_STRING);
    if (cheat_active) {
        video_top_putc(9, 15, GLYPH_LAMP, TXT_CYAN);
        video_top_text(11, 15, TXT_CYAN, "Code Genie");
        scene_draw_cheats(17);
    } else if (game.meta.cheats_used) {
        video_top_putc(3, 15, GLYPH_LAMP, TXT_CYAN);
        video_top_text(5, 15, TXT_DIM, "This save used Code Genie");
    }

    // Attribution: the music's composers and licences, then the art.
    static const struct {
        const char *artist, *licence, *tracks;
    } music[] = {
        { "Kosmo The Cat", "CC0", "title, dungeon, boss" },
        { "Beyond", "public domain", "camp, sewers" },
        { "Drozerix", "public domain", "caverns" },
        { "JAM", "public domain", "molten depths" },
    };
    ui_clear();
    ui_panel(2, 0, 28, 17);
    ui_text_center(1, TXT_DIM, "- MUSIC -");
    for (int i = 0; i < ARRAY_LEN(music); i++) {
        int y = 3 + i * 3;
        ui_text(3, y, TXT_GOLD, "%s", music[i].artist);
        ui_text(28 - (int)strlen(music[i].licence), y, TXT_DIM, "%s",
                music[i].licence);
        ui_text(4, y + 1, TXT_NORMAL, "%s", music[i].tracks);
    }
    ui_text(3, 15, TXT_DIM, "Art Shade, font devurandom");
    ui_text(3, 17, TXT_DIM, "See LICENSES.md");
    ui_back_button();
    ui_text(version_x(), VERSION_ROW, TXT_DIM, "%s", version_label);
    taps = 0;
    hint_timer = 0;
    hint_row = -1;
}

static void about_update(void)
{
    scene_menu_update();
    update_egg();
    if ((input.down & (KEY_B | KEY_A | KEY_START)) || ui_back_touched()) {
        audio_sfx(SND_BACK);
        state_set(ST_MENU);
    }
}

const State state_about = {
    .name = "about",
    .enter = about_enter,
    .update = about_update,
};
