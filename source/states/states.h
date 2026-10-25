// State definitions and helpers shared by the menu-side states.

#ifndef DELVE_STATES_H
#define DELVE_STATES_H

#include <stdbool.h>
#include <stddef.h>

#include "../game.h"
#include "../state.h"

extern const State state_title;
extern const State state_menu;
extern const State state_options;
extern const State state_upgrades;
extern const State state_credits;
extern const State state_about;
extern const State state_play;
extern const State state_summary;
extern const State state_rogue;
extern const State state_history;
extern const State state_slots;
extern const State state_egg;

// Menu backdrop: drifting parallax and an optional logo; the title's
// diorama is drawn by the renderer.
void scene_menu_top(bool logo);
void scene_menu_update(void);
void scene_draw_diorama(void);   // the title screen's 3D room
void scene_draw_stats(int y, const MetaProgress *m);
// Total play time, "42m" or "3h05m".
void scene_format_time(char *out, size_t size, uint32_t seconds);
void scene_draw_run(const RunResult *r, const char *title);
// The Code Genie cheats on now, two columns from top-screen text row y.
// Returns how many there are.
int scene_draw_cheats(int y);

// Set by the summary's "Descend again": the play state starts a new run
// at once instead of entering the camp.
extern bool play_descend_now;

// The console owner's name from the firmware (ASCII only, at most 10
// characters), or fallback when none is set.
void scene_user_name(char *out, size_t size, const char *fallback);

#ifdef DELVE_DEBUG
// Debug builds: scanlines spent in each part of the play state's draw last
// frame (void, room, props, characters, effects, alpha), for the CPU meter.
extern int debug_draw_sections[6];
#endif

#endif // DELVE_STATES_H
