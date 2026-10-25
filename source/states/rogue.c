// Easter egg: Rogue, a tribute to the 1980 original by Michael Toy, Glenn
// Wichman and Ken Arnold. Opened by tapping the version number on the About
// screen five times.
//
// Top screen: the message line, the map and the status line, as on a
// terminal. Bottom screen: stats, the pack and button help. The game itself
// lives in rogue.c; this state draws it and maps the DS controls.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <nds.h>

#include "../audio.h"
#include "../config.h"
#include "../game.h"
#include "../input.h"
#include "../rogue.h"
#include "../save.h"
#include "../ui.h"
#include "../video.h"
#include "states.h"

typedef enum {
    EG_INTRO,
    EG_SCORES,      // Top Rogueists, from the intro
    EG_PLAY,
    EG_MORE,        // a --More-- prompt is waiting
    EG_PACK,        // browsing the pack
    EG_IDENTIFY,    // choosing what to identify
    EG_HELP,
    EG_PAUSE,
    EG_CONFIRM_QUIT,
    EG_TOMB,        // death (or quitting): tombstone + scores
    EG_WIN,
} EggMode;

#define MAP_TOP       1     // map row 0 is screen row 1
#define STATUS_ROW    23
#define PACK_TOP      4
#define DIAG_GRACE    4     // frames to wait for a second D-pad direction
#define HELPLESS_STEP 8     // frames per turn while asleep or frozen
#define RUN_STEP      2     // frames per step while running
#define REST_TURNS    20

static Rogue game_state;
static Rogue *const g = &game_state;
static EggMode mode;
static int pack_cursor;
static int diag_wait;
static int run_dx, run_dy;   // running direction, 0,0 when not running
static int rest_left;
static int timer;
static int history_back;     // L: how far back in the message history
static int last_rank = -1;
static bool recorded;
static bool in_game;         // a game is in progress (Help returns to it)

// Message line pages for the current turn ("--More--" between them).
// Text is formatted into LINE_BUF and clipped to the screen when drawn.
#define LINE_BUF (RG_MSG_LEN * 2)
#define MAX_PAGES 8
static char pages[MAX_PAGES][TEXT_COLS + 1];
static int page_count, page_shown;

static TextColor tc(RogueColor c)
{
    switch (c) {
    case RC_DIM:   return TXT_DIM;
    case RC_GOLD:  return TXT_GOLD;
    case RC_RED:   return TXT_RED;
    case RC_GREEN: return TXT_GREEN;
    case RC_CYAN:  return TXT_CYAN;
    default:       return TXT_NORMAL;
    }
}

// Drawing helpers

typedef void (*PutCell)(int x, int y, char c, TextColor color);

// A whole text row: the text from column 0 (or centred), blanks around it.
// Written cell by cell, so a row never scrolls the console.
static void put_row(PutCell put, int y, TextColor c, const char *text,
                    bool center)
{
    int n = (int)strlen(text);
    int x0 = center ? (TEXT_COLS - n) / 2 : 0;
    for (int x = 0; x < TEXT_COLS; x++)
        put(x, y, x >= x0 && x < x0 + n ? text[x - x0] : ' ', c);
}

static void top_row(int y, TextColor c, const char *text)
{
    put_row(video_top_putc, y, c, text, false);
}

static void top_center(int y, TextColor c, const char *text)
{
    put_row(video_top_putc, y, c, text, true);
}

static void bottom_row(int y, TextColor c, const char *text)
{
    put_row(ui_putc, y, c, text, false);
}

static void bottom_center(int y, TextColor c, const char *text)
{
    put_row(ui_putc, y, c, text, true);
}

static void clear_top(void)
{
    for (int y = 0; y < TEXT_ROWS; y++)
        top_row(y, TXT_NORMAL, "");
}

static void clear_bottom(void)
{
    for (int y = 0; y < TEXT_ROWS; y++)
        bottom_row(y, TXT_NORMAL, "");
}

static void clear_screens(void)
{
    clear_top();
    clear_bottom();
}

