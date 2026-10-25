// Rogue, the Easter egg: generation, turns, items and scores.

#include <string.h>

#include "test.h"
#include "rogue.h"

static bool walkable(const Rogue *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= RG_W || y >= RG_H)
        return false;
    int t = g->map[y][x].tile;
    return RT_FLOOR == t || RT_DOOR == t || RT_CORRIDOR == t || RT_STAIRS == t;
}

// Every walkable cell reachable from the player (4-connected flood fill).
static bool level_connected(const Rogue *g)
{
    static bool seen[RG_H][RG_W];
    static int8_t stack[RG_W * RG_H][2];
    memset(seen, 0, sizeof(seen));
    int top = 0;
    stack[top][0] = g->player.x;
    stack[top][1] = g->player.y;
    top++;
    seen[g->player.y][g->player.x] = true;
    while (top) {
        top--;
        int x = stack[top][0], y = stack[top][1];
        static const int8_t d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int i = 0; i < 4; i++) {
            int nx = x + d[i][0], ny = y + d[i][1];
            if (walkable(g, nx, ny) && !seen[ny][nx]) {
                seen[ny][nx] = true;
                stack[top][0] = (int8_t)nx;
                stack[top][1] = (int8_t)ny;
                top++;
            }
        }
    }
    for (int y = 0; y < RG_H; y++)
        for (int x = 0; x < RG_W; x++)
            if (walkable(g, x, y) && !seen[y][x])
                return false;
    return true;
}

static void find_stairs(const Rogue *g, int *sx, int *sy)
{
    *sx = *sy = -1;
    for (int y = 0; y < RG_H; y++)
        for (int x = 0; x < RG_W; x++)
            if (RT_STAIRS == g->map[y][x].tile) {
                *sx = x;
                *sy = y;
            }
}

// Puts the player on the stairs (removing anything standing there).
static void goto_stairs(Rogue *g)
{
    int sx, sy;
    find_stairs(g, &sx, &sy);
    for (int i = 0; i < RG_MAX_MONSTERS; i++)
        if (g->monsters[i].active && g->monsters[i].x == sx
            && g->monsters[i].y == sy)
            g->monsters[i].active = false;
    g->player.x = (int8_t)sx;
    g->player.y = (int8_t)sy;
}

static void check_level(const Rogue *g)
{
    CHECK(level_connected(g));
    int stairs = 0;
    for (int y = 0; y < RG_H; y++)
        for (int x = 0; x < RG_W; x++)
            stairs += RT_STAIRS == g->map[y][x].tile;
    CHECK_EQ(stairs, 1);
    CHECK(walkable(g, g->player.x, g->player.y));
    for (int i = 0; i < RG_MAX_ITEMS; i++) {
        const RogueItem *it = &g->floor[i];
        if (IC_NONE == it->cls)
            continue;
        CHECK_EQ(g->map[it->y][it->x].tile, RT_FLOOR);
    }
    for (int i = 0; i < RG_MAX_MONSTERS; i++) {
        const RogueMonster *m = &g->monsters[i];
        if (!m->active)
            continue;
        CHECK(walkable(g, m->x, m->y));
        CHECK(!(m->x == g->player.x && m->y == g->player.y));
        CHECK(m->type < 26);
        for (int j = i + 1; j < RG_MAX_MONSTERS; j++)
            CHECK(!(g->monsters[j].active && g->monsters[j].x == m->x
                    && g->monsters[j].y == m->y));
    }
}

static void generation_tests(void)
{
    static Rogue g;
    int amulets = 0;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        rogue_new(&g, seed * 7919u, "Tester");
        CHECK_EQ(g.level, 1);
        CHECK_EQ(g.pack_count, 3);  // food, mace, ring mail
        CHECK(g.player.weapon >= 0 && g.player.armor >= 0);
        CHECK(1 == g.msg_count);
        for (int level = 1; level <= RG_AMULET_LEVEL; level++) {
            check_level(&g);
            if (RG_AMULET_LEVEL == level) {
                for (int i = 0; i < RG_MAX_ITEMS; i++)
                    amulets += IC_AMULET == g.floor[i].cls;
                break;
            }
            goto_stairs(&g);
            CHECK(rogue_stairs(&g));
            CHECK_EQ(g.level, level + 1);
        }
    }
    CHECK_EQ(amulets, 60);
}

