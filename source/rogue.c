// Rogue, the Easter egg: levels, monsters, items and turns (rogue.h).

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "fixed.h"
#include "rogue.h"

// Tables

// The 26 monsters, after the original's table: level, armour class (lower
// is harder to hit), experience, and up to three attacks as dice.
const RogueMonsterDef rogue_monsters[26] = {
    { 'A', "aquator",       5,  2,   20, { { 0, 0 } },                   SP_RUST,        MF_MEAN },
    { 'B', "bat",           1,  3,    1, { { 1, 2 } },                   SP_NONE,        MF_FLITS },
    { 'C', "centaur",       4,  4,   17, { { 1, 2 }, { 1, 5 }, { 1, 5 } }, SP_NONE,      0 },
    { 'D', "dragon",       10, -1, 5000, { { 1, 8 }, { 1, 8 }, { 3, 10 } }, SP_NONE,     MF_MEAN },
    { 'E', "emu",           1,  7,    2, { { 1, 2 } },                   SP_NONE,        MF_MEAN },
    { 'F', "venus flytrap", 8,  3,   80, { { 1, 6 } },                   SP_HOLD,        MF_MEAN | MF_STILL },
    { 'G', "griffin",      13,  2, 2000, { { 4, 3 }, { 3, 5 } },         SP_NONE,        MF_MEAN | MF_REGEN },
    { 'H', "hobgoblin",     1,  5,    3, { { 1, 8 } },                   SP_NONE,        MF_MEAN },
    { 'I', "ice monster",   1,  9,    5, { { 0, 0 } },                   SP_FREEZE,      0 },
    { 'J', "jabberwock",   15,  6, 3000, { { 2, 12 }, { 2, 4 } },        SP_NONE,        0 },
    { 'K', "kestrel",       1,  7,    1, { { 1, 4 } },                   SP_NONE,        MF_MEAN },
    { 'L', "leprechaun",    3,  8,   10, { { 1, 1 } },                   SP_STEAL_GOLD,  0 },
    { 'M', "medusa",        8,  2,  200, { { 3, 4 }, { 3, 4 }, { 2, 5 } }, SP_CONFUSE,   MF_MEAN },
    { 'N', "nymph",         3,  9,   37, { { 0, 0 } },                   SP_STEAL_ITEM,  0 },
    { 'O', "orc",           1,  6,    5, { { 1, 8 } },                   SP_NONE,        MF_MEAN },
    { 'P', "phantom",       8,  3,  120, { { 4, 4 } },                   SP_NONE,        MF_INVIS },
    { 'Q', "quagga",        3,  3,   15, { { 1, 5 }, { 1, 5 } },         SP_NONE,        MF_MEAN },
    { 'R', "rattlesnake",   2,  3,    9, { { 1, 6 } },                   SP_WEAKEN,      MF_MEAN },
    { 'S', "snake",         1,  5,    2, { { 1, 3 } },                   SP_NONE,        MF_MEAN },
    { 'T', "troll",         6,  4,  120, { { 1, 8 }, { 1, 8 }, { 2, 6 } }, SP_NONE,      MF_MEAN | MF_REGEN },
    { 'U', "black unicorn", 7, -2,  190, { { 1, 9 }, { 1, 9 }, { 2, 9 } }, SP_NONE,      MF_MEAN },
    { 'V', "vampire",       8,  1,  350, { { 1, 10 } },                  SP_DRAIN_HP,    MF_MEAN | MF_REGEN },
    { 'W', "wraith",        5,  4,   55, { { 1, 6 } },                   SP_DRAIN_LEVEL, 0 },
    { 'X', "xeroc",         7,  7,  100, { { 4, 4 } },                   SP_NONE,        MF_MIMIC },
    { 'Y', "yeti",          4,  6,   50, { { 1, 6 }, { 1, 6 } },         SP_NONE,        0 },
    { 'Z', "zombie",        2,  8,    6, { { 1, 8 } },                   SP_NONE,        MF_MEAN },
};

// Monsters roughly in order of danger; a level draws from a window around
// its depth.
static const char level_order[26] = {
    'K', 'E', 'B', 'S', 'H', 'I', 'R', 'O', 'Z', 'L', 'C', 'Q', 'A',
    'N', 'Y', 'F', 'T', 'W', 'P', 'X', 'U', 'M', 'V', 'G', 'J', 'D',
};

static const char *const potion_colors[] = {
    "amber", "aquamarine", "black", "blue", "brown", "clear", "crimson",
    "cyan", "ecru", "gold", "green", "grey", "magenta", "orange", "pink",
    "plaid", "purple", "red", "silver", "tan", "tangerine", "topaz",
    "turquoise", "violet", "white", "yellow",
};
#define POTION_COLORS ARRAY_LEN(potion_colors)

static const char *const potion_names[P_KINDS] = {
    "healing", "extra healing", "gain strength", "restore strength",
    "raise level", "see invisible", "detect monsters", "confusion",
    "blindness", "sleep", "poison",
};
static const uint8_t potion_weights[P_KINDS] = {
    15, 6, 12, 12, 3, 6, 8, 9, 7, 9, 9,
};

static const char *const scroll_names[S_KINDS] = {
    "identify", "magic mapping", "teleportation", "enchant armor",
    "enchant weapon", "hold monster", "sleep", "create monster",
    "aggravate monsters", "scare monster", "remove curse",
};
static const uint8_t scroll_weights[S_KINDS] = {
    25, 8, 10, 9, 8, 4, 4, 6, 4, 5, 8,
};

// Scroll titles are made of these.
static const char *const syllables[] = {
    "ab", "ag", "aks", "ala", "an", "ankh", "arg", "ash", "bek", "bu",
    "cer", "dan", "el", "eep", "fid", "gan", "hi", "ion", "ix", "ka",
    "kli", "la", "lem", "mar", "mik", "nag", "neg", "nih", "nom", "oo",
    "pay", "po", "qua", "re", "ri", "sne", "ta", "uk", "ul", "vo", "xu",
    "za", "zok",
};
#define SYLLABLES ARRAY_LEN(syllables)

typedef struct {
    const char *name;
    uint8_t n, sides;
} WeaponDef;

static const WeaponDef weapons[W_KINDS] = {
    { "mace", 2, 4 },
    { "long sword", 3, 4 },
    { "dagger", 1, 6 },
    { "two handed sword", 4, 4 },
    { "spear", 2, 3 },
};

typedef struct {
    const char *name;
    int8_t ac;
    uint8_t weight;
} ArmorDef;

static const ArmorDef armors[A_KINDS] = {
    { "leather armor", 8, 20 },
    { "ring mail", 7, 15 },
    { "studded leather armor", 7, 15 },
    { "scale mail", 6, 13 },
    { "chain mail", 5, 12 },
    { "splint mail", 4, 10 },
    { "banded mail", 4, 10 },
    { "plate mail", 3, 5 },
};

static const uint32_t exp_levels[] = {
    10, 20, 40, 80, 160, 320, 640, 1300, 2600, 5200, 13000, 26000, 50000,
    100000, 200000, 400000, 800000, 2000000, 4000000, 8000000,
};
#define EXP_LEVELS ARRAY_LEN(exp_levels)

#define FOOD_START      1300
#define FOOD_MAX        2000
#define FOOD_HUNGRY     300
#define FOOD_WEAK       150
#define FOOD_STARVE     (-500)
#define NO_ROOM         0xFF
#define WANDER_CHANCE   70    // 1 in N turns a wandering monster appears
#define CHASE_RANGE     30    // path steps a monster will follow

// Small helpers

static int rnd(Rogue *g, int n)
{
    return n > 0 ? (int)rng_range(&g->rng, (uint32_t)n) : 0;
}

static int roll(Rogue *g, int n, int sides)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += rnd(g, sides) + 1;
    return total;
}

static bool in_map(int x, int y)
{
    return x >= 0 && y >= 0 && x < RG_W && y < RG_H;
}

static bool passable_tile(int t)
{
    return RT_FLOOR == t || RT_DOOR == t || RT_CORRIDOR == t || RT_STAIRS == t;
}

static bool passable(const Rogue *g, int x, int y)
{
    return in_map(x, y) && passable_tile(g->map[y][x].tile);
}

// Diagonal steps may not cut a corner or pass through a doorway (as in the
// original).
static bool diag_ok(const Rogue *g, int x0, int y0, int x1, int y1)
{
    if (x0 == x1 || y0 == y1)
        return true;
    if (RT_DOOR == g->map[y0][x0].tile || RT_DOOR == g->map[y1][x1].tile)
        return false;
    return passable(g, x1, y0) && passable(g, x0, y1);
}

