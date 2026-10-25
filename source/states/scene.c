// Shared top-screen scenery for the title and menu states.

#include <stdio.h>
#include <string.h>

#include <NEMain.h>

#include "../anim.h"
#include "../art_layout.h"
#include "../boon.h"
#include "../cheat.h"
#include "../game.h"
#include "../meta.h"
#include "../parallax.h"
#include "../render.h"
#include "../roomgen.h"
#include "../video.h"
#include "states.h"

void scene_menu_top(bool logo)
{
    video_top_clear();
    video_top_scroll(0);
    if (logo)
        video_logo_draw(2);
}

void scene_menu_update(void)
{
    parallax_update(48, 20);
}

// Title diorama: a lit treasure room with the hero breathing in front of
// the chest, drawn by the game's own renderer under the logo.

#define DIORAMA_SEED 0x7171E5u

static Room diorama;
static RoomAlpha diorama_alpha;
static Camera diorama_cam;
static bool diorama_ready;

static void build_diorama(void)
{
    roomgen_generate(&diorama, DIORAMA_SEED, 1, RT_TREASURE, SIDE_PZ,
                     ROOM_SIZE / 2);
    diorama.enemy_count = 0;
    room_fade_reset(diorama_alpha);
    render_void_init(DIORAMA_SEED);
    // Aim a little behind the chest, so the room sits under the logo.
    camera_init(&diorama_cam, ZOOM_CLOSE, FX_INT(ROOM_SIZE), FX(4.6), FX(4.6));
    diorama_ready = true;
}

void scene_draw_diorama(void)
{
    if (!diorama_ready)
        build_diorama();
    int frame = (int)game_frames;
    const fx hx = FX(5.5), hz = FX(7.3);
    PropState ps = { .room_frame = (uint32_t)frame, .flood_ring = -1 };
    render_begin(&diorama_cam);
    render_void(&diorama_cam, frame);
    render_room(&diorama, diorama_alpha, hx, hz, false, frame, &ps);
    render_room_props(&diorama, false, false, &ps, frame);
    render_shadow(hx, hz, FX_ONE);
    // Idle, facing the viewer: the breathing loop.
    int slot = PF_IDLE + (frame / 32) % PF_IDLE_COUNT;
    render_char(SHEET_PLAYER, slot, false, PF_FEET_ROW, hx, hz, SPAL_NORMAL);
}

void scene_format_time(char *out, size_t size, uint32_t seconds)
{
    unsigned long minutes = seconds / 60;
    if (minutes >= 60)
        snprintf(out, size, "%luh%02lum", minutes / 60, minutes % 60);
    else
        snprintf(out, size, "%lum", minutes);
}

void scene_draw_stats(int y, const MetaProgress *m)
{
    video_top_text_center(y, TXT_DIM, "- records -");
    // A blank row between lines: the glyphs fill all 8 rows of a tile
    // (shadow and descenders), so back-to-back lines run together.
    video_top_text(8, y + 2, TXT_NORMAL, "Best depth");
    video_top_text(20, y + 2, TXT_GOLD, "%4lu", (unsigned long)m->best_depth);
    video_top_text(8, y + 4, TXT_NORMAL, "Runs");
    video_top_text(20, y + 4, TXT_GOLD, "%4lu", (unsigned long)m->total_runs);
    video_top_text(8, y + 6, TXT_NORMAL, "Gold");
    video_top_text(20, y + 6, TXT_GOLD, "%4lu", (unsigned long)m->currency);
    char time[12];
    scene_format_time(time, sizeof(time), m->play_seconds);
    video_top_text(8, y + 8, TXT_NORMAL, "Played");
    video_top_text(24 - (int)strlen(time), y + 8, TXT_GOLD, "%s", time);
    // A save that has played with Code Genie cheats.
    if (m->cheats_used)
        video_top_putc(24, y + 2, GLYPH_LAMP, TXT_CYAN);
}