// Splits this turn's messages into message-line pages, word-wrapped, with
// room for " --More--" on every page but the last.
static void paginate(void)
{
    char all[RG_MSGS * (RG_MSG_LEN + 2)];
    all[0] = '\0';
    for (int i = 0; i < g->msg_count; i++) {
        if (i)
            strcat(all, "  ");
        strcat(all, g->msgs[i]);
    }
    page_count = page_shown = 0;
    const char *p = all;
    while (*p && page_count < MAX_PAGES) {
        int rest = (int)strlen(p);
        int width = rest <= TEXT_COLS ? TEXT_COLS : TEXT_COLS - 9;
        int n = rest < width ? rest : width;
        if (n < rest) {
            int cut = n;
            while (cut > 0 && ' ' != p[cut])
                cut--;
            if (cut > 0)
                n = cut;
        }
        memcpy(pages[page_count], p, (size_t)n);
        pages[page_count][n] = '\0';
        page_count++;
        p += n;
        while (' ' == *p)
            p++;
    }
}

static bool game_over(void)
{
    return RS_DEAD == g->status || RS_WON == g->status;
}

static void draw_message_line(void)
{
    char line[LINE_BUF] = "";
    // The last words before the headstone also wait for a button.
    bool more = page_shown < page_count - 1 || (in_game && game_over());
    if (page_shown < page_count)
        snprintf(line, sizeof(line), "%s%s", pages[page_shown],
                 more ? " --More--" : "");
    top_row(0, TXT_NORMAL, line);
}

static void draw_map(void)
{
    for (int y = 0; y < RG_H; y++) {
        for (int x = 0; x < RG_W; x++) {
            char ch;
            RogueColor c;
            rogue_glyph(g, x, y, &ch, &c);
            video_top_putc(x, y + MAP_TOP, ch, tc(c));
        }
    }
}

static void draw_status_line(void)
{
    const RoguePlayer *p = &g->player;
    char line[LINE_BUF];
    snprintf(line, sizeof(line), "Lv:%d Au:%lu Hp:%d(%d) Ac:%d", g->level,
             (unsigned long)p->gold, p->hp, p->max_hp, rogue_armor_class(g));
    top_row(STATUS_ROW, p->hp * 4 <= p->max_hp ? TXT_RED : TXT_NORMAL, line);
}

static void draw_top(void)
{
    draw_message_line();
    draw_map();
    draw_status_line();
}

// Bottom screen

static void draw_pack_list(bool selecting)
{
    char name[RG_MSG_LEN], line[LINE_BUF];
    for (int i = 0; i < RG_PACK; i++) {
        int y = PACK_TOP + i;
        if (i >= g->pack_count) {
            bottom_row(y, TXT_NORMAL, "");
            continue;
        }
        rogue_item_name(g, &g->pack[i], name, sizeof(name));
        bool sel = selecting && i == pack_cursor;
        const char *worn = i == g->player.weapon ? " (in hand)"
                         : i == g->player.armor ? " (worn)" : "";
        snprintf(line, sizeof(line), "%c%c) %s%s", sel ? '>' : ' ', 'a' + i,
                 name, worn);
        bottom_row(y, sel ? TXT_GOLD : *worn ? TXT_CYAN : TXT_NORMAL, line);
    }
}

