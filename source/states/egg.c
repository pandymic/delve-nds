// The Easter egg's version splash, reached by tapping the version on
// the About screen: the version in big logo digits up top, and two buttons
// below, Play Rogue (states/rogue.c) and Code Genie: cheat codes on a hex
// keypad (cheat.h). One code is not a cheat: CODE_BUG_REPORT shows the
// bug-report screen until the next button press, and marks nothing.

#include <stdio.h>
#include <string.h>

#include <NEMain.h>
#include <nds.h>

#include "../audio.h"
#include "../cheat.h"
#include "../config.h"
#include "../game.h"
#include "../input.h"
#include "../save.h"
#include "../ui.h"
#include "../video.h"
#include "states.h"

typedef enum {
    EGG_SPLASH,
    EGG_GENIE,
    EGG_REPORT,     // the bug report, until the next button press
} EggMode;

static EggMode mode;

enum { ITEM_ROGUE, ITEM_GENIE };

static UiMenu menu = {
    .items = { "Play Rogue", "Code Genie" },
    .count = 2,
    .x = 10, .y = 9, .w = 14, .step = 4,
    .has_back = true,
};

// The bug report (top screen): build, libraries, device and save.

// Firmware header offset of the wifi MAC address (GBATEK, "DS Firmware
// Header"). Same value Wifi_GetData(WIFIGETDATA_MACADDRESS) reports, without
// bringing up the wifi stack.
#define FIRMWARE_MAC_OFFSET 0x36

static const char *const languages[8] = {
    "Japanese", "English", "French", "German",
    "Italian", "Spanish", "Chinese", "Korean",
};

static void row(int y, const char *label, TextColor c, const char *value)
{
    video_top_text(1, y, TXT_DIM, "%s", label);
    video_top_text(12, y, c, "%.19s", value);
}

static void draw_uptime(void)
{
    unsigned s = game_frames / 60;
    video_top_text(1, 23, TXT_DIM, "Uptime %02u:%02u:%02u", s / 3600,
                   (s / 60) % 60, s % 60);
}

static void draw_report(void)
{
    char buf[40];
    video_top_clear();
    video_top_text_center(0, TXT_GOLD, "- BUG REPORT -");

    row(2, "Version", TXT_NORMAL, DELVE_VERSION_STRING " " DELVE_BUILD_TYPE);
    row(3, "Commit", TXT_NORMAL, DELVE_GIT_HASH);
    row(4, "Built", TXT_NORMAL, __DATE__);

    row(6, "BlocksDS", TXT_NORMAL, DELVE_BLOCKSDS_VERSION);
    snprintf(buf, sizeof(buf), "%d.%d.%d", _LIBNDS_MAJOR_, _LIBNDS_MINOR_,
             _LIBNDS_PATCH_);
    row(7, "libnds", TXT_NORMAL, buf);
    row(8, "NitroEng", TXT_NORMAL, NITRO_ENGINE_VERSION_STRING);
    snprintf(buf, sizeof(buf), "%d.%d.%d", __GNUC__, __GNUC_MINOR__,
             __GNUC_PATCHLEVEL__);
    row(9, "GCC", TXT_NORMAL, buf);

    row(11, "Mode", TXT_NORMAL, isDSiMode() ? "DSi" : "DS");
    u8 mac[6] = { 0 };
    if (0 == readFirmware(FIRMWARE_MAC_OFFSET, mac, sizeof(mac)))
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else
        strcpy(buf, "unavailable");
    row(12, "MAC", TXT_CYAN, buf);
    scene_user_name(buf, sizeof(buf), "(not set)");
    row(13, "User", TXT_NORMAL, buf);
    row(14, "Language", TXT_NORMAL, languages[PersonalData->language & 7]);

    row(16, "Storage", game.storage_ok ? TXT_GREEN : TXT_RED,
        game.storage_ok ? "SD card OK" : "none (not saving)");
    row(17, "Save", TXT_NORMAL, game.storage_ok ? save_path() : "-");
    snprintf(buf, sizeof(buf), "%lu runs, %lu kills",
             (unsigned long)game.meta.total_runs,
             (unsigned long)game.meta.total_kills);
    row(18, "Records", TXT_NORMAL, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)game.meta.best_depth);
    row(19, "Best", TXT_NORMAL, buf);
    snprintf(buf, sizeof(buf), "0x%02lX", (unsigned long)game.meta.upgrades);
    row(20, "Upgrades", TXT_NORMAL, buf);
    snprintf(buf, sizeof(buf), "0x%04lX / 0x%04lX",
             (unsigned long)cheat_active, (unsigned long)game.meta.cheats_used);
    row(21, "Cheats", TXT_NORMAL, buf);
    video_top_text(1, 22, TXT_DIM, "Include this in bug reports.");
    draw_uptime();

    ui_clear();
    ui_panel(4, 7, 24, 9);
    ui_text_center(9, TXT_GOLD, "Bug report");
    ui_text_center(11, TXT_NORMAL, "on the upper screen");
    ui_text_center(13, TXT_DIM, "Any button to close");
}

