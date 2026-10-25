// Meta-progression between runs: currency award, records, upgrades.
// Pure logic.

#ifndef DELVE_META_H
#define DELVE_META_H

#include <stdbool.h>
#include <stdint.h>

#include "game.h"

// Bits of MetaProgress.upgrades; never reorder (saves store them).
typedef enum {
    UPG_VITALITY,     // +1 max heart
    UPG_SWIFT_DASH,   // dash charges recharge faster
    UPG_HEAVY_BLADE,  // +1 attack damage
    UPG_QUIVER,       // +1 dagger charge
    UPG_DEEP_POCKETS, // +25% gold found
    UPG_VITALITY2,    // +1 more max heart (needs Vitality)
    UPG_KINDLING,     // runs start with the blast meter full
    UPG_SECOND_WIND,  // once per run, a killing blow leaves 2 hearts
    UPG_KEEN_EDGE,    // +1 more attack damage (needs Heavy Blade)
    // Two paths of three, each step needing the one before.
    UPG_FLETCHING,    // Hunter: daggers recharge faster
    UPG_BARBED_TIPS,  // daggers pierce one more enemy
    UPG_HUNTERS_MARK, // +1 dagger damage
    UPG_IRON_HIDE,    // Warden: longer invulnerability after a hit
    UPG_THORNS,       // getting hit lashes out at enemies nearby
    UPG_MENDING,      // heal a heart every few fights
    UPG_COUNT
} UpgradeId;

typedef struct {
    const char *name;
    const char *desc;     // at most 22 characters (upgrades screen)
    uint32_t cost;
    int requires;         // UpgradeId that must be owned first, or -1
} UpgradeDef;

#define DEEP_POCKETS_PCT   25
#define SECOND_WIND_HP     2
#define FLETCHING_PCT      75    // dagger recharge time, percent
#define IRON_HIDE_PCT      150   // invulnerability after a hit, percent
#define THORNS_DAMAGE      1
#define MENDING_FIGHTS     4     // fights cleared per heart

extern const UpgradeDef upgrade_defs[UPG_COUNT];

// Currency for a finished run: gold found + depth and kill bonuses.
#define META_GOLD_PER_DEPTH 3
#define META_GOLD_PER_KILL  1
int meta_award(int depth, int kills, int gold);

// Records a finished run into meta progress and fills in the result. The
// depth bonus counts only the depths this run went down (from its start).
void meta_finish_run(MetaProgress *m, RunResult *r);

// Depths a run went down: depth - start_depth + 1 (start 0 counts as 1).
int meta_depths_descended(const RunResult *r);

// A boss at depth `depth` fell: on the first way down this opens a
// waypoint to the next area. Returns true if it is new.
bool meta_unlock_waypoint(MetaProgress *m, int depth);
bool meta_waypoint_open(const MetaProgress *m, int area);

// Adds r as the newest run, dropping the oldest when full.
void meta_push_history(RunHistory *h, const RunResult *r);

// What ended a run (RunResult.cause): "Slain by an archer", "Fell into a
// pit", ... and a short form for lists ("Archer", "Pit").
const char *meta_cause_text(uint8_t cause);
const char *meta_cause_short(uint8_t cause);

// Whether the save has bought an upgrade.
bool meta_owned(const MetaProgress *m, UpgradeId id);

// True when the upgrade id needs (if anything) is already owned.
bool meta_prereq_met(const MetaProgress *m, UpgradeId id);

// True when id is not owned yet, its prerequisite is, and m can afford it.
bool meta_can_buy(const MetaProgress *m, UpgradeId id);

typedef enum {
    BUY_OK,
    BUY_OWNED,
    BUY_TOO_POOR,
    BUY_LOCKED,       // needs another upgrade first
} BuyResult;

// Buys an upgrade with meta-currency, if allowed.
BuyResult meta_buy(MetaProgress *m, UpgradeId id);

#endif // DELVE_META_H
