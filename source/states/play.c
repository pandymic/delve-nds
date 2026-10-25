// Run in progress: the room view.
//
// Gameplay runs in run.c; this state reads input, renders, shows banners and
// the door preview, and fades between rooms.

#include <string.h>
#include <time.h>

#include "../anim.h"
#include "../art_layout.h"
#include "../audio.h"
#include "../camera.h"
#include "../cheat.h"
#include "../fixed.h"
#include "../fx.h"
#include "../game.h"
#include "../hint.h"
#include "../input.h"
#include "../meta.h"
#include "../parallax.h"
#include "../render.h"
#include "../rng.h"
#include "../roomgen.h"
#include "../run.h"
#include "../save.h"
#include "../ui.h"
#include "../video.h"
#include "states.h"

#define FADE_FRAMES   12
#define DESCEND_FRAMES 44   // walking down into the crypt
#define BANNER_FRAMES 120
#define BANNER_ROW    3

typedef enum {
    PM_PLAY,
    PM_FADE_OUT,
    PM_FADE_IN,
    PM_PAUSED,
    PM_CONFIRM_ABANDON,
    PM_BOON,            // choosing a boon after a cleared room
    PM_HINT,            // a first-time tip (hint.h); the game waits
    PM_WAYPOINT,        // at the crypt: where to start
} PlayMode;

enum { PAUSE_RESUME, PAUSE_OPTIONS, PAUSE_ABANDON };

static UiMenu pause_menu = {
    .items = { "Resume", "Options", "Abandon run" },
    .count = 3,
    .x = 10, .y = 13, .w = 16, .step = 3,
    .has_back = false,
};

static Run run;
#ifdef DELVE_DEBUG
int debug_draw_sections[6];
#endif
static Camera cam;
static RoomAlpha fade;
static PlayMode mode;
static int mode_timer;
static int start_area;   // the crypt's choice (waypoints); kept for
                         // "Descend again"
static int banner_timer;

// Combat feel (presentation only; the simulation never sees these).
#define HITSTOP_HIT     3     // frames the action freezes on a hit
#define HITSTOP_KILL    5
#define HITSTOP_BLAST   6
#define SHAKE_HURT      6
#define SHAKE_BLAST     10
#define RING_FRAMES     16    // blast ring expansion
#define AFTERIMAGES     4
#define AFTERIMAGE_LIFE 12

static Fx effects;
static int hitstop;
static int shake;
static int ring_timer;
static fx ring_x, ring_z, ring_radius;

typedef struct {
    int life;
    int slot;
    bool mirror;
    fx x, z;
} Afterimage;
static Afterimage afterimages[AFTERIMAGES];
static int next_afterimage;

// Last values drawn, to redraw only on change.
static int hud_hp, hud_dash, hud_daggers, hud_meter, shown_alive, shown_gold;
static unsigned hud_blink;

// Feedback: an action with nothing left blinks its empty HUD slots; one
// heart left blinks that heart with a heartbeat.
#define EMPTY_FLASH     24    // frames the empty slots blink
#define LOW_HP_PERIOD   100   // frames between heartbeats
#define LOW_HP_BLINK    14    // ...of which the heart shows empty
static int flash_timer;
static unsigned flash_what;
static RoomStatus shown_status;

static const char *const room_names[RT_COUNT] = {
    [RT_COMBAT] = "Combat",
    [RT_REST] = "Rest",
    [RT_TREASURE] = "Treasure",
    [RT_BOSS] = "Boss",
};
static const char *const reward_names[RT_COUNT] = {
    [RT_COMBAT] = "Fight for gold",
    [RT_REST] = "Rest and heal",
    [RT_TREASURE] = "Treasure chest",
    [RT_BOSS] = "The boss",
};
static const char *boss_name(EnemyType t)
{
    switch (t) {
    case ENEMY_SLIME_KING: return "SLIME KING";
    case ENEMY_GOLEM:      return "CAVERN YETI";
    case ENEMY_MAGMA_LORD: return "MAGMA LORD";
    default:               return "ORC WARLORD";
    }
}

// The sky by depth: the area's two colour bands (area.c), one for each
// half of it.
static int sky_theme(int depth)
{
    int within = (depth < 1 ? 0 : depth - 1) % AREA_DEPTHS;
    return area_defs[room_area(depth)].sky[within >= AREA_DEPTHS / 2];
}
static const char *const side_names[SIDE_COUNT] = {
    [SIDE_NX] = "upper left",
    [SIDE_NZ] = "upper right",
    [SIDE_PX] = "lower right",
    [SIDE_PZ] = "lower left",
};

// Screens

static unsigned hud_blink_now(void)
{
    unsigned b = 0;
    if (flash_timer > 0 && (flash_timer & 4))
        b |= flash_what;
    if (1 == run.player.hp && ROOM_DEAD != run.status
        && game_frames % LOW_HP_PERIOD < LOW_HP_BLINK)
        b |= HUD_BLINK_HEART;
    return b;
}

static void draw_hud(void)
{
    const Player *p = &run.player;
    Hud h = {
        .hp = p->hp, .max_hp = p->max_hp,
        .dash = p->dash_charges, .dash_max = p->dash_max,
        .daggers = p->daggers, .dagger_max = p->dagger_max,
        .meter = p->meter, .meter_max = p->meter_max,
        .depth = run.depth,
        .genie = 0 != run.cheats,
        .blink = hud_blink_now(),
    };
    video_hud_draw(&h);
    hud_blink = h.blink;
    hud_hp = p->hp;
    hud_dash = p->dash_charges;
    hud_daggers = p->daggers;
    hud_meter = p->meter;
}

static void clear_banner(void)
{
    video_top_text(0, BANNER_ROW, TXT_NORMAL, "%31s", "");
}

static void banner(TextColor c, const char *text)
{
    clear_banner();
    video_top_text_center(BANNER_ROW, c, "%s", text);
    banner_timer = BANNER_FRAMES;
}

// Leaves the room: a fade (from the camp, a walk down the crypt first).
static void begin_fade_out(void)
{
    mode = PM_FADE_OUT;
    mode_timer = 0;
}

// A banner that stays until the next one (or the screen is cleared).
static void banner_held(TextColor c, const char *text)
{
    clear_banner();
    video_top_text_center(BANNER_ROW, c, "%s", text);
    banner_timer = 0;
}

// The choice guard. A boon offer opens the moment a room is cleared, and
// the waypoint choice the moment the hero steps onto the crypt's stairs,
// both while the player is likely holding the D-pad or mashing A. So while
// either is open:
//   - it takes no input until CHOICE_INPUT_DELAY frames have passed with no
//     new press (as tips wait HINT_INPUT_DELAY): mashing keeps it waiting;
//     after that, a key held since it opened counts only once released and
//     pressed again;
//   - the top screen dims, its banner pulses with an arrow toward the lower
//     screen, and the lower screen's panels and highlight glow until the
//     choice takes input (glowing: not yet; steady: choose now).
// The game's input is filtered in place around the update, so ui.c's menus
// and input_touched_rect() see the same. Input and presentation only: no
// RNG or gameplay state.

#define CHOICE_INPUT_DELAY 30   // frames of no new press: half a second
#define CHOICE_PULSE       16   // frames per half-pulse of the banner
#define CHOICE_DIM         (-5) // the top screen while a choice is open
#define CHOICE_GLOW        10   // of 16 toward white, at the glow's peak

static bool choice_open;
static int choice_wait, choice_frames;
static uint32_t choice_held;   // keys held at opening, until released
static u16 choice_panel[16], choice_hilite[16];   // ui.c's colours

static bool choice_mode(void)
{
    return PM_BOON == mode || PM_WAYPOINT == mode;
}

// The banner, gold and white in turn, with an arrow toward the lower screen.
static void choice_banner(void)
{
    TextColor c = (choice_frames / CHOICE_PULSE) % 2 ? TXT_NORMAL : TXT_GOLD;
    const char *text = PM_BOON == mode ? "Choose a boon" : "Where to?";
    int len = (int)strlen(text) + 2;   // " v"
    int x = (TEXT_COLS - len) / 2;
    clear_banner();
    video_top_text(x, BANNER_ROW, c, "%s", text);
    video_top_arrow_down(x + len - 1, BANNER_ROW, c);
    banner_timer = 0;   // stays until the choice is made
}

static u16 brighter(u16 c, int k)   // k of 16 toward white
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r += (31 - r) * k / 16;
    g += (31 - g) * k / 16;
    b += (31 - b) * k / 16;
    return (u16)RGB15(r, g, b);
}

// The lower screen's panel frames and highlight bar; k = 0 is ui.c's own.
static void choice_glow(int k)
{
    u16 *panel = BG_PALETTE_SUB + PAL_BANK_PANEL * 16;
    u16 *hilite = BG_PALETTE_SUB + PAL_BANK_HILITE * 16;
    panel[2] = brighter(choice_panel[2], k);
    panel[9] = brighter(choice_panel[9], k);
    hilite[1] = brighter(choice_hilite[1], k);
    hilite[2] = brighter(choice_hilite[2], k);
}

static void choice_begin(void)
{
    choice_open = true;
    choice_wait = CHOICE_INPUT_DELAY;
    choice_frames = 0;
    choice_held = input.held;
    memcpy(choice_panel, BG_PALETTE_SUB + PAL_BANK_PANEL * 16,
           sizeof(choice_panel));
    memcpy(choice_hilite, BG_PALETTE_SUB + PAL_BANK_HILITE * 16,
           sizeof(choice_hilite));
    setBrightness(1, CHOICE_DIM);
    choice_banner();
}

static void choice_end(bool fade_follows)
{
    if (!choice_open)
        return;
    choice_open = false;
    choice_glow(0);
    // A fade that follows sets both screens itself.
    if (!fade_follows)
        setBrightness(1, 0);
}

// Filters the game's input for this frame's update; returns the real input
// for choice_after() to restore.
static Input choice_before(void)
{
    Input real = input;
    if (choice_mode() && !choice_open)
        choice_begin();
    if (!choice_open)
        return real;
    if (choice_wait > 0 && (input.down || input.touch_down))
        choice_wait = CHOICE_INPUT_DELAY;   // a press while waiting: wait on
    choice_held &= input.held;
    uint32_t block = choice_wait > 0 ? ~0u : choice_held;
    input.down &= ~block;
    input.repeat &= ~block;
    if (choice_wait > 0)
        input.touch_down = false;
    return real;
}