static void table_tests(void)
{
    for (int i = 0; i < 26; i++) {
        CHECK_EQ(rogue_monsters[i].glyph, 'A' + i);
        CHECK(rogue_monsters[i].level >= 1);
    }
    for (int l = 1; l < 20; l++)
        CHECK(rogue_exp_needed(l + 1) > rogue_exp_needed(l));

    // Every item name fits a message.
    static Rogue g;
    rogue_new(&g, 99, "Tester");
    char name[RG_MSG_LEN];
    for (int known = 0; known < 2; known++) {
        for (int k = 0; k < P_KINDS; k++) {
            g.potion_known[k] = known;
            RogueItem it = { .cls = IC_POTION, .kind = (uint8_t)k, .count = 12 };
            rogue_item_name(&g, &it, name, sizeof(name));
            CHECK(strlen(name) > 5 && strlen(name) < RG_MSG_LEN - 1);
        }
        for (int k = 0; k < S_KINDS; k++) {
            g.scroll_known[k] = known;
            RogueItem it = { .cls = IC_SCROLL, .kind = (uint8_t)k, .count = 1 };
            rogue_item_name(&g, &it, name, sizeof(name));
            CHECK(strlen(name) > 5 && strlen(name) < RG_MSG_LEN - 1);
        }
    }
    RogueItem armor = { .cls = IC_ARMOR, .kind = A_PLATE, .count = 1,
                        .hit = 2, .flags = IF_KNOWN };
    rogue_item_name(&g, &armor, name, sizeof(name));
    CHECK(0 == strcmp(name, "+2 plate mail [1]"));
    RogueItem mace = { .cls = IC_WEAPON, .kind = W_MACE, .count = 1,
                       .hit = 1, .dmg = 1, .flags = IF_KNOWN };
    rogue_item_name(&g, &mace, name, sizeof(name));
    CHECK(0 == strcmp(name, "+1,+1 mace"));

    rogue_cause_text(4, name, sizeof(name));
    CHECK(0 == strcmp(name, "killed by an emu"));
    rogue_cause_text(1, name, sizeof(name));
    CHECK(0 == strcmp(name, "killed by a bat"));
    rogue_cause_text(DEATH_STARVED, name, sizeof(name));
    CHECK(0 == strcmp(name, "starved"));
}

static void invariants(const Rogue *g)
{
    const RoguePlayer *p = &g->player;
    if (RS_PLAYING == g->status || RS_IDENTIFY == g->status) {
        CHECK(walkable(g, p->x, p->y));
        CHECK(p->hp > 0 && p->hp <= p->max_hp);
    }
    CHECK(g->pack_count <= RG_PACK);
    CHECK(p->weapon < g->pack_count);
    CHECK(p->armor < g->pack_count);
    if (p->weapon >= 0)
        CHECK_EQ(g->pack[p->weapon].cls, IC_WEAPON);
    if (p->armor >= 0)
        CHECK_EQ(g->pack[p->armor].cls, IC_ARMOR);
    CHECK(p->str >= 3);
    CHECK(g->msg_count <= RG_MSGS);
    for (int i = 0; i < g->msg_count; i++)
        CHECK(NULL != memchr(g->msgs[i], 0, RG_MSG_LEN));
    for (int i = 0; i < RG_MAX_MONSTERS; i++) {
        const RogueMonster *m = &g->monsters[i];
        if (m->active) {
            CHECK(walkable(g, m->x, m->y));
            CHECK(!(m->x == p->x && m->y == p->y));
        }
    }
}