// A finished run on the top screen (summary and history): how it ended,
// the numbers, and the boons taken.
void scene_draw_run(const RunResult *r, const char *title)
{
    video_top_clear();
    video_top_text_center(1, r->abandoned ? TXT_DIM : TXT_RED, "%s", title);
    video_top_text_center(3, TXT_NORMAL, "%s", meta_cause_text(r->cause));
    video_top_text_center(5, TXT_NORMAL, "Depth %d, %s", r->depth,
                          area_defs[room_area(r->depth)].name);
    if (r->new_best)
        video_top_text_center(7, TXT_GOLD, "* new best depth *");

    static const char *const labels[6] = { "Rooms", "Kills", "Bosses",
                                           "Gold", "Time", "Earned" };
    char values[6][24];
    snprintf(values[0], sizeof(values[0]), "%d", r->rooms_cleared);
    snprintf(values[1], sizeof(values[1]), "%d", r->kills);
    snprintf(values[2], sizeof(values[2]), "%d", r->bosses);
    snprintf(values[3], sizeof(values[3]), "%d", r->gold);
    if (r->seconds >= 3600)
        snprintf(values[4], sizeof(values[4]), "%d:%02d:%02d", r->seconds / 3600,
                 r->seconds / 60 % 60, r->seconds % 60);
    else
        snprintf(values[4], sizeof(values[4]), "%d:%02d", r->seconds / 60,
                 r->seconds % 60);
    snprintf(values[5], sizeof(values[5]), "%d", r->earned);
    for (int i = 0; i < 6; i++) {
        int x = (i & 1) ? 17 : 2, y = 9 + (i / 2) * 2;
        video_top_text(x, y, TXT_DIM, "%s", labels[i]);
        video_top_text(x + 13 - (int)strlen(values[i]), y, TXT_GOLD, "%s",
                       values[i]);
    }
    if (r->cheated) {
        // Played with Code Genie cheats.
        video_top_text(3, 15, TXT_DIM, "Seed %08lX", (unsigned long)r->seed);
        video_top_putc(18, 15, GLYPH_LAMP, TXT_CYAN);
        video_top_text(20, 15, TXT_CYAN, "Code Genie");
    } else {
        video_top_text_center(15, TXT_DIM, "Seed %08lX", (unsigned long)r->seed);
    }

    // Boons, two per row: spaced out when there are few, packed when many.
    int count = 0;
    for (int i = 0; i < BOON_COUNT; i++)
        count += r->boons[i] > 0;
    if (0 == count) {
        video_top_text_center(18, TXT_DIM, "No boons taken");
        return;
    }
    video_top_text_center(17, TXT_DIM, "- boons -");
    bool packed = count > 6;
    int slot = 0;
    for (int i = 0; i < BOON_COUNT; i++) {
        if (!r->boons[i])
            continue;
        // Columns 1-15 and 16-30: the last row must not reach column 31.
        int x = (slot & 1) ? 16 : 1;
        int y = packed ? 18 + slot / 2 : 19 + (slot / 2) * 2;
        if (packed && 11 == slot && count > 12) {
            video_top_text(x, y, TXT_DIM, "+%d more", count - 11);
            break;
        }
        if (r->boons[i] > 1)
            video_top_text(x, y, TXT_GOLD, "%s x%d", boon_defs[i].name,
                           r->boons[i]);
        else
            video_top_text(x, y, TXT_GOLD, "%s", boon_defs[i].name);
        slot++;
    }
}

int scene_draw_cheats(int y)
{
    int n = 0;
    for (int i = 0; i < CHEAT_COUNT; i++) {
        if (!cheat_on(cheat_active, (CheatId)i))
            continue;
        video_top_text(n % 2 ? 17 : 1, y + n / 2, TXT_CYAN, "%s",
                       cheat_defs[i].name);
        n++;
    }
    return n;
}

void scene_user_name(char *out, size_t size, const char *fallback)
{
    int n = PersonalData->nameLen;
    if (n > 10)
        n = 10;
    size_t j = 0;
    for (int i = 0; i < n && j + 1 < size; i++) {
        s16 ch = PersonalData->name[i];
        out[j++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
    }
    out[j] = '\0';
    if (0 == j)
        snprintf(out, size, "%s", fallback);
}
