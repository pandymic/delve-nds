// Meta-progression: upgrades, currency, records, history (meta.h).

#include <string.h>

#include "meta.h"
#include "run.h"

// Costs are balanced so the whole set takes roughly 12-18 runs of currency.
const UpgradeDef upgrade_defs[UPG_COUNT] = {
    [UPG_VITALITY]     = { "Vitality",     "+1 max heart",            50, -1 },
    [UPG_SWIFT_DASH]   = { "Swift Dash",   "Dash recharges faster",   80, -1 },
    [UPG_QUIVER]       = { "Quiver",       "+1 dagger charge",       120, -1 },
    [UPG_HEAVY_BLADE]  = { "Heavy Blade",  "+1 sword damage",        150, -1 },
    [UPG_DEEP_POCKETS] = { "Deep Pockets", "+25% gold found",        250, -1 },
    [UPG_KINDLING]     = { "Kindling",     "Runs start blast-ready", 350, -1 },
    [UPG_VITALITY2]    = { "Vitality II",  "+1 more max heart",      450,
                           UPG_VITALITY },
    [UPG_SECOND_WIND]  = { "Second Wind",  "Revive once per run",    900, -1 },
    [UPG_KEEN_EDGE]    = { "Keen Edge",    "+1 more sword damage",  1200,
                           UPG_HEAVY_BLADE },
    // The Hunter's path (daggers) and the Warden's (defence).
    [UPG_FLETCHING]    = { "Fletching",    "Daggers return faster",  300, -1 },
    [UPG_BARBED_TIPS]  = { "Barbed Tips",  "Daggers pierce +1 foe",  650,
                           UPG_FLETCHING },
    [UPG_HUNTERS_MARK] = { "Hunter's Mark", "+1 dagger damage",     1100,
                           UPG_BARBED_TIPS },
    [UPG_IRON_HIDE]    = { "Iron Hide",    "Longer safety when hit", 200, -1 },
    [UPG_THORNS]       = { "Thorns",       "Getting hit lashes out", 550,
                           UPG_IRON_HIDE },
    [UPG_MENDING]      = { "Mending",      "Heal 1 every 4 fights",  950,
                           UPG_THORNS },
};

int meta_award(int depth, int kills, int gold)
{
    if (depth < 0)
        depth = 0;
    if (kills < 0)
        kills = 0;
    if (gold < 0)
        gold = 0;
    return gold + depth * META_GOLD_PER_DEPTH + kills * META_GOLD_PER_KILL;
}

void meta_finish_run(MetaProgress *m, RunResult *r)
{
    r->earned = meta_award(meta_depths_descended(r), r->kills, r->gold);
    r->new_best = (uint32_t)r->depth > m->best_depth;
    if (r->new_best)
        m->best_depth = (uint32_t)r->depth;
    m->total_runs++;
    m->total_kills += (uint32_t)r->kills;
    m->bosses_slain += (uint32_t)r->bosses;
    uint32_t t = m->play_seconds + (uint32_t)(r->seconds > 0 ? r->seconds : 0);
    m->play_seconds = t < m->play_seconds ? UINT32_MAX : t;
    // Saturate rather than wrap on absurd totals.
    uint32_t c = m->currency + (uint32_t)r->earned;
    m->currency = c < m->currency ? UINT32_MAX : c;
}

bool meta_owned(const MetaProgress *m, UpgradeId id)
{
    return (m->upgrades >> id) & 1u;
}

bool meta_prereq_met(const MetaProgress *m, UpgradeId id)
{
    int req = upgrade_defs[id].requires;
    return req < 0 || meta_owned(m, (UpgradeId)req);
}

bool meta_can_buy(const MetaProgress *m, UpgradeId id)
{
    return !meta_owned(m, id) && meta_prereq_met(m, id)
        && m->currency >= upgrade_defs[id].cost;
}

BuyResult meta_buy(MetaProgress *m, UpgradeId id)
{
    if (meta_owned(m, id))
        return BUY_OWNED;
    if (!meta_prereq_met(m, id))
        return BUY_LOCKED;
    if (m->currency < upgrade_defs[id].cost)
        return BUY_TOO_POOR;
    m->currency -= upgrade_defs[id].cost;
    m->upgrades |= 1u << id;
    return BUY_OK;
}