static int monster_at(const Rogue *g, int x, int y)
{
    for (int i = 0; i < RG_MAX_MONSTERS; i++)
        if (g->monsters[i].active && g->monsters[i].x == x
            && g->monsters[i].y == y)
            return i;
    return -1;
}

static int item_at(const Rogue *g, int x, int y)
{
    for (int i = 0; i < RG_MAX_ITEMS; i++)
        if (IC_NONE != g->floor[i].cls && g->floor[i].x == x
            && g->floor[i].y == y)
            return i;
    return -1;
}

static const char *article(const char *word)
{
    return strchr("aeiou", word[0]) ? "an" : "a";
}

static const char *mon_name(const Rogue *g, const RogueMonster *m)
{
    const RogueMonsterDef *d = &rogue_monsters[m->type];
    if ((d->flags & MF_INVIS) && !g->player.see_invisible)
        return "it";
    return d->name;
}

void rogue_message(Rogue *g, const char *fmt, ...)
{
    char buf[RG_MSG_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // Messages start with a capital letter, as on the original's top line.
    if (buf[0] >= 'a' && buf[0] <= 'z')
        buf[0] = (char)(buf[0] - 'a' + 'A');
    int slot = g->msg_count < RG_MSGS ? g->msg_count++ : RG_MSGS - 1;
    memcpy(g->msgs[slot], buf, sizeof(buf));
    memcpy(g->history[g->history_head], buf, sizeof(buf));
    g->history_head = (uint8_t)((g->history_head + 1) % RG_HISTORY);
}

// Items

static int weighted(Rogue *g, const uint8_t *weights, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += weights[i];
    int r = rnd(g, total);
    for (int i = 0; i < n; i++) {
        if (r < weights[i])
            return i;
        r -= weights[i];
    }
    return n - 1;
}

static void make_item(Rogue *g, RogueItem *it, int cls)
{
    memset(it, 0, sizeof(*it));
    if (IC_NONE == cls) {
        int r = rnd(g, 100);
        cls = r < 27 ? IC_POTION : r < 64 ? IC_SCROLL : r < 81 ? IC_FOOD
            : r < 90 ? IC_WEAPON : IC_ARMOR;
    }
    it->cls = (uint8_t)cls;
    it->count = 1;
    switch (cls) {
    case IC_POTION:
        it->kind = (uint8_t)weighted(g, potion_weights, P_KINDS);
        break;
    case IC_SCROLL:
        it->kind = (uint8_t)weighted(g, scroll_weights, S_KINDS);
        break;
    case IC_FOOD:
        it->kind = 0 == rnd(g, 10) ? FOOD_FRUIT : FOOD_RATION;
        break;
    case IC_WEAPON:
        it->kind = (uint8_t)rnd(g, W_KINDS);
        if (0 == rnd(g, 10)) {
            it->flags |= IF_CURSED;
            it->hit = (int8_t)-(rnd(g, 3) + 1);
        } else if (0 == rnd(g, 10)) {
            it->hit = (int8_t)(rnd(g, 3) + 1);
            it->dmg = (int8_t)rnd(g, 2);
        }
        break;
    case IC_ARMOR: {
        uint8_t w[A_KINDS];
        for (int i = 0; i < A_KINDS; i++)
            w[i] = armors[i].weight;
        it->kind = (uint8_t)weighted(g, w, A_KINDS);
        int r = rnd(g, 100);
        if (r < 20) {
            it->flags |= IF_CURSED;
            it->hit = (int8_t)-(rnd(g, 3) + 1);
        } else if (r < 28) {
            it->hit = (int8_t)(rnd(g, 3) + 1);
        }
        break;
    }
    default:
        break;
    }
}

static bool stacks(int cls)
{
    return IC_POTION == cls || IC_SCROLL == cls || IC_FOOD == cls;
}

// Returns the pack index, or -1 if the pack is full.
static int pack_add(Rogue *g, const RogueItem *it)
{
    if (stacks(it->cls)) {
        for (int i = 0; i < g->pack_count; i++) {
            RogueItem *p = &g->pack[i];
            if (p->cls == it->cls && p->kind == it->kind && p->count < 99) {
                p->count = (uint8_t)(p->count + it->count);
                return i;
            }
        }
    }
    if (g->pack_count >= RG_PACK)
        return -1;
    g->pack[g->pack_count] = *it;
    g->pack[g->pack_count].flags &= (uint8_t)~IF_DROPPED;
    return g->pack_count++;
}

// Removes one item (or a whole stack) and keeps the equipment indices valid.
static void pack_take(Rogue *g, int idx, bool whole)
{
    RogueItem *p = &g->pack[idx];
    if (!whole && p->count > 1) {
        p->count--;
        return;
    }
    for (int i = idx; i < g->pack_count - 1; i++)
        g->pack[i] = g->pack[i + 1];
    g->pack_count--;
    RoguePlayer *pl = &g->player;
    if (pl->weapon == idx)
        pl->weapon = -1;
    else if (pl->weapon > idx)
        pl->weapon--;
    if (pl->armor == idx)
        pl->armor = -1;
    else if (pl->armor > idx)
        pl->armor--;
}

static int floor_add(Rogue *g, const RogueItem *it, int x, int y)
{
    for (int i = 0; i < RG_MAX_ITEMS; i++) {
        if (IC_NONE == g->floor[i].cls) {
            g->floor[i] = *it;
            g->floor[i].x = (int8_t)x;
            g->floor[i].y = (int8_t)y;
            return i;
        }
    }
    return -1;
}

static char item_glyph(int cls)
{
    switch (cls) {
    case IC_GOLD:   return '*';
    case IC_FOOD:   return ':';
    case IC_POTION: return '!';
    case IC_SCROLL: return '?';
    case IC_WEAPON: return ')';
    case IC_ARMOR:  return ']';
    case IC_AMULET: return ',';
    default:        return ' ';
    }
}

void rogue_item_name(const Rogue *g, const RogueItem *it, char *out, int size)
{
    int n = it->count;
    bool many = n > 1;
    char buf[RG_MSG_LEN];
    switch (it->cls) {
    case IC_GOLD:
        snprintf(buf, sizeof(buf), "%u gold pieces", (unsigned)it->gold);
        break;
    case IC_FOOD:
        if (FOOD_FRUIT == it->kind)
            snprintf(buf, sizeof(buf), many ? "%d slime-molds" : "a slime-mold", n);
        else
            snprintf(buf, sizeof(buf), many ? "%d food rations" : "a food ration", n);
        break;
    case IC_POTION: {
        const char *color = potion_colors[g->potion_color[it->kind]];
        if (g->potion_known[it->kind]) {
            if (many)
                snprintf(buf, sizeof(buf), "%d potions of %s", n,
                         potion_names[it->kind]);
            else
                snprintf(buf, sizeof(buf), "a potion of %s",
                         potion_names[it->kind]);
        } else if (many) {
            snprintf(buf, sizeof(buf), "%d %s potions", n, color);
        } else {
            snprintf(buf, sizeof(buf), "%s %s potion", article(color), color);
        }
        break;
    }
    case IC_SCROLL:
        if (g->scroll_known[it->kind]) {
            if (many)
                snprintf(buf, sizeof(buf), "%d scrolls of %s", n,
                         scroll_names[it->kind]);
            else
                snprintf(buf, sizeof(buf), "a scroll of %s",
                         scroll_names[it->kind]);
        } else if (many) {
            snprintf(buf, sizeof(buf), "%d scrolls titled '%s'", n,
                     g->scroll_title[it->kind]);
        } else {
            snprintf(buf, sizeof(buf), "a scroll titled '%s'",
                     g->scroll_title[it->kind]);
        }
        break;
    case IC_WEAPON:
        if (it->flags & IF_KNOWN)
            snprintf(buf, sizeof(buf), "%+d,%+d %s", it->hit, it->dmg,
                     weapons[it->kind].name);
        else
            snprintf(buf, sizeof(buf), "%s %s", article(weapons[it->kind].name),
                     weapons[it->kind].name);
        break;
    case IC_ARMOR:
        if (it->flags & IF_KNOWN)
            snprintf(buf, sizeof(buf), "%+d %s [%d]", it->hit,
                     armors[it->kind].name, armors[it->kind].ac - it->hit);
        else
            snprintf(buf, sizeof(buf), "%s", armors[it->kind].name);
        break;
    case IC_AMULET:
        snprintf(buf, sizeof(buf), "the Amulet of Yendor");
        break;
    default:
        buf[0] = '\0';
        break;
    }
    snprintf(out, (size_t)size, "%s", buf);
}

const char *rogue_use_verb(const Rogue *g, int idx)
{
    if (idx < 0 || idx >= g->pack_count)
        return NULL;
    switch (g->pack[idx].cls) {
    case IC_POTION: return "quaff";
    case IC_SCROLL: return "read";
    case IC_FOOD:   return "eat";
    case IC_WEAPON: return idx == g->player.weapon ? NULL : "wield";
    case IC_ARMOR:  return idx == g->player.armor ? "take off" : "wear";
    default:        return NULL;
    }
}

// Level generation

// Cell boundaries of the 3 x 3 room grid.
static const int8_t grid_x[4] = { 0, 10, 21, RG_W };
static const int8_t grid_y[4] = { 0, 7, 14, RG_H };

static void set_cell(Rogue *g, int x, int y, int tile, int room)
{
    g->map[y][x].tile = (uint8_t)tile;
    g->map[y][x].room = (uint8_t)room;
}

static void make_rooms(Rogue *g)
{
    bool gone[RG_ROOMS] = { false };
    int ngone = rnd(g, 4);
    for (int i = 0; i < ngone; i++)
        gone[rnd(g, RG_ROOMS)] = true;

    for (int i = 0; i < RG_ROOMS; i++) {
        RogueRoom *r = &g->rooms[i];
        int cx = i % 3, cy = i / 3;
        int x0 = grid_x[cx], y0 = grid_y[cy];
        int cw = grid_x[cx + 1] - x0, ch = grid_y[cy + 1] - y0;
        r->gone = gone[i];
        r->dark = !r->gone && rnd(g, 10) < g->level - 1;
        if (r->gone) {
            r->x = (int8_t)(x0 + 1 + rnd(g, cw - 2));
            r->y = (int8_t)(y0 + 1 + rnd(g, ch - 2));
            r->w = r->h = 1;
            set_cell(g, r->x, r->y, RT_CORRIDOR, NO_ROOM);
            continue;
        }
        r->w = (int8_t)(4 + rnd(g, cw - 4));
        r->h = (int8_t)(4 + rnd(g, ch - 4));
        r->x = (int8_t)(x0 + rnd(g, cw - r->w));
        r->y = (int8_t)(y0 + rnd(g, ch - r->h));
        for (int y = r->y; y < r->y + r->h; y++) {
            for (int x = r->x; x < r->x + r->w; x++) {
                int t = RT_FLOOR;
                if (y == r->y || y == r->y + r->h - 1)
                    t = RT_HWALL;
                else if (x == r->x || x == r->x + r->w - 1)
                    t = RT_VWALL;
                set_cell(g, x, y, t, i);
            }
        }
    }
}

static void dig(Rogue *g, int x, int y)
{
    if (in_map(x, y) && RT_ROCK == g->map[y][x].tile)
        g->map[y][x].tile = RT_CORRIDOR;
}

// Where a corridor leaves room r through the given side: a door on the
// wall, or the junction itself for a gone room. (sx, sy) is the first
// corridor cell.
static void exit_point(Rogue *g, int ri, int dx, int dy, int *sx, int *sy)
{
    RogueRoom *r = &g->rooms[ri];
    if (r->gone) {
        *sx = r->x;
        *sy = r->y;
        return;
    }
    int x, y;
    if (dx) {
        x = dx > 0 ? r->x + r->w - 1 : r->x;
        y = r->y + 1 + rnd(g, r->h - 2);
    } else {
        y = dy > 0 ? r->y + r->h - 1 : r->y;
        x = r->x + 1 + rnd(g, r->w - 2);
    }
    g->map[y][x].tile = RT_DOOR;
    *sx = x + dx;
    *sy = y + dy;
}

// Joins grid neighbours a and b (a left of or above b) with a corridor that
// turns once halfway.
static void connect(Rogue *g, int a, int b)
{
    bool horiz = b == a + 1;
    int sx, sy, ex, ey;
    exit_point(g, a, horiz ? 1 : 0, horiz ? 0 : 1, &sx, &sy);
    exit_point(g, b, horiz ? -1 : 0, horiz ? 0 : -1, &ex, &ey);
    if (horiz) {
        int turn = sx + rnd(g, ex - sx + 1);
        for (int x = sx; x <= turn; x++)
            dig(g, x, sy);
        for (int y = imin(sy, ey); y <= imax(sy, ey); y++)
            dig(g, turn, y);
        for (int x = turn; x <= ex; x++)
            dig(g, x, ey);
    } else {
        int turn = sy + rnd(g, ey - sy + 1);
        for (int y = sy; y <= turn; y++)
            dig(g, sx, y);
        for (int x = imin(sx, ex); x <= imax(sx, ex); x++)
            dig(g, x, turn);
        for (int y = turn; y <= ey; y++)
            dig(g, ex, y);
    }
}

static bool grid_adjacent(int a, int b)
{
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    return (b == a + 1 && 2 != a % 3) || b == a + 3;
}

static void make_corridors(Rogue *g)
{
    bool joined[RG_ROOMS][RG_ROOMS] = { { false } };
    bool in_tree[RG_ROOMS] = { false };
    int count = 1;
    in_tree[rnd(g, RG_ROOMS)] = true;
    // Random spanning tree over the grid, so every room is reachable.
    while (count < RG_ROOMS) {
        int a = rnd(g, RG_ROOMS), b = rnd(g, RG_ROOMS);
        if (!in_tree[a] || in_tree[b] || !grid_adjacent(a, b))
            continue;
        in_tree[b] = true;
        count++;
        joined[a][b] = joined[b][a] = true;
        connect(g, imin(a, b), imax(a, b));
    }
    // A few extra loops.
    for (int i = rnd(g, 5); i > 0; i--) {
        int a = rnd(g, RG_ROOMS), b = rnd(g, RG_ROOMS);
        if (a == b || !grid_adjacent(a, b) || joined[a][b])
            continue;
        joined[a][b] = joined[b][a] = true;
        connect(g, imin(a, b), imax(a, b));
    }
}

static int random_room(Rogue *g)
{
    for (;;) {
        int r = rnd(g, RG_ROOMS);
        if (!g->rooms[r].gone)
            return r;
    }
}

// A free floor cell inside room r (no stairs, item, monster or player).
static bool free_spot(Rogue *g, int r, int *x, int *y)
{
    const RogueRoom *rm = &g->rooms[r];
    for (int tries = 0; tries < 40; tries++) {
        int px = rm->x + 1 + rnd(g, rm->w - 2);
        int py = rm->y + 1 + rnd(g, rm->h - 2);
        if (RT_FLOOR != g->map[py][px].tile || g->map[py][px].trap)
            continue;
        if (item_at(g, px, py) >= 0 || monster_at(g, px, py) >= 0)
            continue;
        if (px == g->player.x && py == g->player.y)
            continue;
        *x = px;
        *y = py;
        return true;
    }
    return false;
}

static int pick_monster_type(Rogue *g, bool wandering)
{
    for (;;) {
        int d = g->level + rnd(g, 10) - 6;
        if (d < 0)
            d = rnd(g, 5);
        if (d > 25)
            d = rnd(g, 5) + 21;
        int type = level_order[d] - 'A';
        // Wanderers are never the stationary or disguised kinds.
        if (wandering && (rogue_monsters[type].flags & (MF_STILL | MF_MIMIC)))
            continue;
        return type;
    }
}

static RogueMonster *spawn_monster(Rogue *g, int type, int x, int y, bool awake)
{
    for (int i = 0; i < RG_MAX_MONSTERS; i++) {
        RogueMonster *m = &g->monsters[i];
        if (m->active)
            continue;
        const RogueMonsterDef *d = &rogue_monsters[type];
        memset(m, 0, sizeof(*m));
        m->active = true;
        m->type = (uint8_t)type;
        m->x = (int8_t)x;
        m->y = (int8_t)y;
        m->hp = m->max_hp = (int16_t)imax(1, roll(g, d->level, 8));
        m->awake = awake;
        if (d->flags & MF_MIMIC) {
            static const char disguises[] = "!?])*:%";
            m->disguise = disguises[rnd(g, (int)sizeof(disguises) - 1)];
        }
        return m;
    }
    return NULL;
}

static void place_player(Rogue *g)
{
    int x, y;
    for (;;) {
        int r = random_room(g);
        if (free_spot(g, r, &x, &y))
            break;
    }
    g->player.x = (int8_t)x;
    g->player.y = (int8_t)y;
}

static void update_view(Rogue *g);

static void new_level(Rogue *g)
{
    memset(g->map, 0, sizeof(g->map));
    for (int y = 0; y < RG_H; y++)
        for (int x = 0; x < RG_W; x++)
            g->map[y][x].room = NO_ROOM;
    memset(g->monsters, 0, sizeof(g->monsters));
    memset(g->floor, 0, sizeof(g->floor));
    g->player.held = false;
    g->player.stuck = 0;
    if (g->level > g->max_level)
        g->max_level = g->level;

    make_rooms(g);
    make_corridors(g);

    int x, y;
    while (!free_spot(g, random_room(g), &x, &y))
        ;
    g->map[y][x].tile = RT_STAIRS;

    // Traps, more of them deeper down.
    if (rnd(g, 10) < g->level) {
        int n = imin(rnd(g, g->level / 4 + 1) + 1, 10);
        for (int i = 0; i < n; i++)
            if (free_spot(g, random_room(g), &x, &y))
                g->map[y][x].trap = (uint8_t)(1 + rnd(g, TRAP_KINDS - 1));
    }

    // Gold in about half the rooms, and up to nine other things.
    RogueItem it;
    bool food = false;
    for (int r = 0; r < RG_ROOMS; r++) {
        if (g->rooms[r].gone || rnd(g, 2) || !free_spot(g, r, &x, &y))
            continue;
        memset(&it, 0, sizeof(it));
        it.cls = IC_GOLD;
        it.gold = (uint16_t)(rnd(g, 50 + 10 * g->level) + 2);
        floor_add(g, &it, x, y);
    }
    for (int i = 0; i < 9; i++) {
        if (0 != rnd(g, 3) || !free_spot(g, random_room(g), &x, &y))
            continue;
        make_item(g, &it, IC_NONE);
        food |= IC_FOOD == it.cls;
        floor_add(g, &it, x, y);
    }
    // Never too long without something to eat.
    if (!food && ++g->levels_without_food >= 3
        && free_spot(g, random_room(g), &x, &y)) {
        make_item(g, &it, IC_FOOD);
        floor_add(g, &it, x, y);
        food = true;
    }
    if (food)
        g->levels_without_food = 0;
    if (g->level >= RG_AMULET_LEVEL && !g->player.has_amulet
        && free_spot(g, random_room(g), &x, &y)) {
        memset(&it, 0, sizeof(it));
        it.cls = IC_AMULET;
        it.count = 1;
        floor_add(g, &it, x, y);
    }

    // A monster in about a quarter of the rooms, mostly asleep.
    for (int r = 0; r < RG_ROOMS; r++) {
        if (g->rooms[r].gone || 0 != rnd(g, 4) || !free_spot(g, r, &x, &y))
            continue;
        int type = pick_monster_type(g, false);
        bool awake = (rogue_monsters[type].flags & MF_MEAN) && 0 != rnd(g, 3);
        if (rogue_monsters[type].flags & (MF_STILL | MF_MIMIC))
            awake = false;
        spawn_monster(g, type, x, y, awake);
    }

    place_player(g);
    update_view(g);
}

static void make_names(Rogue *g)
{
    // Distinct colours for the potions.
    uint8_t order[POTION_COLORS];
    for (int i = 0; i < POTION_COLORS; i++)
        order[i] = (uint8_t)i;
    for (int i = POTION_COLORS - 1; i > 0; i--) {
        int j = rnd(g, i + 1);
        uint8_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (int i = 0; i < P_KINDS; i++)
        g->potion_color[i] = order[i];

    // Gibberish titles of one or two words.
    for (int i = 0; i < S_KINDS; i++) {
        char *t = g->scroll_title[i];
        t[0] = '\0';
        int words = 1 + rnd(g, 2);
        for (int w = 0; w < words; w++) {
            if (w)
                strcat(t, " ");
            int sylls = 1 + rnd(g, 2);
            for (int s = 0; s < sylls; s++) {
                const char *syl = syllables[rnd(g, SYLLABLES)];
                if (strlen(t) + strlen(syl) < sizeof(g->scroll_title[i]) - 1)
                    strcat(t, syl);
            }
        }
    }
}

void rogue_new(Rogue *g, uint32_t seed, const char *name)
{
    memset(g, 0, sizeof(*g));
    rng_seed(&g->rng, seed);
    snprintf(g->name, sizeof(g->name), "%s", name && name[0] ? name : "Rodney");
    make_names(g);

    RoguePlayer *p = &g->player;
    p->hp = p->max_hp = 12;
    p->str = p->max_str = 16;
    p->xlevel = 1;
    p->food = FOOD_START;
    p->weapon = p->armor = -1;

    // Starting kit, as in the original: a +1,+1 mace, +1 ring mail and
    // some food.
    RogueItem it;
    memset(&it, 0, sizeof(it));
    it.cls = IC_FOOD;
    it.kind = FOOD_RATION;
    it.count = 1;
    pack_add(g, &it);
    memset(&it, 0, sizeof(it));
    it.cls = IC_WEAPON;
    it.kind = W_MACE;
    it.count = 1;
    it.hit = it.dmg = 1;
    it.flags = IF_KNOWN;
    p->weapon = (int8_t)pack_add(g, &it);
    memset(&it, 0, sizeof(it));
    it.cls = IC_ARMOR;
    it.kind = A_RING;
    it.count = 1;
    it.hit = 1;
    it.flags = IF_KNOWN;
    p->armor = (int8_t)pack_add(g, &it);

    g->level = 1;
    new_level(g);
    rogue_message(g, "hello %s, welcome to the Dungeons of Doom", g->name);
}

// View and distances

static void update_view(Rogue *g)
{
    memset(g->visible, 0, sizeof(g->visible));
    const RoguePlayer *p = &g->player;
    if (p->blind) {
        g->visible[p->y][p->x] = true;
    } else {
        int room = g->map[p->y][p->x].room;
        if (NO_ROOM != room && !g->rooms[room].dark) {
            for (int y = 0; y < RG_H; y++)
                for (int x = 0; x < RG_W; x++)
                    if (g->map[y][x].room == room)
                        g->visible[y][x] = true;
        }
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (in_map(p->x + dx, p->y + dy))
                    g->visible[p->y + dy][p->x + dx] = true;
    }
    for (int y = 0; y < RG_H; y++)
        for (int x = 0; x < RG_W; x++)
            if (g->visible[y][x])
                g->map[y][x].flags |= CELL_SEEN;
}

// Breadth-first path distances from the player, for the monsters.
static void update_distances(Rogue *g)
{
    static int8_t queue[RG_W * RG_H][2];
    memset(g->dist, 0xFF, sizeof(g->dist));
    int head = 0, tail = 0;
    g->dist[g->player.y][g->player.x] = 0;
    queue[tail][0] = g->player.x;
    queue[tail][1] = g->player.y;
    tail++;
    while (head < tail) {
        int x = queue[head][0], y = queue[head][1];
        head++;
        int d = g->dist[y][x];
        if (d >= CHASE_RANGE)
            continue;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if ((!dx && !dy) || !passable(g, nx, ny)
                    || 0xFF != g->dist[ny][nx] || !diag_ok(g, x, y, nx, ny))
                    continue;
                g->dist[ny][nx] = (uint8_t)(d + 1);
                queue[tail][0] = (int8_t)nx;
                queue[tail][1] = (int8_t)ny;
                tail++;
            }
        }
    }
}

