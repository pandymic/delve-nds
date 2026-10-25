// First-time tips: which one a moment calls for, and their text. Pure
// logic; the play state shows them (and saves MetaProgress.hints_seen).

#ifndef DELVE_HINT_H
#define DELVE_HINT_H

#include <stdbool.h>

#include "game.h"

struct Run;

// Bits of MetaProgress.hints_seen; never reorder (saves store them).
typedef enum {
    HINT_CAMP,      // first time in the camp
    HINT_FIGHT,     // first combat room: clear it, the controls
    HINT_DOORS,     // first doors opening: rewards
    HINT_BOONS,     // first boon offer
    HINT_PITS,      // first room with pits
    HINT_TRAPS,     // first room with traps
    HINT_BOSS,      // first boss arena
    HINT_SHRINE,    // back in camp with gold for an upgrade
    HINT_WAYPOINT,  // back in camp with a waypoint open
    HINT_COUNT,
    HINT_NONE = -1,
} HintId;

#define HINT_LINES     5
#define HINT_LINE_MAX  24   // characters per line (the tip panel)

// Whether a tip has been shown (and dismissed) on this save.
bool hint_seen(const MetaProgress *m, HintId id);
void hint_mark_seen(MetaProgress *m, HintId id);

// The tip for entering the run's current room, or HINT_NONE: the camp, the
// shrine once gold allows an upgrade, a boss, the first fight, pits, traps.
// Ignores the Hints option (the caller checks it).
HintId hint_for_room(const struct Run *run, const MetaProgress *m);

// The tip's title and its lines (NULL past the last). Lines that name a
// button follow the left-handed option.
const char *hint_title(HintId id);
const char *hint_line(HintId id, int line, bool left_handed);

#endif // DELVE_HINT_H