static void draw_bottom_status(void)
{
    const RoguePlayer *p = &g->player;
    char line[LINE_BUF];
    snprintf(line, sizeof(line), "Str:%d(%d) Exp:%d/%lu", p->str, p->max_str,
             p->xlevel, (unsigned long)p->exp);
    bottom_row(0, TXT_NORMAL, line);
    const char *hunger = rogue_hunger_name(g);
    int n = (int)strlen(hunger);
    for (int i = 0; i < n; i++)
        ui_putc(TEXT_COLS - n + i, 0, hunger[i], TXT_RED);

    char conditions[LINE_BUF] = "";
    if (p->confused)
        strcat(conditions, "Confused ");
    if (p->blind)
        strcat(conditions, "Blind ");
    if (p->asleep)
        strcat(conditions, "Asleep ");
    if (p->frozen)
        strcat(conditions, "Frozen ");
    if (p->held)
        strcat(conditions, "Held ");
    if (p->has_amulet)
        strcat(conditions, "Amulet");
    bottom_row(1, TXT_CYAN, conditions);
    snprintf(line, sizeof(line), "Turn %lu   Depth %d (max %d)",
             (unsigned long)g->turns, g->level, g->max_level);
    bottom_row(2, TXT_DIM, line);
    bottom_row(3, TXT_DIM, "--------------------------------");
}

static void draw_hints(void)
{
    bottom_row(21, TXT_DIM, "--------------------------------");
    switch (mode) {
    case EG_PACK: {
        const char *verb = rogue_use_verb(g, pack_cursor);
        char line[LINE_BUF];
        snprintf(line, sizeof(line), "A:%s Y:drop B:close",
                 verb ? verb : "-");
        bottom_row(22, TXT_NORMAL, line);
        bottom_row(23, TXT_DIM, "Tap an item twice to use it");
        break;
    }
    case EG_IDENTIFY:
        bottom_row(22, TXT_GOLD, "Identify which item?");
        bottom_row(23, TXT_DIM, "Pad: choose  A: identify");
        break;
    case EG_MORE:
        bottom_row(22, TXT_NORMAL, "A: more");
        bottom_row(23, TXT_DIM, "");
        break;
    default:
        bottom_row(22, TXT_NORMAL, "A:stairs B:search X:pack Y:rest");
        bottom_row(23, TXT_DIM, "R+pad:run L:log SEL:help ST:menu");
        break;
    }
}

static void draw_bottom(void)
{
    draw_bottom_status();
    draw_pack_list(EG_PACK == mode || EG_IDENTIFY == mode);
    draw_hints();
}

static void draw_all(void)
{
    draw_top();
    draw_bottom();
}

static void draw_help(void)
{
    static const char *const lines[] = {
        "          - COMMANDS -",
        "",
        "D-pad    move / attack",
        "         (two directions at",
        "         once move diagonally)",
        "R + pad  run",
        "A        use the stairs  %",
        "B        rest a turn, search",
        "Y        rest 20 turns",
        "X        pack: quaff, read, eat,",
        "         wield, wear, drop",
        "L        previous messages",
        "START    menu (quit)",
        "",
        "@ you   A-Z monsters   * gold",
        "! potion  ? scroll  : food",
        ") weapon  ] armor  ^ trap",
        "+ door  # corridor  , amulet",
        "",
        "Find the Amulet of Yendor on",
        "level 26, then climb back out.",
        "",
        "        Any button: back",
    };
    for (int y = 0; y < TEXT_ROWS; y++)
        bottom_row(y, 0 == y ? TXT_GOLD : y >= 19 && y <= 20 ? TXT_CYAN
                                                              : TXT_NORMAL,
                   y < ARRAY_LEN(lines) ? lines[y] : "");
}

// Intro, scores, tombstone

// 5x5 block letters for the title.
static const char *const banner[5] = {
    "####   ###   ###  #   # #####",
    "#   # #   # #     #   # #    ",
    "####  #   # #  ## #   # #### ",
    "#  #  #   # #   # #   # #    ",
    "#   #  ###   ###   ###  #####",
};

static void draw_intro(void)
{
    clear_screens();
    for (int i = 0; i < 5; i++)
        top_center(3 + i, TXT_GOLD, banner[i]);
    top_center(10, TXT_DIM, "The Dungeons of Doom");
    top_center(13, TXT_NORMAL, "A tribute to Rogue (1980)");
    top_center(14, TXT_NORMAL, "by Michael Toy, Glenn Wichman");
    top_center(15, TXT_NORMAL, "and Ken Arnold");
    top_center(18, TXT_CYAN, "Find the Amulet of Yendor");
    top_center(19, TXT_CYAN, "on level 26 and bring it back");
    top_center(23, TXT_DIM, "hidden in Delve " DELVE_VERSION_STRING);

    char name[RG_NAME_LEN], line[LINE_BUF];
    scene_user_name(name, sizeof(name), "Rodney");
    snprintf(line, sizeof(line), "Hello %s, just a moment", name);
    bottom_row(3, TXT_NORMAL, line);
    bottom_row(4, TXT_NORMAL, "while I dig the dungeon...");
}