static void choice_after(Input real)
{
    input = real;
    if (choice_mode() && !choice_open) {
        choice_begin();
    } else if (choice_open && !choice_mode()) {
        choice_end(PM_FADE_OUT == mode || PM_FADE_IN == mode);
    } else if (choice_open) {
        choice_frames++;
        if (choice_wait > 0)
            choice_wait--;
        // A triangle-wave glow while it waits, steady once it takes input.
        int phase = choice_frames % (2 * CHOICE_PULSE);
        int tri = phase < CHOICE_PULSE ? phase : 2 * CHOICE_PULSE - phase;
        choice_glow(choice_wait > 0 ? tri * CHOICE_GLOW / CHOICE_PULSE : 0);
        if (0 == choice_frames % CHOICE_PULSE)
            choice_banner();
    }
}

static void draw_stats(void)
{
    int alive = run_enemies_alive(&run);
    ui_text(5, 3, TXT_NORMAL, "Enemies");
    ui_text(15, 3, alive ? TXT_RED : TXT_GREEN, "%-3d", alive);
    ui_text(5, 5, TXT_NORMAL, "Kills");
    ui_text(15, 5, TXT_GOLD, "%-3d", run.kills);
    ui_text(5, 7, TXT_NORMAL, "Gold");
    ui_text(15, 7, TXT_GOLD, "%-5d", run.gold);
    shown_alive = alive;
    shown_gold = run.gold;
}

// "KEY action", the key in gold.
static void key_label(int x, int y, const char *key, const char *action)
{
    ui_text(x, y, TXT_GOLD, "%s", key);
    ui_text(x + (int)strlen(key) + 1, y, TXT_NORMAL, "%s", action);
}

static void draw_controls(void)
{
    bool lefty = game.options.left_handed;
    ui_panel(2, 10, 28, 12);
    key_label(5, 12, lefty ? "Left" : "A", "sword");
    key_label(17, 12, lefty ? "Down" : "B", "dash");
    key_label(5, 14, lefty ? "Right" : "Y", "dagger");
    key_label(17, 14, lefty ? "Up" : "X", "blast");
    key_label(5, 16, lefty ? "ABXY" : "D-pad", "move");
    key_label(5, 18, "SELECT", "zoom");
    key_label(17, 18, "START", "pause");
}

static void draw_door_preview(void)
{
    ui_panel(2, 10, 28, 13);
    ui_text_center(11, TXT_DIM, "- choose a door -");
    for (int i = 0; i < run.room.door_count; i++) {
        const Door *d = &run.room.doors[i];
        int y = 13 + i * 3;
        ui_icon(i, RT_BOSS == d->leads_to ? ICON_SKULL : (UiIcon)d->leads_to,
                4 * 8, y * 8 - 4);
        ui_text(7, y, TXT_NORMAL, "%s", reward_names[d->leads_to]);
        ui_text(7, y + 1, TXT_DIM, "%s wall", side_names[d->side]);
    }
}

// The camp's bottom screen: records and what to do.
static void draw_hub_bottom(void)
{
    ui_clear();
    ui_panel(2, 0, 28, 10);
    ui_text_center(1, TXT_GOLD, "- CAMP -");
    const MetaProgress *m = &game.meta;
    ui_text(5, 3, TXT_NORMAL, "Best depth");
    ui_text(19, 3, TXT_GOLD, "%5lu", (unsigned long)m->best_depth);
    ui_text(5, 5, TXT_NORMAL, "Runs");
    ui_text(19, 5, TXT_GOLD, "%5lu", (unsigned long)m->total_runs);
    ui_text(5, 7, TXT_NORMAL, "Gold");
    ui_text(19, 7, TXT_GOLD, "%5lu", (unsigned long)m->currency);
    ui_panel(2, 11, 28, 12);
    ui_icon(0, ICON_COMBAT, 4 * 8, 13 * 8 - 4);
    ui_text(7, 13, TXT_NORMAL, "The crypt: descend");
    ui_icon(1, ICON_IDOL, 4 * 8, 16 * 8 - 4);
    ui_text(7, 16, TXT_NORMAL, "The shrine: upgrades");
    ui_icon(2, ICON_SKULL, 4 * 8, 19 * 8 - 4);
    ui_text(7, 19, TXT_NORMAL, "The dummy: practice");
}

static void draw_shrine_prompt(bool near)
{
    if (near)
        ui_text_center(21, TXT_GOLD, "%s: buy upgrades",
                       game.options.left_handed ? "Left" : "A");
    else
        ui_clear_row(21);
}

static const Enemy *find_boss(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (run.enemies[i].active && enemy_is_boss(run.enemies[i].type))
            return &run.enemies[i];
    return NULL;
}

// The boss's name and health bar (bottom screen, during a boss fight).
#define BOSS_BAR_X 4
#define BOSS_BAR_W 24
static int shown_boss_hp = -1;

static void draw_boss_bar(void)
{
    const Enemy *b = find_boss();
    int hp = b && EST_DYING != b->state ? b->hp : 0;
    int cells = b && b->max_hp ? (hp * BOSS_BAR_W + b->max_hp - 1) / b->max_hp
                               : 0;
    for (int i = 0; i < BOSS_BAR_W; i++)
        ui_highlight(BOSS_BAR_X + i, 15, 1, i < cells);
    shown_boss_hp = hp;
}

static void draw_boss_panel(void)
{
    const Enemy *b = find_boss();
    ui_panel(2, 10, 28, 12);
    ui_text_center(12, TXT_RED, "- %s -",
                   boss_name(b ? b->type : ENEMY_WARLORD));
    ui_text_center(18, TXT_DIM, "Beat it to go on");
    draw_boss_bar();
}

// The lower screen's top panel during a run: depth, room type and stats.
static void draw_room_header(void)
{
    ui_panel(2, 0, 28, 10);
    ui_text_center(1, TXT_DIM, "- DEPTH %d: %s -", run.depth,
                   room_names[run.room.type]);
    draw_stats();
}

static void draw_bottom(void)
{
    if (run.room.hub) {
        draw_hub_bottom();
        return;
    }
    ui_clear();
    draw_room_header();
    if (ROOM_CLEAR == run.status)
        draw_door_preview();
    else if (RT_BOSS == run.room.type)
        draw_boss_panel();
    else
        draw_controls();
}

// Boon picker

static bool open_hint(HintId id, bool then_boons);
static void hint_after_clear(void);
static void resume(void);

static BoonId offer[BOON_CHOICES];
static int offer_count, offer_cursor;

#define BOON_ROW0  13
#define BOON_STEP  4

static void draw_boon_picker(void)
{
    ui_clear();
    draw_room_header();
    ui_panel(1, 10, 30, 14);
    ui_text_center(11, TXT_GOLD, "- choose a boon -");
    for (int i = 0; i < offer_count; i++) {
        const BoonDef *b = &boon_defs[offer[i]];
        int y = BOON_ROW0 + i * BOON_STEP;
        bool sel = i == offer_cursor;
        ui_highlight(2, y, 28, sel);
        ui_text(4, y, sel ? TXT_GOLD : TXT_NORMAL, "%s", b->name);
        if (b->max > 1)
            ui_text(22, y, TXT_DIM, "%d/%d", run.boons[offer[i]] + 1, b->max);
        ui_text(4, y + 1, TXT_DIM, "%s", b->desc);
    }
}

static void open_boon_picker(void)
{
    offer_count = boon_offer(&run, offer);
    if (0 == offer_count) {
        run.boon_pending = false;
        hint_after_clear();
        return;
    }
    if (open_hint(HINT_BOONS, true))
        return;   // the picker opens when the tip is dismissed
    offer_cursor = 0;
    mode = PM_BOON;
    draw_boon_picker();   // the banner: choice_begin()
}

static void update_boon_picker(void)
{
    int old = offer_cursor;
    if (input.repeat & KEY_UP)
        offer_cursor = (offer_cursor + offer_count - 1) % offer_count;
    if (input.repeat & KEY_DOWN)
        offer_cursor = (offer_cursor + 1) % offer_count;
    bool take = 0 != (input.down & KEY_A);
    for (int i = 0; i < offer_count && input.touch_down; i++) {
        int y = BOON_ROW0 + i * BOON_STEP;
        if (input_touched_rect(2 * 8, (y - 1) * 8, 28 * 8, 3 * 8)) {
            take = i == offer_cursor;   // tap twice: select, then take
            offer_cursor = i;
        }
    }
    if (take) {
        run_take_boon(&run, offer[offer_cursor]);
        audio_sfx(SND_CHIME);
        mode = PM_PLAY;
        draw_hud();
        draw_bottom();
        banner(TXT_GOLD, boon_defs[offer[offer_cursor]].name);
        hint_after_clear();
        return;
    }
    if (offer_cursor != old) {
        audio_sfx(SND_SELECT);
        draw_boon_picker();
    }
}

// The track for where the player is (enter_current_room).
static Music room_music(void)
{
    if (run.room.hub)
        return MUSIC_CAMP;
    if (RT_BOSS == run.room.type && ROOM_CLEAR != run.status)
        return MUSIC_BOSS;
    return (Music)area_defs[run.room.area].music;
}

// The room's tip is looked up on its first frame of play (after the fade).
static bool room_hint_checked;

