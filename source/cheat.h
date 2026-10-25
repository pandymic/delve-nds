// Code Genie: cheat codes, entered on the Easter egg's version splash
// (states/egg.c). Pure logic.
//
// A code is 8 hex digits, shown "XXXX-XXXX". Entering one toggles its cheat
// on or off; several can be on at once, except the three start areas, which
// replace each other. Cheats last until power-off and are never saved, like
// a Game Genie's. A run started with any on marks its save slot for good
// (MetaProgress.cheats_used) and its line in the run history.

#ifndef DELVE_CHEAT_H
#define DELVE_CHEAT_H

#include <stdbool.h>
#include <stdint.h>

// Never renumber: saves keep the bits of the cheats a slot has used.
typedef enum {
    CHEAT_GOD,        // no damage, pits and lava included
    CHEAT_ONE_HIT,    // the player's hits kill (bosses too) and break props
    CHEAT_UPGRADES,   // every upgrade counts as owned (purchases untouched)
    CHEAT_SEWERS,     // the crypt starts the run in the Sunken Sewers
    CHEAT_CAVERNS,    // ...the Crystal Caverns
    CHEAT_DEPTHS,     // ...the Molten Depths
    CHEAT_BLAST,      // the blast meter stays full
    CHEAT_STAMINA,    // dash charges never run out
    CHEAT_DAGGERS,    // daggers never run out
    CHEAT_WIPE,       // the blast kills everything in the room
    CHEAT_GOLD,       // gold found x5
    CHEAT_GIANT,      // characters drawn at double size
    CHEAT_HARD,       // damage taken doubled (pits excepted)
    CHEAT_COUNT
} CheatId;

#define CHEAT_BIT(id)    (1u << (id))
#define CHEAT_AREAS      (CHEAT_BIT(CHEAT_SEWERS) | CHEAT_BIT(CHEAT_CAVERNS) \
                          | CHEAT_BIT(CHEAT_DEPTHS))
#define CHEAT_GOLD_MULT  5
#define CHEAT_GIANT_PCT  200

#define CODE_DIGITS      8
// Not a cheat: shows the bug-report screen, and marks nothing.
#define CODE_BUG_REPORT  0x0BADC0DEu

typedef struct {
    uint32_t code;
    const char *name;     // at most 15 characters (About's two columns)
} CheatDef;

extern const CheatDef cheat_defs[CHEAT_COUNT];

// The cheats on now. Starts at 0 on boot.
extern uint32_t cheat_active;

typedef enum {
    CODE_UNKNOWN,
    CODE_ON,          // *id turned on (and any other start area off)
    CODE_OFF,         // *id turned off
    CODE_REPORT,      // CODE_BUG_REPORT
} CodeResult;

// Applies an entered code to *active.
CodeResult cheat_enter(uint32_t *active, uint32_t code, CheatId *id);

static inline bool cheat_on(uint32_t active, CheatId id)
{
    return 0 != (active & CHEAT_BIT(id));
}

// The area a start-area cheat sends runs to (1-3), or 0 for none.
int cheat_start_area(uint32_t active);

// Upgrade bits a run gets: all of them with CHEAT_UPGRADES, else owned.
uint32_t cheat_upgrades(uint32_t active, uint32_t owned);

// The names of the cheats in mask, comma separated, cut to fit size.
void cheat_list(uint32_t mask, char *out, int size);

#endif // DELVE_CHEAT_H