static void draw_intro_menu(void)
{
    bottom_center(10, TXT_GOLD, "A: descend");
    bottom_center(12, TXT_NORMAL, "X: Top Rogueists");
    bottom_center(14, TXT_NORMAL, "SELECT: commands");
    bottom_center(16, TXT_DIM, "B: back to Delve");
}

static void draw_scores(int first_row, int highlight)
{
    const RogueBoard *b = &game.rogue_board;
    bottom_center(first_row, TXT_GOLD, "- Top Rogueists -");
    if (0 == b->count) {
        bottom_center(first_row + 3, TXT_DIM, "No one yet.");
        return;
    }
    char line[LINE_BUF], cause[RG_MSG_LEN];
    for (int i = 0; i < b->count; i++) {
        const RogueScore *s = &b->entries[i];
        TextColor c = i == highlight ? TXT_GOLD : TXT_NORMAL;
        snprintf(line, sizeof(line), "%d %7lu  %s", i + 1,
                 (unsigned long)s->score, s->name);
        bottom_row(first_row + 2 + i * 3, c, line);
        rogue_cause_text(s->cause, cause, sizeof(cause));
        if (DEATH_WON == s->cause)
            snprintf(line, sizeof(line), "  %s", cause);
        else
            snprintf(line, sizeof(line), "  %s on lvl %d", cause, s->level);
        bottom_row(first_row + 3 + i * 3, i == highlight ? TXT_GOLD : TXT_DIM,
                   line);
    }
}

// The headstone, after the original's ending screen.
static void draw_tombstone(void)
{
    static const char *const stone[] = {
        "          __________",
        "         /          \\",
        "        /    REST    \\",
        "       /      IN      \\",
        "      /     PEACE      \\",
        "     /                  \\",
        "     |                  |",
        "     |                  |",
        "     |                  |",
        "     |                  |",
        "     |                  |",
        "    *|     *  *  *      | *",
        "  ___)/\\\\_//(\\/(/\\)/\\//\\/|_)___",
    };
    clear_top();
    for (int i = 0; i < ARRAY_LEN(stone); i++)
        top_row(3 + i, i >= 11 ? TXT_GREEN : TXT_NORMAL, stone[i]);

    // Lines carved into the stone: centred in its 18 columns.
    char lines[5][RG_MSG_LEN], cause[RG_MSG_LEN];
    snprintf(lines[0], sizeof(lines[0]), "%s", g->name);
    snprintf(lines[1], sizeof(lines[1]), "%lu Au",
             (unsigned long)rogue_final_score(g));
    rogue_cause_text(g->cause, cause, sizeof(cause));
    // "killed by a" on one line, the monster's name on the next.
    const char *name = NULL;
    if (0 == strncmp(cause, "killed by ", 10))
        name = strchr(cause + 10, ' ');
    if (name) {
        snprintf(lines[2], sizeof(lines[2]), "%.*s", (int)(name - cause),
                 cause);
        snprintf(lines[3], sizeof(lines[3]), "%s", name + 1);
    } else {
        snprintf(lines[2], sizeof(lines[2]), "%s", cause);
        lines[3][0] = '\0';
    }
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(lines[4], sizeof(lines[4]), "%d", tm ? tm->tm_year + 1900 : 2026);
    for (int i = 0; i < 5; i++) {
        int n = (int)strlen(lines[i]);
        int x0 = 6 + (18 - n) / 2;
        for (int j = 0; j < n; j++)
            video_top_putc(x0 + j, 9 + i, lines[i][j], TXT_GOLD);
    }
}