static void enter_current_room(void)
{
    room_hint_checked = false;
    room_fade_reset(fade);
    audio_music(room_music());
    render_void_init(run.room.seed);
    parallax_set_theme(run.room.hub ? PLX_THEME_NIGHT : sky_theme(run.depth));
    parallax_set_shimmer(!run.room.hub && AREA_MOLTEN == run.room.area);
    fx_clear(&effects);
    hitstop = shake = ring_timer = flash_timer = 0;
    for (int i = 0; i < AFTERIMAGES; i++)
        afterimages[i].life = 0;
    camera_init(&cam, cam.zoom, FX_INT(ROOM_SIZE), run.player.x, run.player.z);
    shown_status = run.status;
    banner_timer = 0;
    video_top_clear();
    draw_hud();
    draw_bottom();
    if (run.room.hub) {
        banner(TXT_GOLD, "The camp. The crypt awaits.");
    } else if (RT_BOSS == run.room.type) {
        const Enemy *b = find_boss();
        banner(TXT_RED, boss_name(b ? b->type : ENEMY_WARLORD));
        shown_boss_hp = -1;
    } else if (run.depth > 1 && 1 == run.depth % AREA_DEPTHS) {
        banner(TXT_CYAN, area_defs[room_area(run.depth)].banner);
    } else if (game.meta.best_depth > 0
               && (uint32_t)run.depth == game.meta.best_depth + 1) {
        // Past the record (once per run: the next room is one deeper).
        banner(TXT_GOLD, "New best depth!");
        audio_sfx(SND_CHIME);
    } else if (run.events & EV_HEAL) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Rested by the fire: +%d", run.reward_amount);
        banner(TXT_GREEN, msg);
    } else if (RT_REST == run.room.type) {
        banner(TXT_GREEN, "A quiet campfire");
    }
}

// The pause screens' upper panel: the camp or the run's stats.
static void draw_pause_top(void)
{
    ui_clear();
    if (run.room.hub) {
        ui_panel(2, 0, 28, 10);
        ui_text_center(1, TXT_GOLD, "- CAMP -");
        ui_text_center(5, TXT_DIM, "Best depth %lu",
                       (unsigned long)game.meta.best_depth);
    } else {
        draw_room_header();
    }
}

static void draw_pause(void)
{
    draw_pause_top();
    ui_panel(6, 10, 20, 12);
    ui_text_center(11, TXT_GOLD, "PAUSED");
    ui_menu_draw(&pause_menu);
    banner_held(TXT_GOLD, "PAUSED");
    // The boons taken this run, on the top screen.
    int row = BANNER_ROW + 2;
    for (int i = 0; i < BOON_COUNT && row < 20; i++) {
        if (!run.boons[i])
            continue;
        if (BANNER_ROW + 2 == row)
            video_top_text(2, row++, TXT_DIM, "Boons:");
        if (run.boons[i] > 1)
            video_top_text(3, row++, TXT_GOLD, "%s x%d", boon_defs[i].name,
                           run.boons[i]);
        else
            video_top_text(3, row++, TXT_GOLD, "%s", boon_defs[i].name);
    }
    // Code Genie's cheats, under the boons while there is room.
    if (run.cheats && row < 19) {
        video_top_putc(2, ++row, GLYPH_LAMP, TXT_CYAN);
        video_top_text(4, row++, TXT_CYAN, "Code Genie:");
        for (int i = 0; i < CHEAT_COUNT && row < 21; i++)
            if (cheat_on(run.cheats, (CheatId)i))
                video_top_text(3, row++, TXT_CYAN, "%s", cheat_defs[i].name);
    }
}

// Tips (hint.h): a panel on the lower screen; the game waits. "No more
// tips" turns the Hints option off.

#define HINT_INPUT_DELAY 20   // frames before the tip takes input, so a
                              // button held in the fight can't skip it
static HintId hint_open = HINT_NONE;
static bool hint_then_boons;
static int hint_frames;

static bool open_hint(HintId id, bool then_boons)
{
    if (!game.options.hints || HINT_NONE == id || hint_seen(&game.meta, id))
        return false;
    hint_open = id;
    hint_then_boons = then_boons;
    hint_frames = 0;
    mode = PM_HINT;
    audio_sfx(SND_CHIME);
    ui_clear();
    ui_panel(2, 2, 28, 15);
    ui_text_center(3, TXT_GOLD, "- %s -", hint_title(id));
    for (int i = 0; i < HINT_LINES; i++) {
        const char *line = hint_line(id, i, game.options.left_handed);
        if (line)
            ui_text(4, 5 + i * 2, TXT_NORMAL, "%s", line);
    }
    // Left-handed, the face buttons move: "never" moves to SELECT so a
    // step can't turn the tips off.
    const char *never = game.options.left_handed ? "SEL" : "X";
    ui_panel(1, 18, 12, 3);
    ui_text(2, 19, TXT_GOLD, "A");
    ui_text(4, 19, TXT_NORMAL, "Got it");
    ui_panel(13, 18, 18, 3);
    ui_text(14, 19, TXT_GOLD, "%s", never);
    ui_text(15 + (int)strlen(never), 19, TXT_NORMAL, "No more tips");
    banner_held(TXT_GOLD, "TIP: see the lower screen");
    return true;
}

static void update_hint(void)
{
    if (++hint_frames < HINT_INPUT_DELAY)
        return;
    uint32_t never_key = game.options.left_handed ? KEY_SELECT : KEY_X;
    bool ok = (input.down & (KEY_A | KEY_B))
           || input_touched_rect(8, 18 * 8, 12 * 8, 3 * 8);
    bool never = (input.down & never_key)
              || input_touched_rect(13 * 8, 18 * 8, 18 * 8, 3 * 8);
    if (!ok && !never)
        return;
    audio_sfx(never ? SND_BACK : SND_SELECT);
    hint_mark_seen(&game.meta, hint_open);
    if (never)
        game.options.hints = 0;
    save_write(&game);
    hint_open = HINT_NONE;
    if (hint_then_boons) {
        open_boon_picker();
    } else {
        resume();
    }
}

// A cleared room with its doors open (after any boon): the doors tip.
static void hint_after_clear(void)
{
    if (!run.room.hub && run.room.door_count > 0)
        open_hint(HINT_DOORS, false);
}

// Waypoints: at the crypt, once a boss has fallen, where to begin.

static UiMenu waypoint_menu = {
    .x = 6, .y = 6, .w = 22, .step = 3,
    .has_back = true,
};
static char waypoint_labels[AREA_COUNT][32];
static int waypoint_areas[AREA_COUNT];

static void draw_waypoints(void)
{
    ui_clear();
    ui_panel(3, 2, 26, 4 + waypoint_menu.count * 3);
    ui_text_center(3, TXT_GOLD, "- DESCEND TO -");
    ui_menu_draw(&waypoint_menu);
    for (int i = 0; i < waypoint_menu.count; i++) {
        int a = waypoint_areas[i];
        int y = waypoint_menu.y + i * waypoint_menu.step + 1;
        if (0 == a)
            ui_text(waypoint_menu.x, y, TXT_DIM, "depth 1");
        else
            ui_text(waypoint_menu.x, y, TXT_DIM, "depth %d, +%d boons",
                    area_first_depth(a), a * WAYPOINT_BOONS);
    }
    ui_text_center(19, TXT_DIM, "Gold counts only the");
    ui_text_center(20, TXT_DIM, "depths you go down.");
    ui_back_button();
}

static void open_waypoints(void)
{
    int n = 0;
    for (int a = 0; a < AREA_COUNT; a++) {
        if (!meta_waypoint_open(&game.meta, a))
            continue;
        snprintf(waypoint_labels[n], sizeof(waypoint_labels[n]), "%s",
                 area_defs[a].waypoint);
        waypoint_menu.items[n] = waypoint_labels[n];
        waypoint_areas[n++] = a;
    }
    waypoint_menu.count = n;
    waypoint_menu.cursor = n - 1;   // the deepest open one
    for (int i = 0; i < n; i++)
        if (waypoint_areas[i] == start_area)
            waypoint_menu.cursor = i;
    mode = PM_WAYPOINT;
    draw_waypoints();   // the banner: choice_begin()
}

static void update_waypoints(void)
{
    switch (ui_menu_update(&waypoint_menu)) {
    case UI_ACTIVATE:
        start_area = waypoint_areas[waypoint_menu.cursor];
        begin_fade_out();
        draw_bottom();
        break;
    case UI_BACK:
        // Step back out of the doorway.
        run.player.z += FX(0.8);
        resume();
        break;
    default:
        break;
    }
}

// "Abandon this run?": the run's stats stay up top, the dialog replaces
// the pause menu.
static void draw_confirm_abandon(void)
{
    draw_pause_top();
    ui_panel(3, 11, 26, 8);
    ui_text_center(12, TXT_RED, "Abandon this run?");
    ui_text_center(14, TXT_DIM, "Gold found is kept");
    ui_text(7, 16, TXT_GOLD, "A");
    ui_text(9, 16, TXT_NORMAL, "Yes");
    ui_text(18, 16, TXT_GOLD, "B");
    ui_text(20, 16, TXT_NORMAL, "No");
}

static void open_pause(void)
{
    pause_menu.items[PAUSE_ABANDON] = run.room.hub ? "Main menu"
                                                   : "Abandon run";
    mode = PM_PAUSED;
    pause_menu.cursor = 0;
    audio_sfx(SND_SELECT);
    draw_pause();
}

static void resume(void)
{
    mode = PM_PLAY;
    video_top_clear();   // the banner and the boon list
    draw_hud();
    draw_bottom();
}

// A new run, from the camp's crypt.
static void start_run(void)
{
    uint32_t seed = rng_hash3((uint32_t)time(NULL), game_frames, 0xDE1u);
    run_start_at(&run, seed, start_area);
    run_apply_upgrades(&run, cheat_upgrades(cheat_active, game.meta.upgrades));
    // Code Genie: a run with cheats marks the slot for good; saved
    // with the next write (a boss's waypoint, or the run's end).
    run.cheats = cheat_active;
    game.meta.cheats_used |= cheat_active;
    fx_init(&effects, seed);
}

// The camp: entered from the menu, after a run's summary, and back from the
// upgrade shrine.
static void start_hub(void)
{
    run_start_hub(&run);
    run_apply_upgrades(&run, cheat_upgrades(cheat_active, game.meta.upgrades));
    run.cheats = cheat_active;   // the camp marks nothing
    fx_init(&effects, game_frames);
}