// Random play: moves, searches, stairs, using and dropping things.
static int play_random(Rogue *g, Rng *rng, int steps)
{
    int deaths = 0;
    for (int s = 0; s < steps && (RS_PLAYING == g->status
                                  || RS_IDENTIFY == g->status); s++) {
        if (RS_IDENTIFY == g->status) {
            CHECK(rogue_identify(g, (int)rng_range(rng, g->pack_count)));
        } else if (rogue_helpless(g)) {
            rogue_wait(g);
        } else {
            int r = (int)rng_range(rng, 100);
            if (r < 75)
                rogue_move(g, (int)rng_range(rng, 3) - 1,
                           (int)rng_range(rng, 3) - 1);
            else if (r < 82)
                rogue_search(g);
            else if (r < 88)
                rogue_stairs(g);
            else if (r < 97 && g->pack_count)
                rogue_use(g, (int)rng_range(rng, g->pack_count));
            else if (g->pack_count)
                rogue_drop(g, (int)rng_range(rng, g->pack_count));
        }
        invariants(g);
    }
    deaths += RS_DEAD == g->status;
    return deaths;
}

static void play_tests(void)
{
    static Rogue g;
    Rng rng;
    int deaths = 0, deep = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        rogue_new(&g, seed, "Bot");
        rng_seed(&rng, seed * 31u);
        // Every few games, dive to deeper levels first.
        for (int i = 0; i < (int)(seed % 12); i++) {
            goto_stairs(&g);
            rogue_stairs(&g);
        }
        deep += g.level > 8;
        deaths += play_random(&g, &rng, 3000);
        if (RS_DEAD == g.status) {
            char text[RG_MSG_LEN];
            rogue_cause_text(g.cause, text, sizeof(text));
            CHECK('\0' != text[0]);
        }
    }
    CHECK(deaths > 50);   // the dungeon is dangerous
    CHECK(deep > 50);

    // Same seed and inputs, same game.
    static Rogue a, b;
    Rng ra, rb;
    rogue_new(&a, 4242, "Bot");
    rogue_new(&b, 4242, "Bot");
    rng_seed(&ra, 5);
    rng_seed(&rb, 5);
    play_random(&a, &ra, 500);
    play_random(&b, &rb, 500);
    CHECK(0 == memcmp(a.map, b.map, sizeof(a.map)));
    CHECK(a.level == b.level && a.turns == b.turns && a.status == b.status);
    CHECK(a.player.x == b.player.x && a.player.y == b.player.y);
    CHECK(a.player.hp == b.player.hp && a.player.gold == b.player.gold);
    CHECK(a.pack_count == b.pack_count && a.player.exp == b.player.exp);
    for (int i = 0; i < RG_MAX_MONSTERS; i++)
        CHECK(a.monsters[i].active == b.monsters[i].active
              && a.monsters[i].x == b.monsters[i].x
              && a.monsters[i].y == b.monsters[i].y);
}

static void amulet_tests(void)
{
    static Rogue g;
    rogue_new(&g, 777, "Winner");
    while (g.level < RG_AMULET_LEVEL) {
        goto_stairs(&g);
        CHECK(rogue_stairs(&g));
    }
    // Walk onto the Amulet from a neighbouring cell.
    int ai = -1;
    for (int i = 0; i < RG_MAX_ITEMS; i++)
        if (IC_AMULET == g.floor[i].cls)
            ai = i;
    CHECK(ai >= 0);
    if (ai < 0)
        return;
    int ax = g.floor[ai].x, ay = g.floor[ai].y;
    for (int i = 0; i < RG_MAX_MONSTERS; i++)
        g.monsters[i].active = false;
    g.player.x = (int8_t)(ax - 1);
    g.player.y = (int8_t)ay;
    if (!walkable(&g, ax - 1, ay))
        g.player.x = (int8_t)(ax + 1);
    CHECK(rogue_move(&g, ax - g.player.x, 0));
    CHECK(g.player.has_amulet);

    // With the Amulet the stairs lead up, and out at level 1.
    g.player.hp = g.player.max_hp = 30000;
    int climbs = 0;
    while (RS_PLAYING == g.status && climbs < 40) {
        goto_stairs(&g);
        CHECK(rogue_stairs(&g));
        climbs++;
    }
    CHECK_EQ(g.status, RS_WON);
    CHECK_EQ(climbs, RG_AMULET_LEVEL);
    CHECK_EQ(g.max_level, RG_AMULET_LEVEL);
    CHECK_EQ(rogue_final_score(&g), g.player.gold);
}

