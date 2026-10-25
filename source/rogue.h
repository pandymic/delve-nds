// Rogue: the Easter egg. A small tribute to Rogue (1980) by Michael Toy,
// Glenn Wichman and Ken Arnold, opened by tapping the version number on
// the About screen.
//
// Pure logic (no libnds): levels of nine rooms on a 3x3 grid joined by
// corridors, the A-Z monsters, potions and scrolls with unknown names,
// hunger, traps, and the Amulet of Yendor on level 26. Turn based: every
// player action is one turn, then each monster acts. states/rogue.c draws
// it and maps the DS buttons to these calls.

#ifndef DELVE_ROGUE_H
#define DELVE_ROGUE_H

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"

#define RG_W            32    // map columns (one screen of text)
#define RG_H            22    // map rows
#define RG_ROOMS        9     // 3 x 3 grid
#define RG_MAX_MONSTERS 24
#define RG_MAX_ITEMS    32    // lying on the floor
#define RG_PACK         17    // pack letters a-q
#define RG_MSGS         8     // messages per turn
#define RG_MSG_LEN      64
#define RG_HISTORY      12    // previous messages (L)
#define RG_NAME_LEN     11
#define RG_AMULET_LEVEL 26
#define RG_SCORES       5     // "Top Rogueists"

// What a map cell is. Rock is solid and blank.
typedef enum {
    RT_ROCK,
    RT_HWALL,       // '-'
    RT_VWALL,       // '|'
    RT_FLOOR,       // '.'
    RT_DOOR,        // '+'
    RT_CORRIDOR,    // '#'
    RT_STAIRS,      // '%'
} RogueTile;

typedef enum {
    TRAP_NONE,
    TRAP_DOOR,      // drops to the next level
    TRAP_TELEPORT,
    TRAP_SLEEP,
    TRAP_ARROW,
    TRAP_BEAR,
    TRAP_RUST,
    TRAP_KINDS
} RogueTrap;

enum {
    CELL_SEEN   = 1 << 0,   // remembered on the map
    CELL_TRAP_KNOWN = 1 << 1,
};

typedef struct {
    uint8_t tile;       // RogueTile
    uint8_t room;       // room index, 0xFF outside rooms
    uint8_t trap;       // RogueTrap
    uint8_t flags;
} RogueCell;

typedef struct {
    int8_t x, y, w, h;  // including walls; a gone room is a 1x1 junction
    bool gone;
    bool dark;
} RogueRoom;

typedef enum {
    IC_NONE,
    IC_GOLD,
    IC_FOOD,
    IC_POTION,
    IC_SCROLL,
    IC_WEAPON,
    IC_ARMOR,
    IC_AMULET,
} ItemClass;

enum { FOOD_RATION, FOOD_FRUIT };

typedef enum {
    P_HEALING, P_EXTRA_HEALING, P_GAIN_STRENGTH, P_RESTORE_STRENGTH,
    P_RAISE_LEVEL, P_SEE_INVISIBLE, P_DETECT_MONSTERS, P_CONFUSION,
    P_BLINDNESS, P_SLEEP, P_POISON,
    P_KINDS
} PotionKind;

typedef enum {
    S_IDENTIFY, S_MAGIC_MAPPING, S_TELEPORT, S_ENCHANT_ARMOR,
    S_ENCHANT_WEAPON, S_HOLD_MONSTER, S_SLEEP, S_CREATE_MONSTER,
    S_AGGRAVATE, S_SCARE_MONSTER, S_REMOVE_CURSE,
    S_KINDS
} ScrollKind;

typedef enum {
    W_MACE, W_LONG_SWORD, W_DAGGER, W_TWO_HANDED, W_SPEAR,
    W_KINDS
} WeaponKind;

typedef enum {
    A_LEATHER, A_RING, A_STUDDED, A_SCALE, A_CHAIN, A_SPLINT, A_BANDED,
    A_PLATE,
    A_KINDS
} ArmorKind;

enum {
    IF_KNOWN  = 1 << 0,   // enchantment known (weapons, armour)
    IF_CURSED = 1 << 1,
    IF_DROPPED = 1 << 2,  // scare monster: dropped once, crumbles if lifted
};

typedef struct {
    uint8_t cls;        // ItemClass
    uint8_t kind;
    uint8_t count;      // stacks of potions, scrolls and food
    uint8_t flags;
    int8_t hit, dmg;    // weapons: +hit,+dmg; armour: hit = protection bonus
    uint16_t gold;      // IC_GOLD amount
    int8_t x, y;        // on the floor
} RogueItem;

// Monster special abilities.
typedef enum {
    SP_NONE, SP_RUST, SP_HOLD, SP_FREEZE, SP_STEAL_GOLD, SP_STEAL_ITEM,
    SP_CONFUSE, SP_WEAKEN, SP_DRAIN_HP, SP_DRAIN_LEVEL,
} RogueSpecial;

enum {
    MF_MEAN   = 1 << 0,   // often awake and hunting
    MF_FLITS  = 1 << 1,   // moves erratically (bats)
    MF_INVIS  = 1 << 2,
    MF_STILL  = 1 << 3,   // never moves (Venus flytrap)
    MF_REGEN  = 1 << 4,
    MF_MIMIC  = 1 << 5,   // looks like an item until disturbed (xeroc)
};

typedef struct {
    char glyph;
    const char *name;
    uint8_t level;
    int8_t armor;       // armour class, lower is better
    uint16_t exp;
    uint8_t dice[3][2]; // up to three attacks: count, sides (0 = none)
    uint8_t special;    // RogueSpecial
    uint8_t flags;      // MF_*
} RogueMonsterDef;