// Entered from the menu or summary (the camp, or straight into a run with
// "Descend again"), back from Options (the pause menu) or from the shrine.
static void play_enter(void)
{
    render_set_char_scale(cheat_on(cheat_active, CHEAT_GIANT)
                          ? FX_ONE * CHEAT_GIANT_PCT / 100 : FX_ONE);
    // Back from Options opened in the pause menu: keep the run.
    if (ST_OPTIONS == state_previous()) {
        audio_music(room_music());
        video_top_clear();
        draw_hud();
        mode = PM_PAUSED;
        draw_pause();
        return;
    }
    // "Descend again" from the summary: straight into a new run.
    if (play_descend_now) {
        play_descend_now = false;
        start_run();
        cam.zoom = (ZoomLevel)game.options.default_zoom;
        audio_sfx(SND_PORTAL);
        enter_current_room();
        mode = PM_FADE_IN;
        mode_timer = FADE_FRAMES;
        setBrightness(3, -16);
        return;
    }
    // Back from the shrine: the camp, with upgrades bought just now.
    fx px = 0, pz = 0;
    bool from_shrine = ST_UPGRADES == state_previous() && run.room.hub;
    if (from_shrine) {
        px = run.player.x;
        pz = run.player.z;
    }
    start_hub();
    if (from_shrine) {
        run.player.x = px;
        run.player.z = pz;
    }
    cam.zoom = (ZoomLevel)game.options.default_zoom;
    mode = PM_PLAY;
    setBrightness(3, 0);
    enter_current_room();
}

static void play_exit(void)
{
    choice_end(false);
    render_set_char_scale(FX_ONE);
    setBrightness(3, 0);
    parallax_set_theme(0);
    parallax_set_shimmer(false);
    ui_icons_clear();
}

// Update

// The buttons for each action. Left-handed swaps the D-pad and the face
// buttons, mirrored so each action keeps its place under the thumb: the
// sword (A, right) moves to D-pad left, the dagger (Y, left) to D-pad right.
typedef struct {
    uint32_t up, down, left, right;
    uint32_t attack, dash, dagger, blast;
} Controls;

static const Controls controls_std = {
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_Y, KEY_X,
};
static const Controls controls_left = {
    KEY_X, KEY_B, KEY_Y, KEY_A, KEY_LEFT, KEY_DOWN, KEY_RIGHT, KEY_UP,
};

static const Controls *controls(void)
{
    return game.options.left_handed ? &controls_left : &controls_std;
}

// The sword button went down this frame (the shrine's "use" too).
static bool attack_pressed(void)
{
    return 0 != (input.down & controls()->attack);
}

// The buttons as this frame's PlayerInput, for either hand.
static PlayerInput read_player_input(void)
{
    const Controls *c = controls();
    int sx = 0, sy = 0;
    if (input.held & c->right) sx++;
    if (input.held & c->left)  sx--;
    if (input.held & c->down)  sy++;
    if (input.held & c->up)    sy--;

    // Auto-attack: holding the sword button swings again as soon as the
    // last swing ends.
    uint32_t swing = game.options.auto_attack ? input.held : input.down;
    PlayerInput in = {
        .move = dir8_from_input(sx, sy),
        .dash = 0 != (input.down & c->dash),
        .attack = 0 != (swing & c->attack),
        .throw_dagger = 0 != (input.down & c->dagger),
        .blast = 0 != (input.down & c->blast),
    };
    return in;
}

static void update_camera(void)
{
    if (input.down & KEY_SELECT)
        camera_toggle_zoom(&cam);
    camera_follow(&cam, run.player.x, run.player.z);
    camera_update(&cam);

    fx half = FX_INT(ROOM_SIZE) / 2;
    fx r = camera_screen_right(cam.x - half, cam.z - half);
    fx u = camera_screen_up(cam.x - half, cam.z - half);
    parallax_update(16, 6);
    parallax_set_camera(FX_TO_INT(r * 20), FX_TO_INT(-u * 20));
}

// Records the run, saves, and shows the summary.
static void finish_run(bool abandoned)
{
    RunResult *r = &game.last_run;
    *r = (RunResult){
        .depth = run.depth,
        .kills = run.kills,
        .gold = run.gold,
        .rooms_cleared = run.rooms_cleared,
        .start_depth = run.start_depth,
        .bosses = run.bosses,
        .seconds = (int)(run.time / 60),
        .cause = abandoned ? CAUSE_ABANDONED : run.hurt_by,
        .abandoned = abandoned,
        .cheated = 0 != run.cheats,
        .seed = run.seed,
    };
    for (int i = 0; i < BOON_COUNT; i++)
        r->boons[i] = run.boons[i];
    meta_finish_run(&game.meta, r);
    meta_push_history(&game.history, r);
    save_write(&game);
    state_set(ST_SUMMARY);
}

// A sound for each kind of thing that happened this frame.
static void play_event_sounds(void)
{
    uint32_t ev = run.events;
    if (ev & EV_PLAYER_DIED) {
        audio_sfx(SND_DEATH);
        audio_music(MUSIC_NONE);
    }
    else if (ev & EV_PLAYER_HURT)  audio_sfx(SND_HURT);
    if (ev & (EV_GOLD | EV_HEAL))  audio_sfx(SND_PICKUP);
    if (ev & EV_ENEMY_DIED)        audio_sfx(SND_POP);
    else if (ev & EV_ENEMY_HIT)    audio_sfx(SND_HIT);
    else if (ev & EV_PLAYER_SWING) audio_sfx(SND_SWING);
    if (ev & EV_SHOT_BLOCKED)      audio_sfx(SND_HIT);
    if (ev & EV_POT_BREAK)         audio_sfx(SND_SHATTER);
    if (ev & (EV_BARREL_BREAK | EV_CRATE_BREAK)) audio_sfx(SND_WOOD);
    if (ev & EV_FUSE_LIT)          audio_sfx(SND_TINK);
    if (ev & EV_EXPLODE)           audio_sfx(SND_EXPLODE);
    if (ev & EV_COIN)              audio_sfx(SND_COIN);
    if (ev & EV_HEART)             audio_sfx(SND_PICKUP);
    if (ev & (EV_PLAYER_FELL | EV_ENEMY_FELL)) audio_sfx(SND_FALL);
    // In a boss arena: the Warlord roars as he charges; the Slime King's
    // landings splat.
    bool boss = RT_BOSS == run.room.type;
    bool king = boss && ENEMY_SLIME_KING == area_defs[run.room.area].boss;
    if (ev & EV_CHARGE)  audio_sfx(boss && !king ? SND_ROAR : SND_DASH);
    if (ev & EV_SLAM)    audio_sfx(king ? SND_SPLAT : SND_SLAM);
    if (ev & EV_SUMMON)            audio_sfx(SND_POP);
    if (ev & EV_ZONE_MARKED)       audio_sfx(SND_TINK);
    if (ev & EV_ZONE_BURST)        audio_sfx(SND_BLAST);
    if (ev & EV_PLAYER_DASH)       audio_sfx(SND_DASH);
    if (ev & EV_ENEMY_SHOOT)       audio_sfx(SND_SHOOT);
    if (ev & EV_DOOR_ENTER)
        audio_sfx(SND_DOOR);
    if (ev & EV_DAGGER_THROW)      audio_sfx(SND_THROW);
    if (ev & EV_DAGGER_WALL)       audio_sfx(SND_TINK);
    if (ev & EV_BLAST)             audio_sfx(SND_BLAST);
    if (ev & EV_METER_FULL)        audio_sfx(SND_CHARGED);
    for (int i = 0; i < run.hit_count; i++)
        if (HIT_TRAP == run.hits[i].kind)
            audio_sfx(HAZ_SPIKES == run.hits[i].amount ? SND_SPIKES
                      : HAZ_ROCKFALL == run.hits[i].amount ? SND_SLAM
                      : SND_SHOOT);
}

// Hit-stop and shake only ever grow from a new hit, never cut one short.
static void add_hitstop(int frames)
{
    hitstop = imax(hitstop, frames);
}

static void add_shake(int frames)
{
    shake = imax(shake, frames);
}

// A ring spreading over the floor from (x, z) to radius.
static void start_ring(fx x, fx z, fx radius)
{
    ring_timer = RING_FRAMES;
    ring_x = x;
    ring_z = z;
    ring_radius = radius;
}