// Code Genie: a hex keypad, D-pad and A or touch. Eight digits enter the
// code at once.

#define KEY_COLS   4
#define KEY_ROWS   4
#define KEY_X0     6     // first key panel
#define KEY_Y0     5
#define KEY_W      5
#define KEY_H      3
#define EDIT_Y     (KEY_Y0 + KEY_ROWS * KEY_H)   // Del and Clear
#define EDIT_W     10
#define DEL_X      KEY_X0
#define CLEAR_X    (KEY_X0 + KEY_COLS * KEY_W - EDIT_W)
#define CODE_Y     2
#define MSG_X      10    // right of the Back button
#define MSG_Y      22
#define MSG_W      21

#define ACTIVE_Y   11    // the cheats on, up top

enum { CUR_DEL = KEY_COLS * KEY_ROWS, CUR_CLEAR };   // the edit row

static char digits[CODE_DIGITS + 1];
static int count;
static int cursor;

static const char hex[] = "0123456789ABCDEF";

static void message(TextColor c, const char *text)
{
    ui_text(MSG_X, MSG_Y, c, "%-*s", MSG_W, text);
}

static void draw_code(void)
{
    char shown[CODE_DIGITS + 2];
    int k = 0;
    for (int i = 0; i < CODE_DIGITS; i++) {
        if (CODE_DIGITS / 2 == i)
            shown[k++] = '-';
        shown[k++] = i < count ? digits[i] : '_';
    }
    shown[k] = '\0';
    ui_text_center(CODE_Y, TXT_GOLD, "%s", shown);
}

static void key_rect(int i, int *x, int *y, int *w)
{
    if (i < CUR_DEL) {
        *x = KEY_X0 + (i % KEY_COLS) * KEY_W;
        *y = KEY_Y0 + (i / KEY_COLS) * KEY_H;
        *w = KEY_W;
    } else {
        *x = CUR_DEL == i ? DEL_X : CLEAR_X;
        *y = EDIT_Y;
        *w = EDIT_W;
    }
}

static void draw_key(int i)
{
    int x, y, w;
    key_rect(i, &x, &y, &w);
    bool sel = i == cursor;
    ui_highlight(x + 1, y + 1, w - 2, sel);
    TextColor c = sel ? TXT_GOLD : TXT_NORMAL;
    if (i < CUR_DEL)
        ui_text(x + w / 2, y + 1, c, "%c", hex[i]);
    else
        ui_text(x + 2, y + 1, c, "%s", CUR_DEL == i ? "Del" : "Clear");
}

static void draw_active(int y)
{
    if (0 == scene_draw_cheats(y))
        video_top_text_center(y, TXT_DIM, "No cheats on");
}

// The lamp beside "Code Genie" (redrawn when the menu repaints it).
static void draw_menu_lamp(void)
{
    ui_putc(menu.x + menu.w - 3, menu.y + ITEM_GENIE * menu.step, GLYPH_LAMP,
            TXT_CYAN);
}

static void draw_genie_top(void)
{
    scene_menu_top(false);
    video_top_text_center(0, TXT_GOLD, "- CODE GENIE -");
    video_top_putc(7, 0, GLYPH_LAMP, TXT_CYAN);
    video_top_putc(24, 0, GLYPH_LAMP, TXT_CYAN);
    // A blank row between lines: the glyphs fill their whole tile.
    video_top_text_center(2, TXT_NORMAL, "A code is eight hex digits.");
    video_top_text_center(4, TXT_NORMAL, "Enter one again to undo it.");
    video_top_text_center(6, TXT_DIM, "Runs with cheats mark the save");
    video_top_text_center(9, TXT_DIM, "- cheats on -");
    draw_active(ACTIVE_Y);
}