// Combat

// The original's to-hit roll: d20 plus bonuses against 20 - level - armour.
static bool swing(Rogue *g, int at_level, int op_armor, int bonus)
{
    return rnd(g, 20) + bonus >= (20 - at_level) - op_armor;
}

int rogue_armor_class(const Rogue *g)
{
    const RoguePlayer *p = &g->player;
    if (p->armor < 0)
        return 10;
    const RogueItem *a = &g->pack[p->armor];
    return armors[a->kind].ac - a->hit;
}

static int str_hit_bonus(int str)
{
    return str < 7 ? str - 7 : str < 17 ? 0 : str < 19 ? 1 : 2;
}

static int str_dmg_bonus(int str)
{
    return str < 7 ? str - 7 : str < 16 ? 0 : str - 15 > 4 ? 4 : str - 15;
}

uint32_t rogue_exp_needed(int xlevel)
{
    if (xlevel < 1)
        return 0;
    if (xlevel > EXP_LEVELS)
        return 0xFFFFFFFFu;
    return exp_levels[xlevel - 1];
}

static void check_level(Rogue *g)
{
    RoguePlayer *p = &g->player;
    while (p->exp >= rogue_exp_needed(p->xlevel)) {
        p->xlevel++;
        int gain = roll(g, 1, 10);
        p->max_hp = (int16_t)(p->max_hp + gain);
        p->hp = (int16_t)(p->hp + gain);
        rogue_message(g, "welcome to level %d", p->xlevel);
    }
}