// Particles, damage numbers, hit-stop and shake for this frame's hits.
static void spawn_effects(void)
{
    const fx chest = FX(0.5);
    for (int i = 0; i < run.hit_count; i++) {
        const RunHit *h = &run.hits[i];
        switch (h->kind) {
        case HIT_ENEMY:
            fx_burst(&effects, PART_SPARK, 5, h->x, chest, h->z, FX(0.05));
            if (game.options.hit_numbers)
                fx_number(&effects, h->amount, h->x, h->z, false);
            add_hitstop(HITSTOP_HIT);
            break;
        case HIT_KILL:
            fx_burst(&effects, PART_SPARK, 8, h->x, chest, h->z, FX(0.07));
            fx_burst(&effects, PART_DUST, 6, h->x, FX(0.2), h->z, FX(0.04));
            if (game.options.hit_numbers)
                fx_number(&effects, h->amount, h->x, h->z, true);
            add_hitstop(HITSTOP_KILL);
            break;
        case HIT_POT:
            fx_burst(&effects, PART_SHARD, 8, h->x, FX(0.3), h->z, FX(0.05));
            fx_burst(&effects, PART_DUST, 4, h->x, FX(0.2), h->z, FX(0.03));
            break;
        case HIT_DAGGER_WALL:
            fx_burst(&effects, PART_SPARK, 4, h->x, chest, h->z, FX(0.04));
            fx_burst(&effects, PART_DUST, 3, h->x, chest, h->z, FX(0.02));
            break;
        case HIT_SHOT:
            fx_burst(&effects, PART_EMBER, 6, h->x, FX(0.45), h->z, FX(0.04));
            break;
        case HIT_BARREL:
            fx_burst(&effects, PART_SPLINTER, 10, h->x, FX(0.4), h->z, FX(0.05));
            fx_burst(&effects, PART_DUST, 4, h->x, FX(0.2), h->z, FX(0.03));
            break;
        case HIT_CHIP:
            fx_burst(&effects, PART_SPLINTER, 3, h->x, FX(0.4), h->z, FX(0.04));
            break;
        case HIT_CRATE:
            fx_burst(&effects, PART_SPLINTER, 14, h->x, FX(0.5), h->z, FX(0.06));
            fx_burst(&effects, PART_DUST, 6, h->x, FX(0.3), h->z, FX(0.04));
            break;
        case HIT_FUSE:
            fx_burst(&effects, PART_SPARK, 4, h->x, FX(0.7), h->z, FX(0.03));
            break;
        case HIT_EXPLODE:
            fx_burst(&effects, PART_EMBER, 14, h->x, FX(0.4), h->z, FX(0.1));
            fx_burst(&effects, PART_CINDER, 8, h->x, FX(0.5), h->z, FX(0.08));
            fx_burst(&effects, PART_SMOKE, 8, h->x, FX(0.4), h->z, FX(0.05));
            fx_burst(&effects, PART_WISP, 4, h->x, FX(0.8), h->z, FX(0.02));
            fx_burst(&effects, PART_SPARK, 10, h->x, FX(0.4), h->z, FX(0.12));
            start_ring(h->x, h->z, EXPLODE_RADIUS);
            shake = SHAKE_BLAST + 2;
            add_hitstop(4);
            break;
        case HIT_COIN:
            fx_burst(&effects, PART_GOLD, 6, h->x, FX(0.3), h->z, FX(0.04));
            break;
        case HIT_HEART:
            fx_burst(&effects, PART_BLOOD, 5, h->x, FX(0.4), h->z, FX(0.03));
            break;
        case HIT_ZONE:
            fx_burst(&effects, PART_EMBER, 12, h->x, FX(0.2), h->z, FX(0.08));
            fx_burst(&effects, PART_SMOKE, 6, h->x, FX(0.2), h->z, FX(0.04));
            break;
        case HIT_SLAM:
            fx_burst(&effects, PART_DUST, 10, h->x, FX(0.3), h->z, FX(0.06));
            fx_burst(&effects, PART_SPARK, 4, h->x, FX(0.6), h->z, FX(0.05));
            add_shake(SHAKE_HURT);
            break;
        case HIT_SUMMON:
            fx_burst(&effects, PART_SMOKE, 8, h->x, FX(0.2), h->z, FX(0.03));
            break;
        case HIT_SHOCK:
            start_ring(h->x, h->z, SHOCK_RADIUS);
            fx_burst(&effects, PART_SPARK, 10, h->x, FX(0.3), h->z, FX(0.08));
            add_shake(SHAKE_HURT);
            break;
        case HIT_FALL:
            fx_burst(&effects, PART_DUST, 8, h->x, FX(0.05), h->z, FX(0.04));
            break;
        case HIT_TRAP:
            if (HAZ_ROCKFALL == h->amount) {
                fx_burst(&effects, PART_DUST, 12, h->x, FX(0.2), h->z, FX(0.06));
                fx_burst(&effects, PART_SMOKE, 6, h->x, FX(0.3), h->z, FX(0.04));
                add_shake(3);
                break;
            }
            fx_burst(&effects, HAZ_FIRE == h->amount ? PART_EMBER : PART_DUST,
                     6, h->x, FX(0.2), h->z, FX(0.04));
            break;
        }
    }
    // Ongoing: lit fuses fizz, burning vents throw embers.
    for (int i = 0; i < run.room.decor_count; i++)
        if (run.fuse[i] && 0 == (game_frames & 3))
            fx_emit(&effects, PART_SPARK, run.room.decor[i].x, FX(0.95),
                    run.room.decor[i].z, 0, FX(0.03), 0, 10);
    for (int i = 0; i < run.room.hazard_count; i++) {
        const Hazard *hz = &run.room.hazards[i];
        if (HAZ_FIRE != hz->kind)
            continue;
        TrapState st = hazard_state(hz, run.frames, NULL);
        uint32_t r = rng_hash3(game_frames, (uint32_t)i, 0xF1Eu);
        fx jx = (fx)(r % 1024) - 512, jz = (fx)((r >> 10) % 1024) - 512;
        if (TRAP_WARN == st && 0 == (game_frames & 7)) {
            // About to burst: the vent smokes and spits a cinder.
            fx_emit(&effects, PART_WISP, hz->x + jx / 2, FX(0.15), hz->z + jz / 2,
                    0, FX(0.008), 0, 50);
            fx_emit(&effects, PART_CINDER, hz->x, FX(0.1), hz->z, jx / 64,
                    FX(0.03), jz / 64, 24);
        } else if (TRAP_ACTIVE == st && 0 == (game_frames & 3)) {
            // Burning: licks of flame up the column, cinders off the top.
            fx_emit(&effects, PART_FLAME, hz->x + jx / 2,
                    FX(0.3) + (fx)((r >> 20) % 4096) * 3 / 2, hz->z + jz / 2,
                    0, FX(0.03), 0, 14);
            fx_emit(&effects, PART_CINDER, hz->x + jx / 3, FX(1.9),
                    hz->z + jz / 3, jx / 40, FX(0.035), jz / 40, 36);
        }
    }
    const Player *p = &run.player;
    if (run.events & EV_BLAST) {
        start_ring(p->x, p->z, p->blast_radius);
        shake = SHAKE_BLAST;
        hitstop = HITSTOP_BLAST;
        fx_burst(&effects, PART_SPARK, 16, p->x, chest, p->z, FX(0.12));
    }
    if (run.events & EV_PLAYER_HURT) {
        shake = SHAKE_HURT;
        fx_burst(&effects, PART_BLOOD, 6, p->x, chest, p->z, FX(0.05));
    }
    if (run.events & EV_METER_FULL)
        banner(TXT_CYAN, "Blast ready!  Press X");
    if (run.events & EV_PLAYER_FELL) {
        shake = SHAKE_HURT;
        uint8_t pit = area_defs[run.room.area].pit_cause;
        banner(TXT_RED, CAUSE_LAVA == pit ? "Into the lava!"
                      : CAUSE_WATER == pit ? "Into the water!" : "You fell!");
    }
}

// Pressing an action with nothing to use: a dry click, and its empty HUD
// slots blink.
static void check_empty_actions(const PlayerInput *in)
{
    const Player *p = &run.player;
    unsigned what = 0;
    if (in->dash && 0 == p->dash_charges && PACT_DASH != p->action)
        what |= HUD_BLINK_DASH;
    if (in->throw_dagger && 0 == p->daggers)
        what |= HUD_BLINK_DAGGER;
    if (in->blast && p->meter < p->meter_max)
        what |= HUD_BLINK_METER;
    if (what) {
        audio_sfx(SND_EMPTY);
        flash_what = what;
        flash_timer = EMPTY_FLASH;
    }
}

// Ambient life: dust drifting through the air, embers rising off torches.
static void ambient_particles(void)
{
    uint32_t r = rng_hash3(game_frames, run.room.seed, 0xA1Eu);
    // The Molten Depths: embers rise from the lava.
    if (AREA_MOLTEN == run.room.area && !run.room.hub && 0 == (game_frames & 3)) {
        int cx = (int)(r % ROOM_SIZE), cz = (int)((r >> 8) % ROOM_SIZE);
        bool lava = CELL_PIT == run.room.cells[cz][cx];
        if (lava || 0 == (r >> 16) % 4)
            fx_emit(&effects, 0 == (r >> 28) % 3 ? PART_CINDER : PART_EMBER,
                    FX_INT(cx) + (fx)((r >> 4) % 4096),
                    lava ? -FX(0.2) : FX(0.1), FX_INT(cz) + (fx)((r >> 20) % 4096),
                    (fx)((r >> 3) % 64) - 32, FX(0.016), 0, 60);
    }
    if (0 == (game_frames % 6)) {
        fx x = FX_INT(r % ROOM_SIZE) + (fx)((r >> 8) % 4096);
        fx z = FX_INT((r >> 12) % ROOM_SIZE) + (fx)((r >> 20) % 4096);
        fx_emit(&effects, PART_MOTE, x, FX(0.3) + (fx)((r >> 4) % 4096), z,
                FX(0.004), FX(0.003), -FX(0.002), 90);
    }
    // Torches: now and then a cinder or an ember off the flame, a lick of
    // flame at its tip, a thin wisp of smoke. Each torch on its own beat.
    for (int i = 0; i < run.room.decor_count; i++) {
        const Decor *d = &run.room.decor[i];
        if (DECOR_TORCH != d->kind)
            continue;
        uint32_t beat = game_frames + (uint32_t)i * 7;
        uint32_t h = rng_hash3(game_frames, (uint32_t)i, 0x70Cu);
        fx nx, nz;
        side_inward(d->wall, &nx, &nz);
        fx x = d->x + fx_mul(nx, FX(0.15)), z = d->z + fx_mul(nz, FX(0.15));
        fx drift = (fx)(h % 64) - 32;
        if (0 == beat % 9 && 0 != (h >> 8) % 3)
            fx_emit(&effects, (h >> 12) & 1 ? PART_CINDER : PART_EMBER,
                    x, FX(1.15), z, drift, FX(0.014) + (fx)((h >> 16) % 24), 0,
                    34 + (int)((h >> 20) % 16));
        if (0 == beat % 13)
            fx_emit(&effects, PART_FLAME, x + drift * 3, FX(1.3), z, 0,
                    FX(0.012), 0, 10);
        if (0 == beat % 41)
            fx_emit(&effects, PART_WISP, x, FX(1.6), z, drift / 4,
                    FX(0.006), 0, 64);
    }
    // The campfire (rest rooms, the camp): a steady crackle of flame
    // licks, cinders spiralling up, a column of smoke.
    const Room *room = &run.room;
    if (room->feature_x >= 0
        && CELL_FIRE == room_cell(room, room->feature_x, room->feature_z)) {
        fx x = FX_INT(room->feature_x) + FX_HALF;
        fx z = FX_INT(room->feature_z) + FX_HALF;
        uint32_t h = rng_hash3(game_frames, room->seed, 0xCA4Fu);
        fx jx = (fx)(h % 1536) - 768, jz = (fx)((h >> 11) % 1536) - 768;
        if (0 == game_frames % 3)
            fx_emit(&effects, PART_FLAME, x + jx / 2, FX(0.35), z + jz / 2,
                    0, FX(0.024), 0, 12);
        if (0 == game_frames % 5)
            fx_emit(&effects, (h >> 22) & 3 ? PART_CINDER : PART_EMBER,
                    x + jx / 3, FX(0.7), z + jz / 3, jx / 48,
                    FX(0.022) + (fx)((h >> 24) % 48), jz / 48, 40);
        if (0 == game_frames % 11)
            fx_emit(&effects, PART_WISP, x + jx / 4, FX(1.05), z + jz / 4,
                    FX(0.002), FX(0.009), -FX(0.002), 80);
    }
}