static void draw_victory(void)
{
    clear_top();
    char line[LINE_BUF];
    top_center(4, TXT_GOLD, "Congratulations!");
    top_center(7, TXT_NORMAL, "You have made it to the light");
    top_center(8, TXT_NORMAL, "of day with the Amulet of Yendor");
    snprintf(line, sizeof(line), "and %lu pieces of gold.",
             (unsigned long)g->player.gold);
    top_center(9, TXT_NORMAL, line);
    top_center(12, TXT_GOLD, "A total winner!");
    top_center(16, TXT_DIM, "Thanks to Michael Toy, Glenn");
    top_center(17, TXT_DIM, "Wichman and Ken Arnold for the");
    top_center(18, TXT_DIM, "game that started it all.");
}

static void finish_game(void)
{
    in_game = false;
    if (!recorded) {
        recorded = true;
        last_rank = rogue_board_add(&game.rogue_board, g);
        if (game.storage_ok)
            save_write(&game);
    }
    clear_bottom();
    if (RS_WON == g->status) {
        mode = EG_WIN;
        draw_victory();
    } else {
        mode = EG_TOMB;
        draw_tombstone();
    }
    draw_scores(1, last_rank);
    bottom_center(19, TXT_NORMAL, "A: play again");
    bottom_center(20, TXT_DIM, "B: back to Delve");
}

// Turn handling

static void new_game(void)
{
    char name[RG_NAME_LEN];
    scene_user_name(name, sizeof(name), "Rodney");
    rogue_new(g, rng_hash3((uint32_t)time(NULL), game_frames, 0x60D),
              name);
    recorded = false;
    in_game = true;
    last_rank = -1;
    run_dx = run_dy = 0;
    rest_left = 0;
    history_back = 0;
    mode = EG_PLAY;
    clear_screens();
    paginate();
    if (page_count > 1)
        mode = EG_MORE;
    draw_all();
}

// After every action: show its messages, and notice the end of the game.
static void after_action(void)
{
    history_back = 0;
    if (game_over()) {
        run_dx = run_dy = 0;
        rest_left = 0;
        // Show the last words before the headstone.
        paginate();
        if (0 == page_count) {
            snprintf(pages[0], sizeof(pages[0]), "%s",
                     RS_WON == g->status ? "You escape the dungeon!"
                                         : "You die...");
            page_count = 1;
        }
        mode = EG_MORE;
        draw_all();
        return;
    }
    if (RS_IDENTIFY == g->status) {
        mode = EG_IDENTIFY;
        pack_cursor = 0;
    }
    paginate();
    if (page_count > 1 && EG_IDENTIFY != mode) {
        mode = EG_MORE;
        run_dx = run_dy = 0;
        rest_left = 0;
    }
    draw_all();
}

// D-pad directions held right now, as a step.
static void held_direction(int *dx, int *dy)
{
    u32 h = input.held;
    *dx = (h & KEY_RIGHT) ? 1 : (h & KEY_LEFT) ? -1 : 0;
    *dy = (h & KEY_DOWN) ? 1 : (h & KEY_UP) ? -1 : 0;
}

static void step(int dx, int dy)
{
    if ((!dx && !dy) || !rogue_move(g, dx, dy))
        return;
    after_action();
    // R held: keep running, unless the first step already found something.
    if ((input.held & KEY_R) && EG_PLAY == mode && !rogue_run_should_stop(g)) {
        run_dx = dx;
        run_dy = dy;
        timer = 0;
    }
}

static void update_running(void)
{
    // Any button stops a run or a rest.
    if (input.down) {
        run_dx = run_dy = 0;
        rest_left = 0;
        return;
    }
    if (++timer < RUN_STEP)
        return;
    timer = 0;
    if (run_dx || run_dy) {
        if (!rogue_move(g, run_dx, run_dy) || rogue_run_should_stop(g))
            run_dx = run_dy = 0;
    } else if (rest_left > 0) {
        rest_left--;
        rogue_search(g);
        if (rogue_run_should_stop(g))
            rest_left = 0;
    }
    after_action();
}

