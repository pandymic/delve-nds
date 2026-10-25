// Title screen: logo over a 3D diorama of a dungeon room, parallax behind;
// START or touch continues.

#include "../config.h"
#include "../game.h"
#include "../input.h"
#include "../ui.h"
#include "../audio.h"
#include "states.h"

static void title_enter(void)
{
    audio_music(MUSIC_MENU);
    scene_menu_top(true);
    video_top_text_center(8, TXT_GOLD, "an endless descent");
    // Right-aligned; the last cell of the bottom row is left free. The
    // string's size counts its NUL, which makes room for the 'v'.
    int len = (int)sizeof(DELVE_VERSION_STRING);
    video_top_text(TEXT_COLS - 1 - len, TEXT_ROWS - 1, TXT_DIM, "v%s",
                   DELVE_VERSION_STRING);

    ui_clear();
    ui_panel(4, 9, 24, 5);
}

static void title_update(void)
{
    scene_menu_update();

    // Blink the prompt: 40 frames on, 20 off.
    unsigned phase = state_frames() % 60;
    if (0 == phase)
        ui_text_center(11, TXT_GOLD, "Touch or press START");
    else if (40 == phase)
        ui_text(6, 11, TXT_GOLD, "                    ");

    if ((input.down & (KEY_START | KEY_A)) || input.touch_down) {
        audio_sfx(SND_SELECT);
        // Save slots need the SD card; without one, straight to the menu.
        state_set(game.storage_ok ? ST_SLOTS : ST_MENU);
    }
}

const State state_title = {
    .name = "title",
    .enter = title_enter,
    .update = title_update,
    .draw = scene_draw_diorama,
};