static void die(Rogue *g, uint8_t cause)
{
    if (RS_DEAD == g->status)
        return;
    g->player.hp = 0;
    g->status = RS_DEAD;
    g->cause = cause;
}

static void kill_monster(Rogue *g, RogueMonster *m)
{
    const RogueMonsterDef *d = &rogue_monsters[m->type];
    rogue_message(g, "you have defeated the %s", mon_name(g, m));
    g->player.exp += d->exp + (uint32_t)m->max_hp / 6;
    // A leprechaun's gold is left behind.
    if (SP_STEAL_GOLD == d->special && item_at(g, m->x, m->y) < 0) {
        RogueItem it;
        memset(&it, 0, sizeof(it));
        it.cls = IC_GOLD;
        it.gold = (uint16_t)(rnd(g, 50 + 10 * g->level) + 10);
        floor_add(g, &it, m->x, m->y);
    }
    m->active = false;
    check_level(g);
}

static void attack_monster(Rogue *g, RogueMonster *m)
{
    RoguePlayer *p = &g->player;
    const RogueMonsterDef *d = &rogue_monsters[m->type];
    if (m->disguise) {
        m->disguise = 0;
        m->awake = true;
        rogue_message(g, "wait! That's a %s!", d->name);
        return;
    }
    int bonus = str_hit_bonus(p->str);
    int n = 1, sides = 2, plus = 0;
    if (p->weapon >= 0) {
        const RogueItem *w = &g->pack[p->weapon];
        bonus += w->hit;
        n = weapons[w->kind].n;
        sides = weapons[w->kind].sides;
        plus = w->dmg;
    }
    if (!m->awake || m->held)
        bonus += 4;   // sleeping targets are easy
    m->awake = true;
    if (!swing(g, p->xlevel, d->armor, bonus)) {
        rogue_message(g, "you miss the %s", mon_name(g, m));
        return;
    }
    int dmg = imax(1, roll(g, n, sides) + plus + str_dmg_bonus(p->str));
    m->hp = (int16_t)(m->hp - dmg);
    if (m->hp <= 0)
        kill_monster(g, m);
    else
        rogue_message(g, "you hit the %s", mon_name(g, m));
}