static void show_history(void)
{
    // L steps back through earlier messages, newest first.
    for (int tries = 0; tries < RG_HISTORY; tries++) {
        history_back = history_back % RG_HISTORY + 1;
        int i = (g->history_head + RG_HISTORY - history_back) % RG_HISTORY;
        if (g->history[i][0]) {
            char line[LINE_BUF];
            snprintf(line, sizeof(line), "%s", g->history[i]);
            top_row(0, TXT_DIM, line);
            return;
        }
    }
}

// A tap on one of the pack's rows (bottom screen): *i is its index.
static bool pack_row_touched(int *i)
{
    if (!input.touch_down || input.touch_y < PACK_TOP * 8
        || input.touch_y >= (PACK_TOP + g->pack_count) * 8)
        return false;
    *i = input.touch_y / 8 - PACK_TOP;
    return true;
}

// The game's controls: running and resting go on by themselves, a
// helpless player passes turns, else the D-pad and buttons act.
static void update_play(void)
{
    if (run_dx || run_dy || rest_left) {
        update_running();
        return;
    }
    if (rogue_helpless(g)) {
        if (++timer >= HELPLESS_STEP) {
            timer = 0;
            rogue_wait(g);
            after_action();
        }
        return;
    }

    // D-pad: a short grace period lets two directions pressed a frame apart
    // count as one diagonal step; holding keeps walking (key repeat).
    int dx, dy;
    if (input.down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
        diag_wait = DIAG_GRACE;
    } else if (0 == diag_wait
               && (input.repeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))) {
        held_direction(&dx, &dy);
        step(dx, dy);
        return;
    }
    if (diag_wait > 0) {
        held_direction(&dx, &dy);
        if (0 == --diag_wait || (dx && dy)) {
            diag_wait = 0;
            step(dx, dy);
        }
        return;
    }

    if (input.down & KEY_A) {
        rogue_stairs(g);
        after_action();
    } else if (input.down & KEY_B) {
        if (rogue_search(g))
            after_action();
    } else if (input.down & KEY_Y) {
        rest_left = REST_TURNS;
        timer = 0;
    } else if (input.down & KEY_X) {
        mode = EG_PACK;
        pack_cursor = 0;
        draw_bottom();
    } else if (input.down & KEY_L) {
        show_history();
    } else if (input.down & KEY_SELECT) {
        mode = EG_HELP;
        draw_help();
    } else if (input.down & KEY_START) {
        mode = EG_PAUSE;
        clear_bottom();
        bottom_center(8, TXT_GOLD, "- PAUSED -");
        bottom_center(11, TXT_NORMAL, "A: resume");
        bottom_center(13, TXT_NORMAL, "SELECT: commands");
        bottom_center(15, TXT_NORMAL, "X: quit this game");
        bottom_center(19, TXT_DIM, "The dungeon waits.");
    } else if (pack_row_touched(&pack_cursor)) {
        mode = EG_PACK;
        draw_bottom();
    }
}

static void update_pack(bool identify)
{
    int old = pack_cursor;
    if (input.repeat & KEY_UP)
        pack_cursor--;
    if (input.repeat & KEY_DOWN)
        pack_cursor++;
    if (pack_cursor < 0)
        pack_cursor = g->pack_count - 1;
    if (pack_cursor >= g->pack_count)
        pack_cursor = 0;

    bool activate = 0 != (input.down & KEY_A);
    int i;
    if (pack_row_touched(&i)) {
        activate = i == pack_cursor;   // tap again to use
        pack_cursor = i;
    }

    if (identify) {
        if (activate && rogue_identify(g, pack_cursor)) {
            mode = EG_PLAY;
            after_action();
        } else if (pack_cursor != old) {
            draw_bottom();
        }
        return;
    }

    if (activate && rogue_use_verb(g, pack_cursor)) {
        mode = EG_PLAY;
        rogue_use(g, pack_cursor);
        after_action();
    } else if (input.down & KEY_Y) {
        mode = EG_PLAY;
        rogue_drop(g, pack_cursor);
        after_action();
    } else if (input.down & (KEY_B | KEY_X)) {
        mode = EG_PLAY;
        draw_bottom();
    } else if (pack_cursor != old) {
        draw_bottom();
    }
}

