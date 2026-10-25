// Room shapes, pits, pathfinding and line of sight.

#include <string.h>

#include "test.h"
#include "roomgen.h"
#include "run.h"

static const PlayerInput idle = { .move = DIR_NONE };

static void empty_run(Run *run)
{
    run_start(run, 1);
    room_clear(&run->room);
    run_reset_room(run);
    run->player.x = run->player.safe_x = FX(6.0);
    run->player.z = run->player.safe_z = FX(6.0);
}

static void step_n(Run *run, const PlayerInput *in, int n)
{
    for (int i = 0; i < n; i++)
        run_update(run, in);
}

static bool door_ok(const Room *r, WallSide side, int pos)
{
    int cx, cz;
    door_cell(side, pos, &cx, &cz);
    fx nx, nz;
    side_inward(side, &nx, &nz);
    return room_is_open(r, cx, cz)
        && room_is_open(r, cx + FX_TO_INT(nx), cz + FX_TO_INT(nz));
}

static void generation_tests(void)
{
    static Room r;
    int with_void = 0, with_pit = 0, rooms = 0;
    for (uint32_t seed = 1; seed <= 4000; seed++) {
        int depth = 1 + (int)(seed % 30);
        RoomType type = (RoomType)(seed % RT_COUNT);
        WallSide entry = (WallSide)(seed % SIDE_COUNT);
        roomgen_generate(&r, seed * 2246822519u, depth, type, entry,
                         2 + (int)(seed % 8));
        rooms++;
        int voids = 0, pits = 0, open = 0;
        for (int z = 0; z < ROOM_SIZE; z++) {
            for (int x = 0; x < ROOM_SIZE; x++) {
                voids += CELL_VOID == room_cell(&r, x, z);
                pits += CELL_PIT == room_cell(&r, x, z);
                open += room_is_open(&r, x, z);
            }
        }
        with_void += voids > 0;
        with_pit += pits > 0;
        if (1 == depth)
            CHECK(0 == voids && 0 == pits);   // the first room is plain

        // Spawn and entry on open floor, everything reachable from there.
        int sx = FX_TO_INT(r.spawn_x), sz = FX_TO_INT(r.spawn_z);
        CHECK(room_is_open(&r, sx, sz));
        CHECK(room_connected(&r, sx, sz));
        int ex, ez;
        door_cell(r.entry_side, r.entry_pos, &ex, &ez);
        CHECK(room_is_open(&r, ex, ez));
        CHECK(open >= 50);   // obstacles take a few cells after shaping

        CHECK(r.door_count >= 1);
        for (int i = 0; i < r.door_count; i++)
            CHECK(door_ok(&r, r.doors[i].side, r.doors[i].pos));
        for (int i = 0; i < r.enemy_count; i++)
            CHECK(room_is_open(&r, FX_TO_INT(r.enemies[i].x),
                               FX_TO_INT(r.enemies[i].z)));
        for (int i = 0; i < r.decor_count; i++) {
            const Decor *d = &r.decor[i];
            int cx = FX_TO_INT(d->x), cz = FX_TO_INT(d->z);
            if (SIDE_NONE == d->wall) {
                CHECK(room_is_open(&r, cx, cz));
            } else {
                // Wall props hang where the room has floor along that wall.
                int wx, wz;
                int pos = SIDE_NX == d->wall ? cz : cx;
                door_cell(d->wall, pos, &wx, &wz);
                CHECK(room_is_open(&r, wx, wz)
                      || CELL_VOID != room_cell(&r, wx, wz));
            }
        }
    }
    // Shapes actually happen, but plenty of rooms keep their square.
    CHECK(with_void > rooms / 5);
    CHECK(with_pit > rooms / 5);
    CHECK(with_void + with_pit < rooms);
}

static void line_tests(void)
{
    static Room r;
    room_clear(&r);
    r.cells[5][5] = CELL_PILLAR;
    r.cells[8][2] = CELL_PIT;
    CHECK(!room_line_clear(&r, FX(5.5), FX(2.0), FX(5.5), FX(9.0)));
    CHECK(room_line_clear(&r, FX(2.5), FX(6.0), FX(2.5), FX(10.0)));  // over the pit
    CHECK(!room_line_walkable(&r, FX(2.5), FX(6.0), FX(2.5), FX(10.0)));
    CHECK(room_line_walkable(&r, FX(1.0), FX(1.0), FX(10.0), FX(1.0)));

    // Path map: around a wall of void with one gap.
    for (int z = 0; z < 10; z++)
        r.cells[z][6] = CELL_VOID;
    uint8_t dist[ROOM_SIZE][ROOM_SIZE];
    room_path_map(&r, 3, 2, dist);
    CHECK_EQ(dist[2][3], 0);
    CHECK_EQ(dist[2][6], 255);             // void
    CHECK(255 != dist[2][9]);
    CHECK(dist[2][9] > 9);                 // the long way round
    CHECK_EQ(dist[8][2], 255);             // pit
}