static void survival_tests(void)
{
    static Rogue g;
    rogue_new(&g, 31337, "Hungry");
    for (int i = 0; i < RG_MAX_MONSTERS; i++)
        g.monsters[i].active = false;

    // Hunger stages and starvation.
    g.player.food = 301;
    rogue_search(&g);
    CHECK_EQ(g.player.food, 300);
    CHECK(g.msg_count >= 1);   // "you are starting to get hungry"
    CHECK(0 == strcmp(rogue_hunger_name(&g), ""));
    rogue_search(&g);
    CHECK(0 == strcmp(rogue_hunger_name(&g), "Hungry"));
    // Eating fixes it.
    int food = -1;
    for (int i = 0; i < g.pack_count; i++)
        if (IC_FOOD == g.pack[i].cls)
            food = i;
    CHECK(food >= 0);
    CHECK(rogue_use(&g, food));
    CHECK(g.player.food > 1000);
    g.player.food = -499;
    g.player.frozen = 0;
    rogue_wait(&g);
    CHECK_EQ(g.status, RS_DEAD);
    CHECK_EQ(g.cause, DEATH_STARVED);
    CHECK_EQ(rogue_final_score(&g), g.player.gold - g.player.gold / 10);

    // A dropped scare monster scroll keeps monsters from attacking.
    rogue_new(&g, 2024, "Scared");
    for (int i = 0; i < RG_MAX_MONSTERS; i++)
        g.monsters[i].active = false;
    RogueItem scroll = { .cls = IC_SCROLL, .kind = S_SCARE_MONSTER, .count = 1 };
    g.pack[g.pack_count++] = scroll;
    CHECK(rogue_drop(&g, g.pack_count - 1));
    RogueMonster *m = &g.monsters[0];
    memset(m, 0, sizeof(*m));
    int px = g.player.x, py = g.player.y;
    for (int dx = -1; dx <= 1 && !m->active; dx++)
        for (int dy = -1; dy <= 1 && !m->active; dy++)
            if ((dx || dy) && walkable(&g, px + dx, py + dy)
                && !(dx && dy)) {
                m->active = true;
                m->type = 'O' - 'A';
                m->x = (int8_t)(px + dx);
                m->y = (int8_t)(py + dy);
                m->hp = m->max_hp = 50;
                m->awake = true;
            }
    CHECK(m->active);
    g.player.food = 2000;
    int hp = g.player.hp;
    for (int i = 0; i < 30; i++)
        rogue_search(&g);
    CHECK(g.player.hp >= hp);
}

static void board_tests(void)
{
    static Rogue g;
    RogueBoard b;
    memset(&b, 0, sizeof(b));
    rogue_new(&g, 1, "Ann");
    g.status = RS_DEAD;
    g.cause = 1;
    static const uint32_t golds[7] = { 100, 500, 50, 1000, 10, 700, 600 };
    int ranks[7];
    for (int i = 0; i < 7; i++) {
        g.player.gold = golds[i];
        ranks[i] = rogue_board_add(&b, &g);
    }
    CHECK_EQ(b.count, RG_SCORES);
    CHECK_EQ(ranks[0], 0);
    CHECK_EQ(ranks[1], 0);
    CHECK_EQ(ranks[3], 0);
    CHECK_EQ(ranks[4], 4);    // the board was not full yet
    CHECK_EQ(b.entries[RG_SCORES - 1].score, 90);   // 10 and 50 pushed out
    for (int i = 1; i < b.count; i++)
        CHECK(b.entries[i - 1].score >= b.entries[i].score);
    CHECK_EQ(b.entries[0].score, 900);   // 1000 less a tenth
    CHECK(0 == strcmp(b.entries[0].name, "Ann"));
}

void run_rogue_tests(void)
{
    table_tests();
    generation_tests();
    play_tests();
    amulet_tests();
    survival_tests();
    board_tests();
}