static void draw_genie(void)
{
    ui_clear();
    ui_panel(KEY_X0 - 1, 0, KEY_COLS * KEY_W + 2, 5);
    draw_code();
    for (int i = 0; i < KEY_COLS * KEY_ROWS; i++)
        ui_panel(KEY_X0 + (i % KEY_COLS) * KEY_W, KEY_Y0 + (i / KEY_COLS) * KEY_H,
                 KEY_W, KEY_H);
    ui_panel(DEL_X, EDIT_Y, EDIT_W, KEY_H);
    ui_panel(CLEAR_X, EDIT_Y, EDIT_W, KEY_H);
    for (int i = 0; i <= CUR_CLEAR; i++)
        draw_key(i);
    ui_back_button();
    message(TXT_DIM, "B: delete");
}

static void open_genie(void)
{
    mode = EGG_GENIE;
    count = 0;
    cursor = 0;
    draw_genie_top();
    draw_genie();
}

static uint32_t code_value(void)
{
    uint32_t v = 0;
    for (int i = 0; i < CODE_DIGITS; i++)
        v = (v << 4) | (uint32_t)(strchr(hex, digits[i]) - hex);
    return v;
}

static void enter_code(void)
{
    CheatId id = CHEAT_GOD;
    CodeResult r = cheat_enter(&cheat_active, code_value(), &id);
    count = 0;
    draw_code();
    char msg[32];
    switch (r) {
    case CODE_ON:
        audio_sfx(SND_CHIME);
        snprintf(msg, sizeof(msg), "%s on", cheat_defs[id].name);
        message(TXT_GREEN, msg);
        break;
    case CODE_OFF:
        audio_sfx(SND_BACK);
        snprintf(msg, sizeof(msg), "%s off", cheat_defs[id].name);
        message(TXT_DIM, msg);
        break;
    case CODE_REPORT:
        audio_sfx(SND_SELECT);
        mode = EGG_REPORT;
        draw_report();
        return;
    case CODE_UNKNOWN:
        audio_sfx(SND_DENY);
        message(TXT_RED, "Unknown code");
        return;
    }
    // The list of cheats on, up top.
    for (int y = ACTIVE_Y; y < ACTIVE_Y + (CHEAT_COUNT + 1) / 2; y++)
        video_top_text(0, y, TXT_NORMAL, "%32s", "");
    draw_active(ACTIVE_Y);
}

static void press(int i)
{
    if (CUR_DEL == i || CUR_CLEAR == i) {
        if (0 == count) {
            audio_sfx(SND_DENY);
            return;
        }
        count = CUR_DEL == i ? count - 1 : 0;
        audio_sfx(SND_BACK);
        draw_code();
        return;
    }
    audio_sfx(SND_SELECT);
    digits[count++] = hex[i];
    draw_code();
    if (CODE_DIGITS == count)
        enter_code();
    else if (1 == count)
        message(TXT_DIM, "B: delete");
}

static int key_at(int px, int py)
{
    for (int i = 0; i <= CUR_CLEAR; i++) {
        int x, y, w;
        key_rect(i, &x, &y, &w);
        if (px >= x * 8 && px < (x + w) * 8 && py >= y * 8 && py < (y + KEY_H) * 8)
            return i;
    }
    return -1;
}

static void open_splash(void);