static void fall_tests(void)
{
    static Run run;
    empty_run(&run);
    Enemy *keep = run_spawn_enemy(&run, ENEMY_CHASER, FX(0.5), FX(11.5));
    keep->state = EST_RECOVER;
    keep->timer = 100000;
    run.room.cells[6][8] = CELL_PIT;
    Player *p = &run.player;
    p->z = FX(6.5);

    // Walk east (+x) into the pit: a heart lost, back on the edge.
    PlayerInput east = { .move = DIR_SE };
    bool fell = false;
    for (int i = 0; i < 60 && !fell; i++) {
        run_update(&run, &east);
        fell = 0 != (run.events & EV_PLAYER_FELL);
    }
    CHECK(fell);
    CHECK_EQ(p->hp, PLAYER_START_HP - PIT_DAMAGE);
    CHECK(!room_over_pit(&run.room, p->x, p->z));
    CHECK(p->x < FX(8.0));
    CHECK(p->iframes > 0);

    // Dashing carries the player over a one-cell gap.
    empty_run(&run);
    keep = run_spawn_enemy(&run, ENEMY_CHASER, FX(0.5), FX(11.5));
    keep->state = EST_RECOVER;
    keep->timer = 100000;
    run.room.cells[6][7] = CELL_PIT;
    p = &run.player;
    p->x = FX(6.4);
    p->z = FX(6.5);
    PlayerInput dash = { .move = DIR_SE, .dash = true };
    run_update(&run, &dash);
    step_n(&run, &idle, PLAYER_DASH_FRAMES + 2);
    CHECK_EQ(p->hp, PLAYER_START_HP);
    CHECK(p->x > FX(8.0));

    // An enemy knocked into a pit falls (and counts as a kill).
    empty_run(&run);
    keep = run_spawn_enemy(&run, ENEMY_CHASER, FX(0.5), FX(11.5));
    keep->state = EST_RECOVER;
    keep->timer = 100000;
    run.room.cells[6][8] = CELL_PIT;
    Enemy *e = run_spawn_enemy(&run, ENEMY_CHASER, FX(6.75), FX(6.5));
    e->state = EST_RECOVER;
    e->timer = 100000;
    e->hp = e->max_hp = 50;
    run.player.z = FX(6.5);
    int kills = run.kills;
    PlayerInput atk = { .move = DIR_SE, .attack = true };
    bool enemy_fell = false;
    for (int i = 0; i < 6 && e->active; i++) {
        run_update(&run, &atk);
        for (int t = 0; t < PLAYER_ATTACK_FRAMES + 1 && e->active; t++) {
            run_update(&run, &idle);
            enemy_fell |= 0 != (run.events & EV_ENEMY_FELL);
        }
    }
    CHECK(enemy_fell);
    CHECK(!e->active);
    CHECK_EQ(run.kills, kills + 1);
}

static void pathing_tests(void)
{
    static Run run;
    empty_run(&run);
    // A wall of void down the middle, open only at the bottom.
    for (int z = 0; z < 9; z++)
        run.room.cells[z][6] = CELL_VOID;
    run.player.x = FX(9.5);
    run.player.z = FX(2.5);
    run.player.iframes = 100000;
    Enemy *e = run_spawn_enemy(&run, ENEMY_CHASER, FX(3.5), FX(2.5));
    bool reached = false;
    for (int i = 0; i < 900 && !reached; i++) {
        run.player.iframes = 100000;
        run_update(&run, &idle);
        fx dx = e->x - run.player.x, dz = e->z - run.player.z;
        reached = fx_mul(dx, dx) + fx_mul(dz, dz) < FX(1.5 * 1.5);
    }
    CHECK(reached);

    // Mages only shoot along a clear line.
    empty_run(&run);
    for (int x = 2; x < 10; x++)
        run.room.cells[6][x] = CELL_PILLAR;
    run.player.x = FX(6.0);
    run.player.z = FX(9.5);
    Enemy *m = run_spawn_enemy(&run, ENEMY_SHOOTER, FX(6.0), FX(3.0));
    (void)m;
    int shots = 0, blind = 0;
    for (int i = 0; i < 900; i++) {
        run.player.iframes = 100000;
        run_update(&run, &idle);
        if (run.events & EV_ENEMY_SHOOT) {
            shots++;
            for (int k = 0; k < MAX_ENEMIES; k++) {
                const Enemy *s = &run.enemies[k];
                if (s->active && ENEMY_SHOOTER == s->type
                    && !room_line_clear(&run.room, s->x, s->z,
                                        run.player.x, run.player.z))
                    blind++;
            }
        }
    }
    CHECK_EQ(blind, 0);
    CHECK(shots > 0);   // it found a way round the wall to shoot
}

void run_shapes_tests(void)
{
    generation_tests();
    line_tests();
    fall_tests();
    pathing_tests();
}