// Dash afterimages: a copy of the player every other frame of a dash.
static void track_afterimages(void)
{
    const Player *p = &run.player;
    for (int i = 0; i < AFTERIMAGES; i++)
        if (afterimages[i].life > 0)
            afterimages[i].life--;
    if (PACT_DASH != p->action || (run.frames & 1))
        return;
    static AnimTrack track;
    SpriteFrame f = player_anim_frame(&track, p, game_frames);
    Afterimage *a = &afterimages[next_afterimage];
    next_afterimage = (next_afterimage + 1) % AFTERIMAGES;
    a->life = AFTERIMAGE_LIFE;
    a->slot = f.slot;
    a->mirror = f.mirror;
    a->x = p->x;
    a->z = p->z;
}

// Banners for chests, cleared rooms and death; the death panel below.
static void handle_events(void)
{
    char msg[32];
    play_event_sounds();
    if (run.events & EV_CHEST_OPEN) {
        snprintf(msg, sizeof(msg), "Treasure! +%d gold", run.reward_amount);
        banner(TXT_GOLD, msg);
    } else if ((run.events & EV_ROOM_CLEAR) && (run.events & EV_GOLD)) {
        snprintf(msg, sizeof(msg), "Room cleared  +%d gold", run.reward_amount);
        banner(TXT_GREEN, msg);
    } else if (run.events & EV_ROOM_CLEAR && RT_REST != run.room.type) {
        banner(TXT_GREEN, "Room cleared");
    }
    if (run.events & EV_PLAYER_DIED) {
        banner(TXT_RED, "YOU DIED");
        // Replace whatever the lower panel showed (controls, boss bar).
        for (int y = 10; y < 23; y++)
            ui_clear_row(y);
        ui_highlight(BOSS_BAR_X, 15, BOSS_BAR_W, false);
        ui_panel(2, 10, 28, 13);
        ui_text_center(13, TXT_RED, "Fell at depth %d", run.depth);
        ui_text_center(15, TXT_DIM, "%s", meta_cause_text(run.hurt_by));
        ui_text_center(18, TXT_NORMAL, "A: continue");
    }
}

// A frame of play: tips, the shrine, hit-stop, the simulation, then the
// screens, sounds and effects for whatever happened, and leaving the room.
static void update_play(void)
{
    if (!room_hint_checked) {
        room_hint_checked = true;
        if (open_hint(hint_for_room(&run, &game.meta), false))
            return;
    }
    PlayerInput in = { .move = DIR_NONE };
    if (ROOM_DEAD != run.status)
        in = read_player_input();
    // The camp's shrine: A there opens the upgrades instead of swinging.
    if (run.room.hub) {
        static bool shown_near;
        bool near = run_at_shrine(&run);
        if (near != shown_near) {
            shown_near = near;
            draw_shrine_prompt(near);
        }
        if (near && attack_pressed()) {
            audio_sfx(SND_CHIME);
            shown_near = false;
            state_set(ST_UPGRADES);
            return;
        }
    }
    // Hit-stop: the whole fight pauses for a few frames on a solid hit.
    if (hitstop > 0) {
        hitstop--;
        return;
    }
#ifdef DELVE_DEBUG
    // Hold L, press R: kill everything. Hold R, press L: leave the room.
    // Hold L, press X: fill the blast meter.
    if ((input.held & KEY_L) && (input.down & KEY_R))
        run_debug_kill_all(&run);
    if ((input.held & KEY_L) && (input.down & KEY_X)) {
        player_add_meter(&run.player, run.player.meter_max);
        in.blast = false;
    }
    if ((input.held & KEY_R) && (input.down & KEY_L) && run.room.door_count) {
        run.player.hp = run.player.max_hp;   // so a skip never kills
        run.exit_door = run.frames % run.room.door_count;
        begin_fade_out();
        return;
    }
#endif
    if (ROOM_DEAD != run.status)
        check_empty_actions(&in);
    run_update(&run, &in);
    room_fade_update(&run.room, fade, run.player.x, run.player.z,
                     SPRITE_HALF_W, SPRITE_HEIGHT);
    handle_events();
    spawn_effects();
    ambient_particles();
    track_afterimages();
    fx_update(&effects);
    if (shake > 0)
        shake--;
    if (ring_timer > 0)
        ring_timer--;

    const Player *p = &run.player;
    if (flash_timer > 0)
        flash_timer--;
    if (1 == p->hp && ROOM_DEAD != run.status && !run.room.hub
        && 0 == game_frames % LOW_HP_PERIOD)
        audio_sfx(SND_HEARTBEAT);
    if (p->hp != hud_hp || p->dash_charges != hud_dash
        || p->daggers != hud_daggers || p->meter != hud_meter
        || hud_blink_now() != hud_blink)
        draw_hud();
    if (ROOM_DEAD != run.status
        && (run_enemies_alive(&run) != shown_alive || run.gold != shown_gold))
        draw_stats();
    if (run.status != shown_status) {
        shown_status = run.status;
        if (ROOM_CLEAR == run.status) {
            draw_bottom();
            if (!run.boon_pending)
                hint_after_clear();
        }
    }
    if (RT_BOSS == run.room.type && ROOM_FIGHT == run.status) {
        const Enemy *b = find_boss();
        int hp = b && EST_DYING != b->state ? b->hp : 0;
        if (hp != shown_boss_hp)
            draw_boss_bar();
    }
    if (run.events2 & EV2_MENDED) {
        audio_sfx(SND_CHIME);
        draw_hud();
    }
    if (run.events2 & EV2_SECOND_WIND) {
        banner(TXT_GOLD, "Second wind!");
        audio_sfx(SND_CHIME);
        draw_hud();
    }
    // The Magma Lord's floods.
    if (run.events2 & EV2_FLOOD_WARN) {
        banner(TXT_RED, "The lava rises!");
        audio_sfx(SND_ROAR);
    }
    if (run.events2 & EV2_FLOOD) {
        audio_sfx(SND_EXPLODE);
        shake = SHAKE_BLAST;
    }
    if (run.events2 & EV2_BOSS_DOWN) {
        audio_music(MUSIC_NONE);   // the fanfare alone, then the area's track
        audio_sfx(SND_FANFARE);
        // On the first way down, a boss opens a waypoint to the next area.
        if (meta_unlock_waypoint(&game.meta, run.depth)) {
            banner(TXT_GOLD, "Boss slain! A waypoint opens");
            save_write(&game);
        } else {
            banner(TXT_GOLD, "The boss falls!");
        }
        shake = SHAKE_BLAST + 4;
        hitstop = 10;
        fx_burst(&effects, PART_GOLD, 20, run.player.x, FX(0.6), run.player.z,
                 FX(0.1));
    }
    if (run.events2 & EV2_BOON_READY)
        open_boon_picker();

    if (run.events & EV_DOOR_ENTER) {
        // Code Genie's start areas skip the waypoint choice.
        int forced = cheat_start_area(run.cheats);
        if (run.room.hub && !forced && game.meta.waypoints) {
            open_waypoints();
            return;
        }
        if (run.room.hub)
            start_area = forced;
        begin_fade_out();
    }

    // Closing the lid pauses too: libnds puts the console to sleep after
    // the lid has been shut for 20 frames (inputSetLidSleepDuration), and
    // on waking the game waits in the pause menu instead of mid-fight.
    if (ROOM_DEAD == run.status && run.frames > 60
        && (input.down & (KEY_A | KEY_START)))
        finish_run(false);
    else if (ROOM_DEAD != run.status
             && ((input.down & KEY_START) || (input.held & KEY_LID)))
        open_pause();
}

// One frame of the current mode; the play itself, choices, fades, pause
// and dialogs.
static void update_mode(void)
{
    switch (mode) {
    case PM_PLAY:
        update_play();
        break;
    case PM_BOON: {
        // The room keeps going (coins fly in) while the player chooses.
        PlayerInput still = { .move = DIR_NONE };
        run_update(&run, &still);
        fx_update(&effects);
        spawn_effects();
        update_boon_picker();
        break;
    }
    case PM_FADE_OUT: {
        // From the camp the hero walks down the crypt's stairs first; the
        // fade comes at the end.
        int total = run.room.hub ? DESCEND_FRAMES : FADE_FRAMES;
        int dark = imax(++mode_timer - (total - FADE_FRAMES), 0);
        setBrightness(3, -16 * dark / FADE_FRAMES);
        if (mode_timer >= total) {
            // The camp's crypt starts a run; other doors lead on.
            if (run.room.hub)
                start_run();
            else
                run_enter_door(&run, run.exit_door);
            enter_current_room();
            mode = PM_FADE_IN;
            mode_timer = FADE_FRAMES;
        }
        break;
    }
    case PM_FADE_IN:
        setBrightness(3, -16 * --mode_timer / FADE_FRAMES);
        if (mode_timer <= 0)
            mode = PM_PLAY;
        break;
    case PM_PAUSED:
        if (input.down & (KEY_START | KEY_B)) {
            resume();
            break;
        }
        if (UI_ACTIVATE == ui_menu_update(&pause_menu)) {
            switch (pause_menu.cursor) {
            case PAUSE_RESUME:
                resume();
                break;
            case PAUSE_OPTIONS:
                state_set(ST_OPTIONS);
                break;
            case PAUSE_ABANDON:
                if (run.room.hub) {   // "Main menu" in the camp
                    state_set(ST_MENU);
                    break;
                }
                mode = PM_CONFIRM_ABANDON;
                draw_confirm_abandon();
                break;
            }
        }
        return; // frozen: no camera or parallax movement
    case PM_HINT:
        update_hint();
        return;   // frozen, like the pause
    case PM_WAYPOINT:
        update_waypoints();
        return;
    case PM_CONFIRM_ABANDON:
        if ((input.down & KEY_A) || input_touched_rect(3 * 8, 15 * 8, 13 * 8, 3 * 8))
            finish_run(true);
        else if ((input.down & KEY_B) || input_touched_rect(16 * 8, 15 * 8, 13 * 8, 3 * 8)) {
            mode = PM_PAUSED;
            draw_pause();
        }
        return;
    }

    update_camera();

    if (banner_timer > 0 && 0 == --banner_timer)
        clear_banner();
}