static void update_genie(void)
{
    int old = cursor;
    if (input.repeat & KEY_LEFT) {
        if (cursor < CUR_DEL)
            cursor = cursor / KEY_COLS * KEY_COLS + (cursor + KEY_COLS - 1) % KEY_COLS;
        else
            cursor = CUR_DEL == cursor ? CUR_CLEAR : CUR_DEL;
    } else if (input.repeat & KEY_RIGHT) {
        if (cursor < CUR_DEL)
            cursor = cursor / KEY_COLS * KEY_COLS + (cursor + 1) % KEY_COLS;
        else
            cursor = CUR_DEL == cursor ? CUR_CLEAR : CUR_DEL;
    } else if (input.repeat & KEY_UP) {
        if (cursor >= CUR_DEL)   // up from the edit row: the bottom keys
            cursor = (KEY_ROWS - 1) * KEY_COLS + (CUR_DEL == cursor ? 0 : KEY_COLS - 1);
        else if (cursor < KEY_COLS)
            cursor = cursor < KEY_COLS / 2 ? CUR_DEL : CUR_CLEAR;
        else
            cursor -= KEY_COLS;
    } else if (input.repeat & KEY_DOWN) {
        if (cursor >= CUR_DEL)
            cursor = CUR_DEL == cursor ? 0 : KEY_COLS - 1;
        else if (cursor >= (KEY_ROWS - 1) * KEY_COLS)
            cursor = cursor % KEY_COLS < KEY_COLS / 2 ? CUR_DEL : CUR_CLEAR;
        else
            cursor += KEY_COLS;
    } else if (input.down & KEY_A) {
        press(cursor);
    } else if (input.down & KEY_B) {
        if (count > 0) {
            press(CUR_DEL);
        } else {
            audio_sfx(SND_BACK);
            open_splash();
            return;
        }
    } else if (ui_back_touched()) {
        audio_sfx(SND_BACK);
        open_splash();
        return;
    } else if (input.touch_down) {
        int i = key_at(input.touch_x, input.touch_y);
        if (i >= 0) {
            cursor = i;
            draw_key(old);
            press(i);
            if (EGG_GENIE != mode)
                return;
        }
    }
    if (cursor != old) {
        if (!input.touch_down)
            audio_sfx(SND_SELECT);
        draw_key(old);
        draw_key(cursor);
    }
}

static void update_report(void)
{
    if (0 == state_frames() % 60)
        draw_uptime();
    if (input.down || input.touch_down) {
        audio_sfx(SND_BACK);
        mode = EGG_GENIE;
        draw_genie_top();
        draw_genie();
        message(TXT_DIM, "Bug report closed");
    }
}

// The splash

static void draw_splash(void)
{
    // Major and minor only, as phones show theirs: "2.4".
    char big[16];
    snprintf(big, sizeof(big), "%s", DELVE_VERSION_STRING);
    char *dot = strchr(big, '.');
    if (dot && (dot = strchr(dot + 1, '.')))
        *dot = '\0';
    scene_menu_top(false);
    video_big_text(4, big);
    video_top_text_center(11, TXT_GOLD, "Delve %s", DELVE_VERSION_STRING);
    video_top_text_center(13, TXT_DIM, "built %s", __DATE__);
    int on = 0;
    for (int i = 0; i < CHEAT_COUNT; i++)
        on += cheat_on(cheat_active, (CheatId)i);
    if (on) {
        video_top_putc(8, 17, GLYPH_LAMP, TXT_CYAN);
        video_top_text(10, 17, TXT_CYAN, "%d cheat%s on", on, 1 == on ? "" : "s");
    }

    ui_clear();
    ui_text_center(1, TXT_DIM, "- v%s -", DELVE_VERSION_STRING);
    for (int i = 0; i < menu.count; i++)
        ui_panel(menu.x - 2, menu.y + i * menu.step - 1, menu.w + 2, 3);
    ui_menu_draw(&menu);
    draw_menu_lamp();
    ui_back_button();
}

static void open_splash(void)
{
    mode = EGG_SPLASH;
    draw_splash();
}

static void update_splash(void)
{
    switch (ui_menu_update(&menu)) {
    case UI_ACTIVATE:
        if (ITEM_ROGUE == menu.cursor)
            state_set(ST_ROGUE);
        else
            open_genie();
        break;
    case UI_BACK:
        state_set(ST_ABOUT);
        break;
    case UI_MOVED:
        draw_menu_lamp();
        break;
    default:
        break;
    }
}

static void egg_enter(void)
{
    audio_music(MUSIC_MENU);
    // Back from Rogue: the cursor stays on it.
    if (ST_ROGUE != state_previous())
        menu.cursor = ITEM_ROGUE;
    open_splash();
}

static void egg_update(void)
{
    scene_menu_update();
    switch (mode) {
    case EGG_SPLASH: update_splash(); break;
    case EGG_GENIE:  update_genie(); break;
    case EGG_REPORT: update_report(); break;
    }
}

const State state_egg = {
    .name = "egg",
    .enter = egg_enter,
    .update = egg_update,
};