static void rust_armor(Rogue *g)
{
    RoguePlayer *p = &g->player;
    if (p->armor < 0)
        return;
    RogueItem *a = &g->pack[p->armor];
    if (A_LEATHER == a->kind || armors[a->kind].ac - a->hit >= 9)
        return;
    a->hit--;
    rogue_message(g, "your armor weakens, oh my!");
}

static void monster_special(Rogue *g, RogueMonster *m)
{
    RoguePlayer *p = &g->player;
    switch (rogue_monsters[m->type].special) {
    case SP_RUST:
        rust_armor(g);
        break;
    case SP_HOLD:
        if (!p->held)
            rogue_message(g, "you are being held");
        p->held = true;
        break;
    case SP_FREEZE:
        if (0 == rnd(g, 3)) {
            p->frozen = (uint8_t)(p->frozen + rnd(g, 3) + 2);
            rogue_message(g, "you are frozen by the %s", mon_name(g, m));
        }
        break;
    case SP_STEAL_GOLD:
        if (p->gold) {
            uint32_t lost = (uint32_t)(rnd(g, 50 + 10 * g->level) + 2);
            p->gold = p->gold > lost ? p->gold - lost : 0;
            rogue_message(g, "your purse feels lighter");
            m->active = false;
        }
        break;
    case SP_STEAL_ITEM: {
        int candidates[RG_PACK], n = 0;
        for (int i = 0; i < g->pack_count; i++)
            if (i != p->weapon && i != p->armor
                && IC_AMULET != g->pack[i].cls)
                candidates[n++] = i;
        if (n) {
            int idx = candidates[rnd(g, n)];
            char name[RG_MSG_LEN];
            RogueItem one = g->pack[idx];
            one.count = 1;
            rogue_item_name(g, &one, name, sizeof(name));
            pack_take(g, idx, false);
            rogue_message(g, "she stole %s!", name);
            m->active = false;
        }
        break;
    }
    case SP_CONFUSE:
        p->confused = (uint8_t)(p->confused + rnd(g, 4) + 2);
        rogue_message(g, "the %s's gaze has confused you", mon_name(g, m));
        break;
    case SP_WEAKEN:
        if (0 == rnd(g, 2) && p->str > 3) {
            p->str--;
            rogue_message(g, "you feel a bite in your leg and now feel weaker");
        }
        break;
    case SP_DRAIN_HP:
        if (0 == rnd(g, 3) && p->max_hp > 3) {
            p->max_hp--;
            p->hp = (int16_t)imin(p->hp, p->max_hp);
            rogue_message(g, "you feel weaker");
        }
        break;
    case SP_DRAIN_LEVEL:
        if (0 == rnd(g, 5) && p->xlevel > 1) {
            p->xlevel--;
            p->exp = rogue_exp_needed(p->xlevel - 1);
            p->max_hp = (int16_t)imax(3, p->max_hp - roll(g, 1, 10));
            p->hp = (int16_t)imin(p->hp, p->max_hp);
            rogue_message(g, "you suddenly feel weaker");
        }
        break;
    default:
        break;
    }
}

static bool scare_scroll_under_player(const Rogue *g)
{
    int i = item_at(g, g->player.x, g->player.y);
    return i >= 0 && IC_SCROLL == g->floor[i].cls
        && S_SCARE_MONSTER == g->floor[i].kind;
}

static void attack_player(Rogue *g, RogueMonster *m)
{
    const RogueMonsterDef *d = &rogue_monsters[m->type];
    int ac = rogue_armor_class(g);
    int dmg = 0;
    bool hit = false;
    for (int i = 0; i < 3; i++) {
        int n = d->dice[i][0], sides = d->dice[i][1];
        if (i > 0 && !n)
            break;
        if (!swing(g, d->level, ac, 0))
            continue;
        hit = true;
        if (n)
            dmg += roll(g, n, sides);
    }
    if (!hit) {
        rogue_message(g, "the %s misses", mon_name(g, m));
        return;
    }
    if (dmg)
        rogue_message(g, "the %s hits", mon_name(g, m));
    g->player.hp = (int16_t)(g->player.hp - dmg);
    if (g->player.hp <= 0) {
        die(g, m->type);
        return;
    }
    monster_special(g, m);
}

// Monsters' turn

static bool monster_can_enter(const Rogue *g, int x0, int y0, int x, int y)
{
    return passable(g, x, y) && monster_at(g, x, y) < 0
        && !(x == g->player.x && y == g->player.y) && diag_ok(g, x0, y0, x, y);
}

static void monster_step_random(Rogue *g, RogueMonster *m)
{
    int dx = rnd(g, 3) - 1, dy = rnd(g, 3) - 1;
    if (monster_can_enter(g, m->x, m->y, m->x + dx, m->y + dy)) {
        m->x = (int8_t)(m->x + dx);
        m->y = (int8_t)(m->y + dy);
    }
}