static void play_update(void)
{
    Input real = choice_before();
    update_mode();
    choice_after(real);
}

// Draw

static AnimTrack player_track;

static void draw_player(void)
{
    const Player *p = &run.player;
    if (!(PM_FADE_OUT == mode && run.room.hub))
        render_shadow(p->x, p->z, FX_ONE);

    // Blink while invulnerable from a hit (not from a dash), after the
    // hurt pose.
    bool hurt_blink = p->hurt_frames > 0 && PACT_DASH != p->action
                   && PACT_BLAST != p->action
                   && p->hurt_frames < PLAYER_IFRAMES_HURT - PLAYER_HURT_POSE
                   && (p->hurt_frames & 4);
    SpriteFrame f = player_anim_frame(&player_track, p, game_frames);
    if (PM_FADE_OUT == mode && run.room.hub) {
        // Down the crypt's stairs: walking on, away from the camera, and
        // sinking.
        int row;
        bool mirror;
        anim_dir_row(DIR_NE, &row, &mirror);   // up the screen, to -z
        int slot = row * PF_FRAMES_PER_DIR + PF_WALK
                 + (mode_timer / 6) % PF_WALK_COUNT;
        fx t = FX_ONE * mode_timer / DESCEND_FRAMES;
        render_char_scaled(SHEET_PLAYER, slot, mirror, PF_FEET_ROW, p->x,
                           -fx_mul(FX(0.8), t), p->z - fx_mul(FX(1.0), t),
                           SPAL_NORMAL, 1);
        return;
    }
    if (p->hp <= 0 || !hurt_blink)
        render_char(SHEET_PLAYER, f.slot, f.mirror, PF_FEET_ROW, p->x, p->z,
                    SPAL_NORMAL);
}

// Sheet, feet row and recolour for each enemy type (the idol, bats and
// crystals are drawn from other sheets in draw_enemy). The Miniworld
// monsters have their own cell size and drawing scale (0 = a 24x24 Puny
// cell).
static const struct {
    SpriteSheet sheet;
    int feet_row;
    SpritePal pal;
    int cell;
    fx scale;
} enemy_sheets[ENEMY_TYPE_COUNT] = {
    [ENEMY_CHASER]  = { SHEET_CHASER, CF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_SHOOTER] = { SHEET_SHOOTER, SF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_BRUTE]   = { SHEET_BRUTE, BF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_ARCHER]  = { SHEET_ARCHER, AF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_CASTER]  = { SHEET_CASTER, KF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_SHAMAN]  = { SHEET_SHAMAN, HF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_TURRET]  = { SHEET_PROPS, 16, SPAL_NORMAL },
    [ENEMY_SLIME]   = { SHEET_SLIME, MF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_DUMMY]   = { SHEET_DUMMY, DF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_WARLORD] = { SHEET_BRUTE, BF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_SLIME_KING] = { SHEET_SLIME, MF_FEET_ROW, SPAL_NORMAL },
    [ENEMY_BAT]     = { SHEET_CAVE, 16, SPAL_NORMAL },
    [ENEMY_DIGGER]  = { SHEET_CRAB, RF_FEET_ROW, SPAL_NORMAL, RF_CELL, FX(1.25) },
    [ENEMY_CRYSTAL] = { SHEET_CAVE, 16, SPAL_NORMAL },
    [ENEMY_GOLEM]   = { SHEET_YETI, YF_FEET_ROW, SPAL_NORMAL, YF_CELL, FX(2.0) },
    [ENEMY_FIRE_SLIME]  = { SHEET_SLIME, MF_FEET_ROW, SPAL_MAGMA },
    [ENEMY_MAGMA_BRUTE] = { SHEET_BULL, UF_FEET_ROW, SPAL_NORMAL, UF_CELL, FX(1.5) },
    [ENEMY_IMP]         = { SHEET_IMP, IF_FEET_ROW, SPAL_NORMAL, IF_CELL, FX(1.125) },
    [ENEMY_MAGMA_LORD]  = { SHEET_DRAGON, GF_FEET_ROW, SPAL_NORMAL, GF_CELL, FX(2.0) },
};

static const uint8_t bat_frames[CAVE_BAT_FRAMES][2] = {
    { CAVE_BAT_U, CAVE_BAT_V }, { CAVE_BAT_1_U, CAVE_BAT_1_V },
};
static const uint8_t gem_frames[CAVE_GEM_FRAMES][2] = {
    { CAVE_GEM_U, CAVE_GEM_V }, { CAVE_GEM_1_U, CAVE_GEM_1_V },
    { CAVE_GEM_2_U, CAVE_GEM_2_V }, { CAVE_GEM_3_U, CAVE_GEM_3_V },
};

// Bats, crystals and buried diggers: not character sheets. Returns false
// for everything else.
static bool draw_cave_enemy(const Enemy *e, bool flash)
{
    SpritePal pal = flash ? SPAL_FLASH : SPAL_NORMAL;
    static const int8_t bob[8] = { 0, 1, 2, 2, 1, 0, -1, -1 };
    switch (e->type) {
    case ENEMY_BAT: {
        if (EST_DYING == e->state && (e->anim & 2))
            return true;
        // Aloft, wings beating; diving it swoops lower.
        render_shadow(e->x, e->z, FX(0.5));
        int f = (e->anim / (EST_WINDUP == e->state ? 2 : 5)) % CAVE_BAT_FRAMES;
        fx y = (EST_ATTACK == e->state ? FX(0.35) : FX(0.7))
             + bob[(e->anim / 4) & 7] * FX(0.03);
        bool mirror = run.player.x - e->x > run.player.z - e->z;
        render_billboard(SHEET_CAVE, bat_frames[f][0], bat_frames[f][1], 16,
                         16, mirror, e->x, y, e->z, pal);
        return true;
    }
    case ENEMY_CRYSTAL: {
        if (EST_DYING == e->state && (e->anim & 2))
            return true;
        render_shadow(e->x, e->z, FX(0.6));
        // Spins faster as it charges a volley.
        int f = (e->anim / (EST_WINDUP == e->state ? 3 : 8)) % CAVE_GEM_FRAMES;
        render_billboard(SHEET_CAVE, gem_frames[f][0], gem_frames[f][1], 16,
                         16, false, e->x,
                         FX(0.25) + bob[(e->anim / 6) & 7] * FX(0.03), e->z,
                         pal);
        return true;
    }
    case ENEMY_DIGGER:
        if (EST_MOVE == e->state) {
            // Underground: a trail of loose earth.
            if (0 == (game_frames & 7))
                fx_emit(&effects, PART_DUST, e->x, FX(0.05), e->z, 0,
                        FX(0.01), 0, 20);
            render_prop(SHEET_CLUTTER, CLUT_RUBBLE, false, e->x, -FX(0.35),
                        e->z, SPAL_NORMAL);
            return true;
        }
        if (EST_WINDUP == e->state) {
            // Breaking out: rising from the floor (the floor hides the
            // rest), with a warning ring round it.
            int total = enemy_defs[e->type].windup;
            fx lift = -FX(1.2) * e->timer / total;
            render_ground(SHEET_FX, FX_RING_U, FX_RING_V, FX_RING_SIZE,
                          FX_RING_SIZE, e->x, e->z, FX(1.1),
                          (game_frames & 4) ? 22 : 12, 56, SPAL_ELITE);
            Dir8 facing = anim_facing_towards(run.player.x - e->x,
                                              run.player.z - e->z);
            SpriteFrame f = enemy_anim_frame(e, facing);
            render_monster(SHEET_CRAB, RF_CELL, f.slot, f.mirror, RF_FEET_ROW,
                           e->x, lift, e->z,
                           e->elite ? SPAL_ELITE : SPAL_NORMAL,
                           enemy_sheets[ENEMY_DIGGER].scale);
            return true;
        }
        return false;
    default:
        return false;
    }
}

// Bosses that leap (the Slime King, the Yeti) are in the air while
// attacking.
static bool leaping(const Enemy *e)
{
    return (ENEMY_SLIME_KING == e->type || ENEMY_GOLEM == e->type)
        && EST_ATTACK == e->state;
}

// An enemy's frame from its own sheet, lifted off the floor by lift; Puny
// sheets are drawn at char_scale times (bosses 2).
static void draw_enemy_frame(const Enemy *e, SpriteFrame f, fx lift,
                             SpritePal pal, int char_scale)
{
    SpriteSheet sheet = enemy_sheets[e->type].sheet;
    int feet = enemy_sheets[e->type].feet_row;
    if (enemy_sheets[e->type].cell)
        render_monster(sheet, enemy_sheets[e->type].cell, f.slot, f.mirror,
                       feet, e->x, lift, e->z, pal,
                       enemy_sheets[e->type].scale);
    else if (char_scale > 1)
        render_char_scaled(sheet, f.slot, f.mirror, feet, e->x, lift, e->z, pal,
                           char_scale);
    else
        render_char(sheet, f.slot, f.mirror, feet, e->x, e->z, pal);
}