// State

static void rogue_enter(void)
{
    audio_music(MUSIC_NONE);   // the original was silent
    video_set_terminal(true);
    ui_clear();
    ui_set_terminal(true);
    video_top_clear();
    mode = EG_INTRO;
    in_game = false;
    timer = 0;
    draw_intro();
}

static void rogue_exit(void)
{
    video_top_clear();
    ui_clear();
    video_set_terminal(false);
    ui_set_terminal(false);
}

static void leave(void)
{
    state_set(ST_EGG);   // the version splash
}

// Back to the game from a menu screen on the bottom.
static void resume_play(void)
{
    mode = EG_PLAY;
    clear_bottom();
    draw_bottom();
}

static void show_intro_menu(void)
{
    draw_intro();
    draw_intro_menu();
    mode = EG_INTRO;
}

// One frame of whichever screen is up: intro, scores, help, the game,
// the pack, pause, or the end.
static void rogue_update(void)
{
    switch (mode) {
    case EG_INTRO:
        if (45 == ++timer)
            draw_intro_menu();
        if (timer < 45)
            break;
        if (input.down & (KEY_A | KEY_START) || input.touch_down)
            new_game();
        else if (input.down & KEY_X) {
            mode = EG_SCORES;
            clear_bottom();
            draw_scores(2, -1);
            bottom_center(22, TXT_DIM, "Any button: back");
        } else if (input.down & KEY_SELECT) {
            mode = EG_HELP;
            draw_help();
        } else if (input.down & KEY_B) {
            leave();
        }
        break;
    case EG_SCORES:
        if (input.down || input.touch_down)
            show_intro_menu();
        break;
    case EG_HELP:
        if (input.down || input.touch_down) {
            if (in_game)
                resume_play();
            else
                show_intro_menu();
        }
        break;
    case EG_MORE:
        if (!(input.down & (KEY_A | KEY_B | KEY_UP | KEY_DOWN | KEY_LEFT
                            | KEY_RIGHT)) && !input.touch_down)
            break;
        if (page_shown < page_count - 1) {
            page_shown++;
        } else if (game_over()) {
            finish_game();
            break;
        }
        if (page_shown == page_count - 1 && !game_over())
            mode = RS_IDENTIFY == g->status ? EG_IDENTIFY : EG_PLAY;
        draw_message_line();
        draw_hints();
        if (EG_IDENTIFY == mode)
            draw_bottom();
        break;
    case EG_PLAY:
        update_play();
        break;
    case EG_PACK:
        update_pack(false);
        break;
    case EG_IDENTIFY:
        update_pack(true);
        break;
    case EG_PAUSE:
        if (input.down & (KEY_A | KEY_START | KEY_B)) {
            resume_play();
        } else if (input.down & KEY_SELECT) {
            mode = EG_HELP;
            draw_help();
        } else if (input.down & KEY_X) {
            mode = EG_CONFIRM_QUIT;
            bottom_center(15, TXT_RED, "Really quit? A: yes  B: no");
        }
        break;
    case EG_CONFIRM_QUIT:
        if (input.down & KEY_A) {
            rogue_quit(g);
            finish_game();
        } else if (input.down & KEY_B) {
            resume_play();
        }
        break;
    case EG_TOMB:
    case EG_WIN:
        if (input.down & KEY_A)
            new_game();
        else if (input.down & KEY_B)
            leave();
        break;
    }
}

const State state_rogue = {
    .name = "rogue",
    .enter = rogue_enter,
    .update = rogue_update,
    .exit = rogue_exit,
};
