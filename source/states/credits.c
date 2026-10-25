// Credits: text scrolling up the top screen over the parallax.

#include "../config.h"
#include "../input.h"
#include "../ui.h"
#include "../video.h"
#include "../audio.h"
#include "states.h"

static const char *const lines[] = {
    "D E L V E",
    "",
    "an endless descent",
    "",
    "",
    "- design & code -",
    "Michal Pandyra",
    "",
    "with help from Claude",
    "",
    "",
    "- art -",
    "Puny Characters",
    "Puny Dungeon Tileset",
    "Miniworld Sprites",
    "by Shade (CC0)",
    "",
    "Victoria Bold font",
    "by devurandom (CC0)",
    "",
    "",
    "- music -",
    "Kosmo The Cat (CC0)",
    "Beyond (public domain)",
    "Drozerix (public domain)",
    "JAM (public domain)",
    "",
    "- sound effects -",
    "original, synthesized",
    "for Delve",
    "",
    "",
    "- built with -",
    "BlocksDS",
    "Nitro Engine",
    "libnds  Maxmod",
    "",
    "",
    "- tested on -",
    "melonDS",
    "Nintendo 3DS",
    "(TWiLight Menu++)",
    "",
    "",
    "thanks for playing",
    "",
    "eightyfour.ca/delve",
    "github.com/pandymic/delve-nds",
};
#define LINE_COUNT ARRAY_LEN(lines)

// The crawl repeats every PERIOD rows: TEXT_ROWS blank rows (the screen
// before the first line comes up), then the lines.
#define PERIOD (TEXT_ROWS + LINE_COUNT)

// The text layer's map is 32 rows (256 px) tall and wraps, so it is a ring:
// the layer scrolls one pixel at a time and only the row about to come up
// is written, while it is still below the screen. (Redrawing every row and
// resetting the scroll each 8 px made the crawl jump: the map changes at
// once, the scroll register only at the next vblank.)
static int scroll;     // pixels scrolled; virtual row v sits at v * 8
static int next_row;   // the first virtual row not yet written

static void write_row(int v)
{
    int i = v % PERIOD - TEXT_ROWS;
    const char *text = i >= 0 ? lines[i] : "";
    TextColor c = '-' == text[0] ? TXT_DIM : TXT_NORMAL;
    if (0 == i)
        c = TXT_GOLD;
    video_top_map_row(v, c, text);
}

static void clear_map(void)
{
    for (int row = 0; row < 32; row++)
        video_top_map_row(row, TXT_NORMAL, "");
}

static void credits_enter(void)
{
    audio_music(MUSIC_MENU);
    scroll = 0;
    next_row = 0;
    video_top_clear();
    clear_map();
    video_top_scroll(0);

    ui_clear();
    ui_panel(4, 6, 24, 5);
    ui_text_center(8, TXT_DIM, "- CREDITS -");
    ui_back_button();
}

static void credits_update(void)
{
    scene_menu_update();

    if (0 == state_frames() % 2)
        scroll++;
    // After PERIOD * 32 rows both the crawl and the 32-row map are back
    // where they started: wrap both counters together.
    if (scroll >= PERIOD * 256) {
        scroll -= PERIOD * 256;
        next_row -= PERIOD * 32;
    }

    // Rows on screen: scroll / 8 to scroll / 8 + TEXT_ROWS (partly). The
    // last of them is still below the screen when scroll / 8 first reaches
    // it, so the write is never seen.
    int last = scroll / 8 + TEXT_ROWS;
    while (next_row <= last)
        write_row(next_row++);
    video_top_scroll(scroll % 256);

    if ((input.down & (KEY_B | KEY_A | KEY_START)) || ui_back_touched()) {
        audio_sfx(SND_BACK);
        state_set(ST_MENU);
    }
}

static void credits_exit(void)
{
    clear_map();   // rows 24-31 are off-screen; leave nothing behind
    video_top_scroll(0);
}

const State state_credits = {
    .name = "credits",
    .enter = credits_enter,
    .update = credits_update,
    .exit = credits_exit,
};