// An enemy's sprite for its state: flicker while spawning, flash on hits
// and telegraphs, bosses doubled, leaping bosses in the air.
static void draw_enemy(const Enemy *e)
{
    // Spawning enemies flicker in.
    if (EST_SPAWN == e->state && (e->anim & 2))
        return;

    Dir8 facing = EST_WINDUP == e->state || EST_ATTACK == e->state
                ? anim_facing_towards(e->aim_x, e->aim_z)
                : anim_facing_towards(run.player.x - e->x, run.player.z - e->z);
    bool flash = e->flash > 0 || (EST_WINDUP == e->state && (e->anim & 4));
    if (draw_cave_enemy(e, flash))
        return;
    SpritePal pal = flash ? SPAL_FLASH : e->elite ? SPAL_ELITE
                  : enemy_sheets[e->type].pal;

    if (EST_DYING != e->state)
        render_shadow(e->x, e->z, ENEMY_SLIME == e->type ? FX(0.6)
                      : ENEMY_BRUTE == e->type ? FX(1.2) : FX_ONE);
    if (ENEMY_TURRET == e->type) {
        // The idol: a stone statue that flashes as it charges up.
        if (EST_DYING == e->state && (e->anim & 2))
            return;
        render_prop(SHEET_PROPS, PROP_IDOL, false, e->x, 0, e->z,
                    flash ? SPAL_FLASH : SPAL_NORMAL);
        return;
    }
    SpriteFrame f = enemy_anim_frame(e, facing);
    if (enemy_is_boss(e->type)) {
        // Bosses are drawn twice the size. The Warlord wears the brute's red;
        // the King arcs through the air when he leaps, and a red ring
        // marks where he will land.
        fx lift = 0;
        if (leaping(e)) {
            int total = enemy_defs[e->type].attack;
            int t = total - e->timer;
            fx peak = ENEMY_GOLEM == e->type ? FX(1.6) : FX(3.0);
            lift = peak * t * (total - t) / (total * total);
        }
        if (ENEMY_WARLORD == e->type && SPAL_NORMAL == pal)
            pal = SPAL_ELITE;
        draw_enemy_frame(e, f, lift, pal, 2);
        return;
    }
    draw_enemy_frame(e, f, 0, pal, 1);
}

// The Slime King's landing spot, while he is in the air.
static void draw_leap_marks(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &run.enemies[i];
        if (e->active && leaping(e))   // (id 58)
            render_ground(SHEET_FX, FX_RING_U, FX_RING_V, FX_RING_SIZE,
                          FX_RING_SIZE, e->tx, e->tz, FX(1.2),
                          (game_frames & 4) ? 24 : 12, 58, SPAL_ELITE);
    }
}

static void draw_afterimages(void)
{
    for (int i = 0; i < AFTERIMAGES; i++) {
        const Afterimage *a = &afterimages[i];
        // The newest copies still overlap the player: skip them.
        if (a->life <= 0 || a->life > AFTERIMAGE_LIFE - 3)
            continue;
        render_char_alpha(SHEET_PLAYER, a->slot, a->mirror, PF_FEET_ROW, a->x,
                          a->z, 2 + a->life * 12 / AFTERIMAGE_LIFE, 40 + i);
    }
}

static void draw_pickups(void)
{
    for (int i = 0; i < MAX_PICKUPS; i++) {
        const Pickup *k = &run.pickups[i];
        if (!k->active)
            continue;
        render_shadow(k->x, k->z, FX(0.4));
        if (PICK_COIN == k->kind) {
            int f = ((int)game_frames / 5 + i) % FX_COIN_FRAMES;
            render_billboard(SHEET_FX, FX_COIN_U + f * 8, FX_COIN_V, 8, 8,
                             false, k->x, k->y + FX(0.1), k->z, SPAL_NORMAL);
        } else {
            // Hearts bob gently once they have landed.
            fx bob = k->y ? 0 : ((game_frames / 8) & 1) * FX(0.04);
            render_billboard(SHEET_FX, FX_HEART_U, FX_HEART_V, 8, 8, false,
                             k->x, k->y + FX(0.1) + bob, k->z, SPAL_NORMAL);
        }
    }
}

// Flame Dash trail: small flickering fires.
static void draw_flames(void)
{
    for (int i = 0; i < MAX_FLAMES; i++) {
        const Flame *f = &run.flames[i];
        if (!f->active)
            continue;
        int slot = FIRE_FLAME + ((int)game_frames / 3 + i) % FIRE_FLAME_FRAMES;
        render_prop(SHEET_FIRE, slot, 0 != (i & 1), f->x, -FX(0.1), f->z,
                    SPAL_NORMAL);
        if ((game_frames & 7) == (uint32_t)(i & 7))
            fx_emit(&effects, (i & 2) ? PART_CINDER : PART_EMBER, f->x,
                    FX(0.5), f->z, 0, FX(0.02), 0, 26);
        if ((game_frames & 31) == (uint32_t)(i & 31))
            fx_emit(&effects, PART_WISP, f->x, FX(0.8), f->z, 0, FX(0.007),
                    0, 44);
    }
}

static void draw_daggers(void)
{
    for (int i = 0; i < MAX_DAGGERS; i++) {
        const Dagger *d = &run.daggers[i];
        if (!d->active)
            continue;
        int frame = ((int)game_frames / 2 + i) % FX_DAGGER_FRAMES;
        render_billboard(SHEET_FX, frame * 16, FX_DAGGER_V, 16, 16, false,
                         d->x, FX(0.35), d->z, SPAL_NORMAL);
    }
}

// Caster marks: a red ring on the floor that pulses faster as it nears
// bursting.
static void draw_zones(void)
{
    for (int i = 0; i < MAX_ZONES; i++) {
        const Zone *zn = &run.zones[i];
        if (!zn->active)
            continue;
        int rate = zn->timer > ZONE_TELEGRAPH / 2 ? 8 : 4;
        int alpha = ((int)game_frames / rate) & 1 ? 26 : 14;
        // Translucent ids: afterimages 40-43, zones 44-55, the digger's
        // ring 56, the blast ring 57, landing marks 58.
        render_ground(SHEET_FX, FX_RING_U, FX_RING_V, FX_RING_SIZE,
                      FX_RING_SIZE, zn->x, zn->z, zn->radius, alpha, 44 + i,
                      SPAL_ELITE);
    }
}

static void draw_blast_ring(void)
{
    if (ring_timer <= 0)
        return;
    int t = RING_FRAMES - ring_timer;
    fx half = ring_radius * (t + 2) / (RING_FRAMES + 2);
    render_ground(SHEET_FX, FX_RING_U, FX_RING_V, FX_RING_SIZE, FX_RING_SIZE,
                  ring_x, ring_z, half, 6 + ring_timer * 25 / RING_FRAMES, 57,
                  SPAL_NORMAL);
}

// The 3D frame: the void, the room, props, characters, shots, effects,
// then everything translucent.
static void play_draw(void)
{
    bool open = ROOM_CLEAR == run.status;
    // Screen shake: a small, decaying jitter of the camera.
    Camera view = cam;
    if (shake > 0 && game.options.screen_shake) {
        static const int8_t jitter[8] = { 3, -2, 2, -3, 1, -1, 2, -2 };
        fx amp = shake * FX(0.012);
        view.x += amp * jitter[game_frames & 7];
        view.z += amp * jitter[(game_frames + 3) & 7];
    }
#ifdef DELVE_DEBUG
    // Section timings (scanlines), shown by the debug meter's top row.
    int t0 = REG_VCOUNT;
#define SECTION(i) do { int t = REG_VCOUNT; \
        debug_draw_sections[i] = (t + 263 - t0) % 263; t0 = t; } while (0)
#else
#define SECTION(i) do { } while (0)
#endif
    render_begin(&view);
    if (!run.room.hub)   // outdoors: the sky, no floating rubble
        render_void(&view, (int)game_frames);
    SECTION(0);
    PropState ps = {
        .broken = run.decor_broken,
        .fuse = run.fuse,
        .crate_broken = run.crate_broken,
        .room_frame = run.frames,
        .crust = run.crust,
        .flood_ring = run.flood_ring,
    };
    render_room(&run.room, fade, run.player.x, run.player.z, open,
                (int)game_frames, &ps);
    SECTION(1);
    render_room_props(&run.room, open, run.chest_opened, &ps,
                      (int)game_frames);
    if (run.room.hub) {
        // The upgrade shrine: an idol that glitters.
        render_shadow(HUB_SHRINE_X, HUB_SHRINE_Z, FX(1.1));
        render_prop(SHEET_PROPS, PROP_IDOL, false, HUB_SHRINE_X, 0,
                    HUB_SHRINE_Z, SPAL_NORMAL);
        if (0 == (game_frames % 12))
            fx_emit(&effects, PART_GOLD,
                    HUB_SHRINE_X + (fx)(game_frames * 997 % 4096) - FX_HALF,
                    FX(0.4), HUB_SHRINE_Z, 0, FX(0.015), 0, 40);
    }
    draw_pickups();
    SECTION(2);
    draw_player();
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (run.enemies[i].active)
            draw_enemy(&run.enemies[i]);
    int fire = ((int)game_frames / 4) & 1;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        const Projectile *s = &run.shots[i];
        if (!s->active)
            continue;
        if (PROJ_SHARD == s->kind) {
            render_billboard(SHEET_CAVE, CAVE_SHARD_U, CAVE_SHARD_V, 16, 16,
                             0 != fire, s->x, PROJECTILE_HEIGHT - FX(0.3), s->z,
                             SPAL_NORMAL);
        } else if (PROJ_ARROW == s->kind) {
            // The dagger frames are an arrow turned 45 degrees at a time,
            // starting from pointing down (DIR_S).
            int frame = (dir8_towards(s->vx, s->vz) - DIR_S + DIR_COUNT)
                      % DIR_COUNT;
            render_billboard(SHEET_FX, frame * 16, FX_DAGGER_V, 16, 16, false,
                             s->x, PROJECTILE_HEIGHT - FX(0.3), s->z,
                             SPAL_NORMAL);
        } else {
            render_billboard(SHEET_FX, FX_FIREBALL_U + fire * 8, FX_FIREBALL_V,
                             8, 8, false, s->x, PROJECTILE_HEIGHT, s->z,
                             SPAL_NORMAL);
        }
    }
    draw_daggers();
    draw_flames();
    SECTION(3);
    render_fx(&effects);
    SECTION(4);

    // Translucent things last: afterimages, the blast ring, faded walls.
    draw_afterimages();
    draw_zones();
    draw_leap_marks();
    draw_blast_ring();
    render_room_faded(&run.room, fade);
    SECTION(5);
#undef SECTION
}

const State state_play = {
    .name = "play",
    .enter = play_enter,
    .update = play_update,
    .draw = play_draw,
    .exit = play_exit,
};
