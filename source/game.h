// Persistent game data: options, meta-progress and run history, saved to
// the SD card by save.c.

#ifndef DELVE_GAME_H
#define DELVE_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "rogue.h"

#define VOLUME_MAX 10

typedef enum {
    ZOOM_CLOSE = 0,
    ZOOM_FAR = 1,
} ZoomLevel;

// All fields are bytes (saved one tag each, compared with memcmp).
typedef struct {
    uint8_t music_volume;  // 0..VOLUME_MAX
    uint8_t sfx_volume;    // 0..VOLUME_MAX
    uint8_t default_zoom;  // ZoomLevel
    uint8_t auto_attack;   // holding the sword button keeps swinging
    uint8_t left_handed;   // ABXY move, the D-pad attacks (mirrored)
    uint8_t screen_shake;
    uint8_t hit_numbers;   // damage numbers over enemies
    uint8_t hints;         // first-time tips (hint.h)
} Options;

typedef struct {
    uint32_t total_runs;
    uint32_t best_depth;
    uint32_t currency;
    uint32_t upgrades;     // bitfield of unlocked upgrades
    uint32_t total_kills;
    uint32_t bosses_slain;
    uint32_t hints_seen;   // HintId bits (hint.h): tips already shown
    uint32_t waypoints;    // bit a: the crypt can start a run in area a
    uint32_t play_seconds; // time in runs, all runs together
    uint32_t last_played;  // save order across slots (save.h)
    uint32_t cheats_used;  // Code Genie bits of every cheat a run here has
                           // used (cheat.h); never cleared
} MetaProgress;

// Boon stacks kept per run (BOON_COUNT fits; checked in boon.c). Saved as
// 4-bit counts.
#define RUN_BOON_SLOTS 16

// Outcome of a run: the summary screen and the run history. Saved in the
// history with each number clamped to 16 bits.
typedef struct {
    int depth;
    int start_depth;       // 1, or a waypoint's first depth
    int kills;
    int gold;              // found during the run
    int rooms_cleared;
    int earned;            // meta-currency awarded
    int bosses;            // bosses beaten
    int seconds;           // time played
    uint8_t cause;         // CAUSE_* (run.h): what ended it
    bool new_best;
    bool abandoned;
    bool cheated;          // played with Code Genie cheats
    uint32_t seed;         // the run's seed, to reproduce it
    uint8_t boons[RUN_BOON_SLOTS];   // stacks taken, by BoonId
} RunResult;

// The last few runs, newest first.
#define RUN_HISTORY 8
typedef struct {
    uint8_t count;
    RunResult runs[RUN_HISTORY];
} RunHistory;

typedef struct {
    Options options;
    MetaProgress meta;
    RogueBoard rogue_board; // Easter egg: Top Rogueists
    RunHistory history;
    RunResult last_run;    // = history.runs[0] once recorded; not saved
    bool storage_ok;       // false when the SD card could not be used
} GameData;

extern GameData game;

// Frames since boot; mixed into run seeds.
extern uint32_t game_frames;

// A fresh save: everything zero, default options.
void game_set_defaults(GameData *g);

#endif // DELVE_GAME_H