static void monster_act(Rogue *g, RogueMonster *m)
{
    const RogueMonsterDef *d = &rogue_monsters[m->type];
    RoguePlayer *p = &g->player;

    if (d->flags & MF_REGEN && m->hp < m->max_hp && 0 == rnd(g, 3))
        m->hp++;
    if (m->held) {
        m->held--;
        return;
    }
    if (!m->awake) {
        // Notices the player when it can see them.
        if (g->visible[m->y][m->x] && !m->disguise
            && 0 == rnd(g, (d->flags & MF_MEAN) ? 2 : 4))
            m->awake = true;
        return;
    }
    if (m->disguise)
        return;

    bool adjacent = iabs(m->x - p->x) <= 1 && iabs(m->y - p->y) <= 1;
    if ((d->flags & MF_FLITS) && 0 == rnd(g, 2)) {
        monster_step_random(g, m);
        return;
    }
    if (adjacent && diag_ok(g, m->x, m->y, p->x, p->y)) {
        if (!scare_scroll_under_player(g))
            attack_player(g, m);
        return;
    }
    if (d->flags & MF_STILL)
        return;

    // Follow the shortest path, or wander when the player is out of reach.
    int here = g->dist[m->y][m->x];
    if (0xFF == here) {
        if (0 == rnd(g, 3))
            monster_step_random(g, m);
        return;
    }
    int best = here, bx = 0, by = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = m->x + dx, ny = m->y + dy;
            if ((!dx && !dy) || !in_map(nx, ny) || g->dist[ny][nx] >= best)
                continue;
            if (!monster_can_enter(g, m->x, m->y, nx, ny))
                continue;
            best = g->dist[ny][nx];
            bx = dx;
            by = dy;
        }
    }
    m->x = (int8_t)(m->x + bx);
    m->y = (int8_t)(m->y + by);
}

static void wandering_monster(Rogue *g)
{
    if (0 != rnd(g, WANDER_CHANCE))
        return;
    for (int tries = 0; tries < 10; tries++) {
        int x, y;
        if (!free_spot(g, random_room(g), &x, &y) || g->visible[y][x])
            continue;
        spawn_monster(g, pick_monster_type(g, true), x, y, true);
        return;
    }
}

// End of a turn

static void tick_player(Rogue *g)
{
    RoguePlayer *p = &g->player;

    p->food--;
    if (FOOD_HUNGRY == p->food)
        rogue_message(g, "you are starting to get hungry");
    else if (FOOD_WEAK == p->food)
        rogue_message(g, "you are starting to feel weak");
    else if (p->food <= 0) {
        if (p->food <= FOOD_STARVE) {
            die(g, DEATH_STARVED);
            return;
        }
        if (!p->frozen && 0 == rnd(g, 5)) {
            p->frozen = (uint8_t)(rnd(g, 8) + 4);
            rogue_message(g, "you faint from lack of food");
        }
    }

    // Natural healing, faster at higher levels.
    int every = imax(3, 20 - p->xlevel * 2);
    if (p->hp < p->max_hp && 0 == g->turns % (uint32_t)every)
        p->hp++;

    if (p->confused && 0 == --p->confused)
        rogue_message(g, "you feel less confused now");
    if (p->blind && 0 == --p->blind)
        rogue_message(g, "the veil of darkness lifts");
    if (p->asleep && 0 == --p->asleep)
        rogue_message(g, "you wake up");
    if (p->frozen && 0 == --p->frozen)
        rogue_message(g, "you can move again");
    if (p->stuck)
        p->stuck--;
}

static void end_turn(Rogue *g)
{
    g->turns++;
    tick_player(g);
    if (RS_DEAD == g->status)
        return;
    update_view(g);
    update_distances(g);
    for (int i = 0; i < RG_MAX_MONSTERS && RS_DEAD != g->status; i++)
        if (g->monsters[i].active)
            monster_act(g, &g->monsters[i]);
    wandering_monster(g);
    update_view(g);
}

// The player can act: the game is on and they are not asleep or frozen.
static bool can_act(const Rogue *g)
{
    return RS_PLAYING == g->status && !rogue_helpless(g);
}

static bool valid_pack_index(const Rogue *g, int idx)
{
    return idx >= 0 && idx < g->pack_count;
}

static void cursed_message(Rogue *g)
{
    rogue_message(g, "you can't. It appears to be cursed");
}

static void begin_action(Rogue *g)
{
    g->msg_count = 0;
    g->detect_monsters = false;
}

// Moving, traps and picking things up

static void teleport(Rogue *g)
{
    place_player(g);
    g->player.held = false;
    g->player.stuck = 0;
}

static void change_level(Rogue *g, int level)
{
    g->level = (uint8_t)level;
    new_level(g);
}

static void spring_trap(Rogue *g)
{
    RoguePlayer *p = &g->player;
    RogueCell *c = &g->map[p->y][p->x];
    c->flags |= CELL_TRAP_KNOWN;
    switch (c->trap) {
    case TRAP_DOOR:
        rogue_message(g, "you fell through a trap door!");
        change_level(g, g->level + 1);
        break;
    case TRAP_TELEPORT:
        teleport(g);
        rogue_message(g, "you feel a wrenching sensation");
        break;
    case TRAP_SLEEP:
        p->asleep = (uint8_t)(p->asleep + rnd(g, 5) + 2);
        rogue_message(g, "a strange white mist envelops you and you fall asleep");
        break;
    case TRAP_ARROW:
        if (swing(g, g->level - 1, rogue_armor_class(g), 1)) {
            p->hp = (int16_t)(p->hp - roll(g, 1, 6));
            rogue_message(g, "oh no! An arrow shot you");
            if (p->hp <= 0)
                die(g, DEATH_ARROW);
        } else {
            rogue_message(g, "an arrow shoots past you");
        }
        break;
    case TRAP_BEAR:
        p->stuck = (uint8_t)(rnd(g, 4) + 4);
        rogue_message(g, "you are caught in a bear trap");
        break;
    case TRAP_RUST:
        rogue_message(g, "a gush of water hits you on the head");
        rust_armor(g);
        break;
    default:
        break;
    }
}

static void pick_up(Rogue *g)
{
    int i = item_at(g, g->player.x, g->player.y);
    if (i < 0)
        return;
    RogueItem *it = &g->floor[i];
    char name[RG_MSG_LEN];
    if (IC_GOLD == it->cls) {
        g->player.gold += it->gold;
        rogue_message(g, "you found %u gold pieces", (unsigned)it->gold);
        it->cls = IC_NONE;
        return;
    }
    if (IC_SCROLL == it->cls && S_SCARE_MONSTER == it->kind
        && (it->flags & IF_DROPPED)) {
        rogue_message(g, "the scroll turns to dust as you pick it up");
        it->cls = IC_NONE;
        return;
    }
    int idx = pack_add(g, it);
    if (idx < 0) {
        rogue_item_name(g, it, name, sizeof(name));
        rogue_message(g, "there's no room for %s", name);
        return;
    }
    if (IC_AMULET == it->cls) {
        g->player.has_amulet = true;
        rogue_message(g, "you have the Amulet of Yendor! Now go up!");
    }
    it->cls = IC_NONE;
    rogue_item_name(g, &g->pack[idx], name, sizeof(name));
    rogue_message(g, "%c) %s", 'a' + idx, name);
}

bool rogue_move(Rogue *g, int dx, int dy)
{
    if (!can_act(g))
        return false;
    begin_action(g);
    RoguePlayer *p = &g->player;
    if (p->confused && 0 != rnd(g, 5)) {
        do {
            dx = rnd(g, 3) - 1;
            dy = rnd(g, 3) - 1;
        } while (!dx && !dy);
    }
    int nx = p->x + dx, ny = p->y + dy;
    if (!in_map(nx, ny))
        return false;

    int mi = monster_at(g, nx, ny);
    if (mi >= 0 && diag_ok(g, p->x, p->y, nx, ny)) {
        attack_monster(g, &g->monsters[mi]);
        end_turn(g);
        return true;
    }
    if (!passable(g, nx, ny) || !diag_ok(g, p->x, p->y, nx, ny))
        return false;   // walking into a wall costs nothing
    if (p->held) {
        rogue_message(g, "you are being held");
        end_turn(g);
        return true;
    }
    if (p->stuck) {
        rogue_message(g, "you are still stuck in the bear trap");
        end_turn(g);
        return true;
    }
    p->x = (int8_t)nx;
    p->y = (int8_t)ny;
    update_view(g);
    if (g->map[ny][nx].trap)
        spring_trap(g);
    pick_up(g);
    if (RS_PLAYING == g->status)
        end_turn(g);
    return true;
}

bool rogue_search(Rogue *g)
{
    if (!can_act(g))
        return false;
    begin_action(g);
    RoguePlayer *p = &g->player;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = p->x + dx, y = p->y + dy;
            if (!in_map(x, y) || !g->map[y][x].trap
                || (g->map[y][x].flags & CELL_TRAP_KNOWN))
                continue;
            if (0 == rnd(g, 3)) {
                g->map[y][x].flags |= CELL_TRAP_KNOWN;
                rogue_message(g, "you found a trap");
            }
        }
    }
    end_turn(g);
    return true;
}