extern const RogueMonsterDef rogue_monsters[26];

typedef struct {
    bool active;
    uint8_t type;       // index into rogue_monsters ('A' + type)
    int8_t x, y;
    int16_t hp, max_hp;
    bool awake;
    uint8_t held;       // turns frozen by a hold monster scroll
    char disguise;      // xeroc: item glyph shown, 0 once revealed
} RogueMonster;

// How a game ended. Monster deaths use the monster index (0-25).
enum {
    DEATH_STARVED = 26,
    DEATH_ARROW,
    DEATH_POISON,
    DEATH_QUIT,
    DEATH_WON,
};

typedef struct {
    uint32_t score;
    uint8_t level;      // deepest dungeon level
    uint8_t cause;      // monster index or DEATH_*
    char name[RG_NAME_LEN];
} RogueScore;

typedef struct {
    RogueScore entries[RG_SCORES];
    uint8_t count;
} RogueBoard;

typedef struct {
    int8_t x, y;
    int16_t hp, max_hp;
    int8_t str, max_str;
    uint8_t xlevel;     // experience level
    uint32_t exp;
    uint32_t gold;
    int16_t food;       // turns until hungry trouble
    int8_t weapon, armor;   // pack index, -1 none
    uint8_t confused, blind, frozen, asleep, stuck;  // turns left
    bool held;          // Venus flytrap
    bool see_invisible;
    bool has_amulet;
} RoguePlayer;

typedef enum {
    RS_PLAYING,
    RS_IDENTIFY,        // choosing an item to identify
    RS_DEAD,
    RS_WON,
} RogueStatus;

typedef struct {
    Rng rng;
    RogueStatus status;
    uint8_t level;      // dungeon level, 1-26 (0 = escaped)
    uint8_t max_level;
    uint8_t cause;      // valid when dead
    uint32_t turns;
    char name[RG_NAME_LEN];

    RogueCell map[RG_H][RG_W];
    RogueRoom rooms[RG_ROOMS];
    RogueMonster monsters[RG_MAX_MONSTERS];
    RogueItem floor[RG_MAX_ITEMS];
    RogueItem pack[RG_PACK];
    uint8_t pack_count;
    RoguePlayer player;
    int levels_without_food;

    // Names: unknown potions by colour, scrolls by title, until known.
    uint8_t potion_color[P_KINDS];
    char scroll_title[S_KINDS][16];
    bool potion_known[P_KINDS];
    bool scroll_known[S_KINDS];

    // Per-turn view.
    bool visible[RG_H][RG_W];
    bool detect_monsters;   // potion: show every monster this turn
    uint8_t dist[RG_H][RG_W];   // path distance from the player (255 far)

    char msgs[RG_MSGS][RG_MSG_LEN];
    uint8_t msg_count;
    char history[RG_HISTORY][RG_MSG_LEN];
    uint8_t history_head;
} Rogue;

// Display colours (the DS maps them to its text palette).
typedef enum {
    RC_NORMAL, RC_DIM, RC_GOLD, RC_RED, RC_GREEN, RC_CYAN,
} RogueColor;

// A new game on level 1 with the starting kit; name is the player's.
void rogue_new(Rogue *g, uint32_t seed, const char *name);

// Player actions. Each returns true if it took a turn (the monsters then
// moved). Messages for the turn are in g->msgs.
bool rogue_move(Rogue *g, int dx, int dy);
bool rogue_search(Rogue *g);            // rest a turn, look for traps
bool rogue_stairs(Rogue *g);            // down, or up with the Amulet
bool rogue_use(Rogue *g, int pack_index);   // quaff, read, eat, wield, wear
bool rogue_drop(Rogue *g, int pack_index);      // one item, onto the floor
bool rogue_identify(Rogue *g, int pack_index);  // after a scroll (RS_IDENTIFY)
void rogue_quit(Rogue *g);                      // ends the game (DEATH_QUIT)

// While asleep or frozen the player cannot act: call this to pass a turn.
bool rogue_helpless(const Rogue *g);
void rogue_wait(Rogue *g);

// Running stops when something worth a look is next to the player.
bool rogue_run_should_stop(const Rogue *g);

// What to draw at a map cell.
void rogue_glyph(const Rogue *g, int x, int y, char *ch, RogueColor *color);

// Pack item description, e.g. "2 scrolls titled 'zok mif'" or
// "+1,+1 mace (weapon in hand)".
void rogue_item_name(const Rogue *g, const RogueItem *it, char *out, int size);

// What rogue_use() does to an item: "quaff", "read", "eat", "wield",
// "wear", "take off", or NULL if it cannot be used.
const char *rogue_use_verb(const Rogue *g, int pack_index);

// Status line details.
const char *rogue_hunger_name(const Rogue *g);   // "" when fed
int rogue_armor_class(const Rogue *g);           // 10 bare; lower is better
uint32_t rogue_exp_needed(int xlevel);           // to reach the next level

// Death or victory text: "killed by a bat", "starved", "quit".
void rogue_cause_text(uint8_t cause, char *out, int size);

// Adds a message to this turn's lines and the history (L).
void rogue_message(Rogue *g, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Final score of a finished game (gold; winners keep it all, the dead lose
// a tenth, as in the original).
uint32_t rogue_final_score(const Rogue *g);

// Inserts a finished game into a board. Returns its rank (0-based) or -1.
int rogue_board_add(RogueBoard *b, const Rogue *g);

#endif // DELVE_ROGUE_H
