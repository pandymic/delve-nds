// Delve: init and main loop.
//
// The game runs one update per displayed frame (60 Hz). If a frame is ever
// late, the game slows down rather than skipping logic, which keeps
// collision and timing deterministic.

#include <NEMain.h>
#include <nds.h>

#include "audio.h"
#include "crash.h"
#include "fixed.h"
#include "game.h"
#include "input.h"
#include "parallax.h"
#include "render.h"
#include "save.h"
#include "state.h"
#include "states/states.h"
#include "ui.h"
#include "video.h"

#ifdef DELVE_DEBUG
// Hold L + SELECT and press START: raise a CPU exception to check the
// crash screen. (L + R + SELECT + START is the libnds exit-to-loader combo.)
static void debug_crash_combo(void)
{
    u32 held = keysHeld();
    if ((held & (KEY_L | KEY_SELECT)) == (KEY_L | KEY_SELECT)
        && (keysDown() & KEY_START))
        crash_test();
}

// Scanlines spent in the update and in drawing (NE_Process), this frame.
static int dbg_update_lines, dbg_draw_lines;

static int lines_since(int start)
{
    return (REG_VCOUNT + 263 - start) % 263;
}

// CPU meter: scanlines spent from vblank to the end of the frame's work,
// as a percentage of the 263-line frame. Shown bottom-right of the top
// screen; the second number is the worst of the last second. Bottom-left:
// the update / draw split of the last frame.
//
// Budget warning: frames over 100% CPU (a dropped frame) or over
// POLY_BUDGET polygons are counted; the count shows in red ("OVER n") for
// the second they happened in.
#define POLY_BUDGET 1800   // of the 2048 the hardware can hold

static void debug_cpu_meter(void)
{
    static int worst, shown_frames, over;
    int lines = (REG_VCOUNT + 263 - 192) % 263;
    int pct = lines * 100 / 263;
    int polys = GFX_POLYGON_RAM_USAGE;
    if (pct > worst)
        worst = pct;
    if (pct >= 100 || polys > POLY_BUDGET)
        over++;
    if (++shown_frames >= 60) {
        // Polygon RAM usage reflects the previous rendered frame.
        video_top_text(14, 23, pct > 90 ? TXT_RED : TXT_DIM,
                       "p%4d cpu%3d/%3d%%", polys, pct, worst);
        if (over)
            video_top_text(8, 23, TXT_RED, "OVER%2d", imin(over, 99));
        else
            video_top_text(8, 23, TXT_DIM, "      ");
        video_top_text(0, 22, TXT_DIM, "u%3d d%3d",
                       dbg_update_lines * 100 / 263,
                       dbg_draw_lines * 100 / 263);
        // Draw sections in scanlines: void, room, props, chars, fx, alpha.
        video_top_text(16, 22, TXT_DIM, "%d %d %d %d %d   ",
                       debug_room_sections[0], debug_room_sections[1],
                       debug_room_sections[2], debug_room_sections[3],
                       debug_room_sections[4]);
        video_top_text(0, 21, TXT_DIM, "%d %d %d %d %d %d   ",
                       debug_draw_sections[0], debug_draw_sections[1],
                       debug_draw_sections[2], debug_draw_sections[3],
                       debug_draw_sections[4], debug_draw_sections[5]);
        worst = 0;
        shown_frames = 0;
        over = 0;
    }
}
#endif

int main(int argc, char *argv[])
{
    crash_install();
    game_set_defaults(&game);

    video_init();
    ui_init();
    input_init();
    parallax_init(0xDE17Eu);
    render_init();

    // Storage after video so a slow card init does not leave the screens
    // blank and unconfigured. A missing card is not an error.
    // The slot played last is loaded (its volumes apply on the title); the
    // slot screen after the title picks the one to play.
    game.storage_ok = save_storage_init(argc, argv);
    if (game.storage_ok) {
        int slot = save_latest_slot();
        if (slot >= 0) {
            save_select_slot(slot);
            save_load(&game);
        }
    }
    audio_init(); // after loading, so saved volumes apply at once

    state_register(ST_TITLE, &state_title);
    state_register(ST_MENU, &state_menu);
    state_register(ST_OPTIONS, &state_options);
    state_register(ST_UPGRADES, &state_upgrades);
    state_register(ST_CREDITS, &state_credits);
    state_register(ST_ABOUT, &state_about);
    state_register(ST_PLAY, &state_play);
    state_register(ST_SUMMARY, &state_summary);
    state_register(ST_HISTORY, &state_history);
    state_register(ST_ROGUE, &state_rogue);
    state_register(ST_SLOTS, &state_slots);
    state_register(ST_EGG, &state_egg);
    state_set(ST_TITLE);

    while (1) {
        video_wait_vblank();
        ui_oam_update();
        ui_backdrop_update();
        input_update();
        audio_update();
#ifdef DELVE_DEBUG
        debug_crash_combo();
        int t0 = REG_VCOUNT;
#endif
        state_update();
#ifdef DELVE_DEBUG
        dbg_update_lines = lines_since(t0);
        t0 = REG_VCOUNT;
#endif
        NE_Process(state_draw);
#ifdef DELVE_DEBUG
        dbg_draw_lines = lines_since(t0);
        debug_cpu_meter();
#endif
        game_frames++;
    }

    return 0;
}