bool rogue_stairs(Rogue *g)
{
    if (!can_act(g))
        return false;
    begin_action(g);
    RoguePlayer *p = &g->player;
    if (RT_STAIRS != g->map[p->y][p->x].tile) {
        rogue_message(g, p->has_amulet ? "I see no way up" : "I see no way down");
        return false;
    }
    if (p->has_amulet) {
        if (g->level <= 1) {
            g->level = 0;
            g->status = RS_WON;
            return true;
        }
        change_level(g, g->level - 1);
        rogue_message(g, "you climb up to level %d", g->level);
    } else {
        change_level(g, g->level + 1);
    }
    return true;
}

bool rogue_helpless(const Rogue *g)
{
    return g->player.frozen > 0 || g->player.asleep > 0;
}

void rogue_wait(Rogue *g)
{
    if (RS_PLAYING != g->status)
        return;
    begin_action(g);
    end_turn(g);
}

void rogue_quit(Rogue *g)
{
    die(g, DEATH_QUIT);
}

// Using things

static void heal(Rogue *g, int amount, int max_gain)
{
    RoguePlayer *p = &g->player;
    p->hp = (int16_t)(p->hp + amount);
    if (p->hp > p->max_hp) {
        p->max_hp = (int16_t)(p->max_hp + max_gain);
        p->hp = p->max_hp;
    }
}

static void quaff(Rogue *g, int kind)
{
    RoguePlayer *p = &g->player;
    g->potion_known[kind] = true;
    switch (kind) {
    case P_HEALING:
        heal(g, roll(g, p->xlevel, 4), 1);
        p->blind = 0;
        rogue_message(g, "you begin to feel better");
        break;
    case P_EXTRA_HEALING:
        heal(g, roll(g, p->xlevel, 8), 2);
        p->blind = 0;
        rogue_message(g, "you begin to feel much better");
        break;
    case P_GAIN_STRENGTH:
        p->str++;
        if (p->str > p->max_str)
            p->max_str = p->str;
        rogue_message(g, "you feel stronger, now. What bulging muscles!");
        break;
    case P_RESTORE_STRENGTH:
        p->str = p->max_str;
        rogue_message(g, "hey, this tastes great. It make you feel warm all over");
        break;
    case P_RAISE_LEVEL:
        p->exp = rogue_exp_needed(p->xlevel);
        rogue_message(g, "you suddenly feel much more skillful");
        check_level(g);
        break;
    case P_SEE_INVISIBLE:
        p->see_invisible = true;
        rogue_message(g, "this potion tastes like slime-mold juice");
        break;
    case P_DETECT_MONSTERS: {
        bool any = false;
        for (int i = 0; i < RG_MAX_MONSTERS; i++)
            any |= g->monsters[i].active;
        if (any) {
            g->detect_monsters = true;
            rogue_message(g, "you sense the presence of monsters");
        } else {
            rogue_message(g, "you have a strange feeling for a moment, then it passes");
        }
        break;
    }
    case P_CONFUSION:
        p->confused = (uint8_t)(p->confused + rnd(g, 8) + 8);
        rogue_message(g, "wait, what's going on here. Huh? What? Who?");
        break;
    case P_BLINDNESS:
        p->blind = (uint8_t)imin(250, p->blind + rnd(g, 30) + 30);
        rogue_message(g, "oh, bummer! Everything is dark! Help!");
        break;
    case P_SLEEP:
        p->asleep = (uint8_t)(p->asleep + rnd(g, 5) + 4);
        rogue_message(g, "you fall asleep");
        break;
    case P_POISON:
        p->str = (int8_t)imax(3, p->str - (rnd(g, 3) + 1));
        rogue_message(g, "you feel very sick now");
        break;
    default:
        break;
    }
}

static bool any_unknown(const Rogue *g)
{
    for (int i = 0; i < g->pack_count; i++) {
        const RogueItem *it = &g->pack[i];
        if ((IC_POTION == it->cls && !g->potion_known[it->kind])
            || (IC_SCROLL == it->cls && !g->scroll_known[it->kind])
            || ((IC_WEAPON == it->cls || IC_ARMOR == it->cls)
                && !(it->flags & IF_KNOWN)))
            return true;
    }
    return false;
}

static void read_scroll(Rogue *g, int kind)
{
    RoguePlayer *p = &g->player;
    g->scroll_known[kind] = true;
    switch (kind) {
    case S_IDENTIFY:
        rogue_message(g, "this is a scroll of identify");
        if (any_unknown(g))
            g->status = RS_IDENTIFY;
        break;
    case S_MAGIC_MAPPING:
        for (int y = 0; y < RG_H; y++)
            for (int x = 0; x < RG_W; x++)
                if (RT_ROCK != g->map[y][x].tile)
                    g->map[y][x].flags |= CELL_SEEN;
        rogue_message(g, "oh, now this scroll has a map on it");
        break;
    case S_TELEPORT:
        teleport(g);
        break;
    case S_ENCHANT_ARMOR:
        if (p->armor >= 0) {
            g->pack[p->armor].hit++;
            g->pack[p->armor].flags &= (uint8_t)~IF_CURSED;
            rogue_message(g, "your armor glows silver for a moment");
        } else {
            rogue_message(g, "you feel a strange sense of loss");
        }
        break;
    case S_ENCHANT_WEAPON:
        if (p->weapon >= 0) {
            RogueItem *w = &g->pack[p->weapon];
            if (rnd(g, 2))
                w->hit++;
            else
                w->dmg++;
            w->flags &= (uint8_t)~IF_CURSED;
            rogue_message(g, "your %s glows blue for a moment",
                          weapons[w->kind].name);
        } else {
            rogue_message(g, "you feel a strange sense of loss");
        }
        break;
    case S_HOLD_MONSTER: {
        bool any = false;
        for (int i = 0; i < RG_MAX_MONSTERS; i++) {
            RogueMonster *m = &g->monsters[i];
            if (m->active && iabs(m->x - p->x) <= 2 && iabs(m->y - p->y) <= 2) {
                m->held = (uint8_t)(rnd(g, 10) + 10);
                any = true;
            }
        }
        rogue_message(g, any ? "the monsters around you freeze"
                             : "you feel a strange sense of loss");
        break;
    }
    case S_SLEEP:
        p->asleep = (uint8_t)(p->asleep + rnd(g, 5) + 4);
        rogue_message(g, "you fall asleep");
        break;
    case S_CREATE_MONSTER: {
        for (int tries = 0; tries < 16; tries++) {
            int x = p->x + rnd(g, 3) - 1, y = p->y + rnd(g, 3) - 1;
            if (monster_can_enter(g, x, y, x, y)) {
                spawn_monster(g, pick_monster_type(g, true), x, y, true);
                return;
            }
        }
        rogue_message(g, "you hear a faint cry of anguish in the distance");
        break;
    }
    case S_AGGRAVATE:
        for (int i = 0; i < RG_MAX_MONSTERS; i++)
            g->monsters[i].awake = true;
        rogue_message(g, "you hear a high pitched humming noise");
        break;
    case S_SCARE_MONSTER:
        rogue_message(g, "you hear maniacal laughter in the distance");
        break;
    case S_REMOVE_CURSE:
        if (p->weapon >= 0)
            g->pack[p->weapon].flags &= (uint8_t)~IF_CURSED;
        if (p->armor >= 0)
            g->pack[p->armor].flags &= (uint8_t)~IF_CURSED;
        rogue_message(g, "you feel as if somebody is watching over you");
        break;
    default:
        break;
    }
}

