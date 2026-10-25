// First-time tips: their text and when each one shows (hint.h).

#include <stddef.h>

#include "hint.h"
#include "meta.h"
#include "run.h"

typedef struct {
    const char *title;
    const char *lines[HINT_LINES];
    const char *lefty[HINT_LINES];   // left-handed text, if it differs
} HintDef;

static const HintDef hints[HINT_COUNT] = {
    [HINT_CAMP] = { "WELCOME TO CAMP", {
        "Walk down into the crypt",
        "to begin a run.",
        "Spend gold at the shrine",
        "(the idol) and practise",
        "on the training dummy.",
    } },
    [HINT_FIGHT] = { "FIGHT!", {
        "Clear the room to open",
        "the doors. A sword,",
        "B dash (you can't be",
        "hit mid-dash), Y dagger,",
        "X blast when charged.",
    }, {
        "Clear the room to open",
        "the doors. Left sword,",
        "Down dash (can't be hit",
        "mid-dash), Right dagger,",
        "Up blast when charged.",
    } },
    [HINT_DOORS] = { "CHOOSE A DOOR", {
        "Each door shows what",
        "lies beyond: a sword is",
        "a fight for gold, a",
        "heart a rest, a chest",
        "treasure. Walk into one.",
    } },
    [HINT_BOONS] = { "BOONS", {
        "Pick one of three boons.",
        "They stack and last",
        "until the run ends.",
        "The pause screen lists",
        "the ones you have.",
    } },
    [HINT_PITS] = { "PITS", {
        "Falling in costs a",
        "heart. Dash to cross a",
        "gap, and knock enemies",
        "in to finish them.",
    } },
    [HINT_TRAPS] = { "TRAPS", {
        "Spikes, fire vents and",
        "saw blades hurt anyone,",
        "enemies too. Watch for",
        "the warning before",
        "they strike.",
    } },
    [HINT_BOSS] = { "A BOSS", {
        "Every 10th depth a boss",
        "guards the way. Learn",
        "its wind-ups and dash",
        "through its attacks.",
        "Beat it to go deeper.",
    } },
    [HINT_SHRINE] = { "UPGRADES", {
        "You have the gold for",
        "an upgrade. Stand by",
        "the shrine (the idol)",
        "and press A to shop.",
        "Upgrades last forever.",
    }, {
        "You have the gold for",
        "an upgrade. Stand by",
        "the shrine (the idol)",
        "and press Left to shop.",
        "Upgrades last forever.",
    } },
    [HINT_WAYPOINT] = { "WAYPOINTS", {
        "A boss has fallen: the",
        "crypt can now take you",
        "straight to the next",
        "area, with 3 boons for",
        "each area you skip.",
    } },
};

bool hint_seen(const MetaProgress *m, HintId id)
{
    return (m->hints_seen >> id) & 1u;
}

void hint_mark_seen(MetaProgress *m, HintId id)
{
    if (id >= 0 && id < HINT_COUNT)
        m->hints_seen |= 1u << id;
}

static bool can_buy_any(const MetaProgress *m)
{
    for (int i = 0; i < UPG_COUNT; i++)
        if (meta_can_buy(m, (UpgradeId)i))
            return true;
    return false;
}

static bool has_pits(const Room *r)
{
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            if (CELL_PIT == room_cell(r, x, z))
                return true;
    return false;
}

HintId hint_for_room(const Run *run, const MetaProgress *m)
{
    const Room *r = &run->room;
    if (r->hub) {
        if (!hint_seen(m, HINT_CAMP))
            return HINT_CAMP;
        if (!hint_seen(m, HINT_SHRINE) && can_buy_any(m))
            return HINT_SHRINE;
        if (!hint_seen(m, HINT_WAYPOINT) && m->waypoints)
            return HINT_WAYPOINT;
        return HINT_NONE;
    }
    if (RT_BOSS == r->type && !hint_seen(m, HINT_BOSS))
        return HINT_BOSS;
    if (RT_COMBAT == r->type && !hint_seen(m, HINT_FIGHT))
        return HINT_FIGHT;
    if (!hint_seen(m, HINT_PITS) && has_pits(r))
        return HINT_PITS;
    if (!hint_seen(m, HINT_TRAPS) && r->hazard_count > 0)
        return HINT_TRAPS;
    return HINT_NONE;
}

const char *hint_title(HintId id)
{
    return id >= 0 && id < HINT_COUNT ? hints[id].title : "";
}

const char *hint_line(HintId id, int line, bool left_handed)
{
    if (id < 0 || id >= HINT_COUNT || line < 0 || line >= HINT_LINES)
        return NULL;
    const HintDef *h = &hints[id];
    if (left_handed && h->lefty[0])
        return h->lefty[line];
    return h->lines[line];
}