void meta_push_history(RunHistory *h, const RunResult *r)
{
    int keep = h->count < RUN_HISTORY ? h->count : RUN_HISTORY - 1;
    memmove(&h->runs[1], &h->runs[0], (size_t)keep * sizeof(RunResult));
    h->runs[0] = *r;
    h->count = (uint8_t)(keep + 1);
}

static const char *const enemy_causes[ENEMY_TYPE_COUNT][2] = {
    [ENEMY_CHASER]     = { "Slain by an orc",        "Orc" },
    [ENEMY_SHOOTER]    = { "Burned by a mage",       "Mage" },
    [ENEMY_BRUTE]      = { "Crushed by a brute",     "Brute" },
    [ENEMY_ARCHER]     = { "Shot by an archer",      "Archer" },
    [ENEMY_CASTER]     = { "Cursed by a warlock",    "Warlock" },
    [ENEMY_SHAMAN]     = { "Slain by a shaman",      "Shaman" },
    [ENEMY_TURRET]     = { "Struck by a stone idol", "Stone idol" },
    [ENEMY_SLIME]      = { "Smothered by a slime",   "Slime" },
    [ENEMY_DUMMY]      = { "Beaten by the dummy",    "Dummy" },
    [ENEMY_WARLORD]    = { "Slain by the Warlord",   "Warlord" },
    [ENEMY_SLIME_KING] = { "Crushed by the King",    "Slime King" },
    [ENEMY_BAT]        = { "Bitten by a bat",        "Bat" },
    [ENEMY_DIGGER]     = { "Ambushed by a cave crab", "Cave crab" },
    [ENEMY_CRYSTAL]    = { "Pierced by a crystal",   "Crystal" },
    [ENEMY_GOLEM]      = { "Crushed by the Yeti",    "Yeti" },
    [ENEMY_FIRE_SLIME] = { "Burned by a fire slime", "Fire slime" },
    [ENEMY_MAGMA_BRUTE]= { "Trampled by a magma brute", "Lava brute" },
    [ENEMY_IMP]        = { "Scorched by an imp",     "Imp" },
    [ENEMY_MAGMA_LORD] = { "Felled by the Magma Lord", "Magma Lord" },
};

static const char *cause_text(uint8_t cause, int form)
{
    if (cause < ENEMY_TYPE_COUNT && enemy_causes[cause][form])
        return enemy_causes[cause][form];
    switch (cause) {
    case CAUSE_PIT:       return form ? "Pit" : "Fell into a pit";
    case CAUSE_EXPLOSION: return form ? "Explosion" : "Caught in a blast";
    case CAUSE_SPIKES:    return form ? "Spikes" : "Impaled on spikes";
    case CAUSE_FIRE:      return form ? "Fire vent" : "Burned by a fire vent";
    case CAUSE_SAW:       return form ? "Saw blade" : "Cut by a saw blade";
    case CAUSE_ROCKFALL:  return form ? "Rockfall" : "Crushed by falling rock";
    case CAUSE_LAVA:      return form ? "Lava" : "Fell into the lava";
    case CAUSE_WATER:     return form ? "Sewer water" : "Lost in the sewer water";
    case CAUSE_ABANDONED: return form ? "Abandoned" : "Abandoned the run";
    default:              return form ? "?" : "Fell in the depths";
    }
}

const char *meta_cause_text(uint8_t cause)
{
    return cause_text(cause, 0);
}

const char *meta_cause_short(uint8_t cause)
{
    return cause_text(cause, 1);
}

int meta_depths_descended(const RunResult *r)
{
    int start = r->start_depth > 0 ? r->start_depth : 1;
    int n = r->depth - start + 1;
    return n > 0 ? n : 0;
}

bool meta_unlock_waypoint(MetaProgress *m, int depth)
{
    if (!depth_is_boss(depth))
        return false;
    int area = depth / AREA_DEPTHS;   // the area after this boss
    if (area < 1 || area >= AREA_COUNT || meta_waypoint_open(m, area))
        return false;
    m->waypoints |= 1u << area;
    return true;
}

bool meta_waypoint_open(const MetaProgress *m, int area)
{
    return 0 == area || ((m->waypoints >> area) & 1u);
}