bool rogue_use(Rogue *g, int idx)
{
    if (!can_act(g) || !valid_pack_index(g, idx))
        return false;
    begin_action(g);
    RoguePlayer *p = &g->player;
    RogueItem *it = &g->pack[idx];
    char name[RG_MSG_LEN];
    switch (it->cls) {
    case IC_POTION: {
        int kind = it->kind;
        pack_take(g, idx, false);
        quaff(g, kind);
        break;
    }
    case IC_SCROLL: {
        int kind = it->kind;
        pack_take(g, idx, false);
        read_scroll(g, kind);
        break;
    }
    case IC_FOOD: {
        bool fruit = FOOD_FRUIT == it->kind;
        pack_take(g, idx, false);
        p->food = (int16_t)imin(FOOD_MAX, imax(p->food, 0) + FOOD_START);
        rogue_message(g, fruit ? "my, that was a yummy slime-mold"
                               : "yum, that tasted good");
        break;
    }
    case IC_WEAPON:
        if (idx == p->weapon)
            return false;
        if (p->weapon >= 0 && (g->pack[p->weapon].flags & IF_CURSED)) {
            cursed_message(g);
            return false;
        }
        p->weapon = (int8_t)idx;
        it->flags |= IF_KNOWN;
        rogue_item_name(g, it, name, sizeof(name));
        rogue_message(g, "you are now wielding %s", name);
        break;
    case IC_ARMOR:
        if (p->armor >= 0 && (g->pack[p->armor].flags & IF_CURSED)) {
            cursed_message(g);
            return false;
        }
        if (idx == p->armor) {
            p->armor = -1;
            rogue_message(g, "you used to be wearing %s",
                          armors[it->kind].name);
        } else {
            p->armor = (int8_t)idx;
            it->flags |= IF_KNOWN;
            rogue_item_name(g, it, name, sizeof(name));
            rogue_message(g, "you are now wearing %s", name);
        }
        break;
    default:
        return false;
    }
    if (RS_PLAYING == g->status || RS_IDENTIFY == g->status)
        end_turn(g);
    return true;
}

bool rogue_drop(Rogue *g, int idx)
{
    if (!can_act(g) || !valid_pack_index(g, idx))
        return false;
    begin_action(g);
    RoguePlayer *p = &g->player;
    if (item_at(g, p->x, p->y) >= 0 || RT_STAIRS == g->map[p->y][p->x].tile) {
        rogue_message(g, "there is something there already");
        return false;
    }
    if ((idx == p->weapon || idx == p->armor)
        && (g->pack[idx].flags & IF_CURSED)) {
        cursed_message(g);
        return false;
    }
    RogueItem it = g->pack[idx];
    it.count = 1;
    if (IC_SCROLL == it.cls && S_SCARE_MONSTER == it.kind)
        it.flags |= IF_DROPPED;
    if (IC_AMULET == it.cls)
        p->has_amulet = false;
    char name[RG_MSG_LEN];
    rogue_item_name(g, &it, name, sizeof(name));
    pack_take(g, idx, false);
    floor_add(g, &it, p->x, p->y);
    rogue_message(g, "dropped %s", name);
    end_turn(g);
    return true;
}

bool rogue_identify(Rogue *g, int idx)
{
    if (RS_IDENTIFY != g->status || !valid_pack_index(g, idx))
        return false;
    RogueItem *it = &g->pack[idx];
    switch (it->cls) {
    case IC_POTION: g->potion_known[it->kind] = true; break;
    case IC_SCROLL: g->scroll_known[it->kind] = true; break;
    case IC_WEAPON:
    case IC_ARMOR:  it->flags |= IF_KNOWN; break;
    default: break;
    }
    char name[RG_MSG_LEN];
    rogue_item_name(g, it, name, sizeof(name));
    g->msg_count = 0;
    rogue_message(g, "%c) %s", 'a' + idx, name);
    g->status = RS_PLAYING;
    return true;
}

// Display helpers

bool rogue_run_should_stop(const Rogue *g)
{
    const RoguePlayer *p = &g->player;
    if (RS_PLAYING != g->status || g->msg_count > 0)
        return true;
    for (int i = 0; i < RG_MAX_MONSTERS; i++) {
        const RogueMonster *m = &g->monsters[i];
        if (m->active && g->visible[m->y][m->x] && !m->disguise
            && !((rogue_monsters[m->type].flags & MF_INVIS)
                 && !p->see_invisible))
            return true;
    }
    int tile = g->map[p->y][p->x].tile;
    if (RT_DOOR == tile || RT_STAIRS == tile)
        return true;
    // Something lying next to the path.
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (in_map(p->x + dx, p->y + dy)
                && (item_at(g, p->x + dx, p->y + dy) >= 0
                    || RT_STAIRS == g->map[p->y + dy][p->x + dx].tile))
                return true;
    // Corridor forks.
    if (RT_CORRIDOR == tile) {
        int ways = 0;
        static const int8_t d4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int i = 0; i < 4; i++)
            ways += passable(g, p->x + d4[i][0], p->y + d4[i][1]);
        if (ways > 2)
            return true;
    }
    return false;
}

void rogue_glyph(const Rogue *g, int x, int y, char *ch, RogueColor *color)
{
    const RoguePlayer *p = &g->player;
    const RogueCell *c = &g->map[y][x];
    if (x == p->x && y == p->y) {
        *ch = '@';
        *color = RC_GOLD;
        return;
    }
    int mi = monster_at(g, x, y);
    if (mi >= 0) {
        const RogueMonster *m = &g->monsters[mi];
        bool invisible = (rogue_monsters[m->type].flags & MF_INVIS)
                      && !p->see_invisible;
        if (g->detect_monsters || (g->visible[y][x] && !invisible && !p->blind)) {
            if (m->disguise && !g->detect_monsters) {
                *ch = m->disguise;
                *color = RC_CYAN;
            } else {
                *ch = rogue_monsters[m->type].glyph;
                *color = RC_RED;
            }
            return;
        }
    }
    if (!(c->flags & CELL_SEEN)) {
        *ch = ' ';
        *color = RC_NORMAL;
        return;
    }
    int ii = item_at(g, x, y);
    if (ii >= 0) {
        int cls = g->floor[ii].cls;
        *ch = item_glyph(cls);
        *color = IC_GOLD == cls || IC_AMULET == cls ? RC_GOLD
               : IC_FOOD == cls ? RC_GREEN : RC_CYAN;
        return;
    }
    if (c->trap && (c->flags & CELL_TRAP_KNOWN)) {
        *ch = '^';
        *color = RC_RED;
        return;
    }
    *color = RC_DIM;
    switch (c->tile) {
    case RT_HWALL:    *ch = '-'; *color = RC_NORMAL; break;
    case RT_VWALL:    *ch = '|'; *color = RC_NORMAL; break;
    case RT_DOOR:     *ch = '+'; *color = RC_NORMAL; break;
    case RT_CORRIDOR: *ch = '#'; break;
    case RT_STAIRS:   *ch = '%'; *color = RC_GREEN; break;
    case RT_FLOOR: {
        // Dark rooms only show the floor right next to the player.
        bool dark = NO_ROOM != c->room && g->rooms[c->room].dark;
        *ch = dark && !g->visible[y][x] ? ' ' : '.';
        break;
    }
    default:          *ch = ' '; break;
    }
}

const char *rogue_hunger_name(const Rogue *g)
{
    int f = g->player.food;
    if (f <= 0)
        return "Faint";
    if (f < FOOD_WEAK)
        return "Weak";
    if (f < FOOD_HUNGRY)
        return "Hungry";
    return "";
}

void rogue_cause_text(uint8_t cause, char *out, int size)
{
    if (cause < 26) {
        const char *name = rogue_monsters[cause].name;
        snprintf(out, (size_t)size, "killed by %s %s", article(name), name);
        return;
    }
    switch (cause) {
    case DEATH_STARVED: snprintf(out, (size_t)size, "starved"); break;
    case DEATH_ARROW:   snprintf(out, (size_t)size, "killed by an arrow"); break;
    case DEATH_POISON:  snprintf(out, (size_t)size, "killed by a dart"); break;
    case DEATH_QUIT:    snprintf(out, (size_t)size, "quit"); break;
    case DEATH_WON:     snprintf(out, (size_t)size, "escaped with the Amulet"); break;
    default:            snprintf(out, (size_t)size, "died"); break;
    }
}

uint32_t rogue_final_score(const Rogue *g)
{
    uint32_t gold = g->player.gold;
    return RS_WON == g->status ? gold : gold - gold / 10;
}

int rogue_board_add(RogueBoard *b, const Rogue *g)
{
    RogueScore s;
    memset(&s, 0, sizeof(s));
    s.score = rogue_final_score(g);
    s.level = g->max_level;
    s.cause = RS_WON == g->status ? DEATH_WON : g->cause;
    snprintf(s.name, sizeof(s.name), "%s", g->name);

    int rank = b->count;
    while (rank > 0 && b->entries[rank - 1].score < s.score)
        rank--;
    if (rank >= RG_SCORES)
        return -1;
    int last = imin(b->count, RG_SCORES - 1);
    for (int i = last; i > rank; i--)
        b->entries[i] = b->entries[i - 1];
    b->entries[rank] = s;
    if (b->count < RG_SCORES)
        b->count++;
    return rank;
}
