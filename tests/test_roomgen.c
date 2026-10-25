// Seeded room generation: determinism, connectivity, doors.

#include <string.h>

#include "test.h"
#include "roomgen.h"
#include "run.h"

static void check_room(const Room *r, int depth)
{
    // Doors: 1-3, distinct walls, never the entry wall, in range.
    CHECK(r->door_count >= 1 && r->door_count <= MAX_DOORS);
    bool any_combat = false;
    for (int i = 0; i < r->door_count; i++) {
        const Door *d = &r->doors[i];
        CHECK(d->side != r->entry_side);
        CHECK(d->pos >= 2 && d->pos <= ROOM_SIZE - 3);
        for (int j = i + 1; j < r->door_count; j++)
            CHECK(r->doors[j].side != d->side);
        any_combat |= RT_COMBAT == d->leads_to;
        if (depth < 2)
            CHECK_EQ(d->leads_to, RT_COMBAT);
        // Doorway cell is free.
        int cx, cz;
        door_cell(d->side, d->pos, &cx, &cz);
        CHECK(!room_solid(r, cx, cz));
    }
    if (depth_is_boss(depth + 1)) {
        // The way on before a boss: a single door into the arena.
        CHECK_EQ(r->door_count, 1);
        CHECK_EQ(r->doors[0].leads_to, RT_BOSS);
    } else {
        CHECK(any_combat);
    }

    // Spawn is on the floor and everything is reachable from it.
    int sx = FX_TO_INT(r->spawn_x), sz = FX_TO_INT(r->spawn_z);
    CHECK(!room_solid(r, sx, sz));
    CHECK(room_connected(r, sx, sz));

    int obstacles = 0;
    for (int z = 0; z < ROOM_SIZE; z++)
        for (int x = 0; x < ROOM_SIZE; x++)
            obstacles += CELL_PILLAR == room_cell(r, x, z)
                      || CELL_CRATE == room_cell(r, x, z);
    // Cave rooms add up to three two-cell rock outcrops (SHAPE_CAVE).
    CHECK(obstacles <= ROOMGEN_MAX_OBSTACLES
                       + (AREA_CAVERNS == r->area ? 6 : 0));

    // Enemies on the floor, away from the spawn.
    for (int i = 0; i < r->enemy_count; i++) {
        const EnemySpawn *e = &r->enemies[i];
        CHECK(!room_solid(r, FX_TO_INT(e->x), FX_TO_INT(e->z)));
        fx dx = e->x - r->spawn_x, dz = e->z - r->spawn_z;
        CHECK(fx_mul(dx, dx) + fx_mul(dz, dz) >= FX(4.5 * 4.5) - FX(0.01));
        CHECK(e->type < ENEMY_TYPE_COUNT);
    }

    // Room-type features.
    switch (r->type) {
    case RT_REST:
        CHECK_EQ(r->enemy_count, 0);
        CHECK_EQ(room_cell(r, r->feature_x, r->feature_z), CELL_FIRE);
        break;
    case RT_TREASURE:
        CHECK_EQ(room_cell(r, r->feature_x, r->feature_z), CELL_CHEST);
        break;
    default:
        CHECK_EQ(r->feature_x, -1);
        CHECK(r->enemy_count >= 1);
        break;
    }
}

void run_roomgen_tests(void)
{
    static Room a, b;

    // Same seed, same room.
    roomgen_generate(&a, 12345, 3, RT_COMBAT, SIDE_NX, 5);
    roomgen_generate(&b, 12345, 3, RT_COMBAT, SIDE_NX, 5);
    CHECK(0 == memcmp(&a, &b, sizeof(a)));
    roomgen_generate(&b, 12346, 3, RT_COMBAT, SIDE_NX, 5);
    CHECK(0 != memcmp(&a, &b, sizeof(a)));

    // The seed hash separates depth and door index.
    CHECK(roomgen_seed(1, 2, 0) != roomgen_seed(1, 2, 1));
    CHECK(roomgen_seed(1, 2, 0) != roomgen_seed(1, 3, 0));

    // Invariants over many rooms.
    int failures_before = test_failures;
    for (uint32_t seed = 0; seed < 3000 && test_failures == failures_before;
         seed++) {
        int depth = 1 + (int)(seed % 25);
        RoomType type = (RoomType)(seed % RT_COUNT);
        WallSide entry = (WallSide)((seed / 3) % SIDE_COUNT);
        int pos = 2 + (int)(seed % (ROOM_SIZE - 4));
        roomgen_generate(&a, roomgen_seed(seed, depth, 0), depth, type, entry, pos);
        check_room(&a, depth);
    }

    // Walking a chain of rooms through doors.
    static Run run;
    run_start(&run, 777);
    CHECK_EQ(run.depth, 1);
    CHECK_EQ(run.room.type, RT_COMBAT);
    for (int i = 0; i < 30; i++) {
        int door = i % run.room.door_count;
        WallSide exit_side = run.room.doors[door].side;
        RoomType next = run.room.doors[door].leads_to;
        run_enter_door(&run, door);
        CHECK_EQ(run.depth, i + 2);
        CHECK_EQ(run.room.type, next);
        CHECK_EQ(run.room.entry_side, side_opposite(exit_side));
        CHECK_EQ(run.player.x, run.room.spawn_x);
        CHECK_EQ(run.player.z, run.room.spawn_z);
        CHECK_EQ(run_enemies_alive(&run), run.room.enemy_count);
    }

    // Reaching an open door raises EV_DOOR_ENTER.
    run_start(&run, 4242);
    for (int i = 0; i < MAX_ENEMIES; i++)
        run.enemies[i].active = false;
    PlayerInput idle = { .move = DIR_NONE };
    run_update(&run, &idle);
    CHECK_EQ(run.status, ROOM_CLEAR);
    const Door *d = &run.room.doors[0];
    fx cx, cz, nx, nz;
    door_center(d->side, d->pos, &cx, &cz);
    side_inward(d->side, &nx, &nz);
    run.player.x = cx + fx_mul(nx, PLAYER_HALF);
    run.player.z = cz + fx_mul(nz, PLAYER_HALF);
    // A cleared combat room offers a boon first: the door stays shut.
    CHECK(run.boon_pending);
    run_update(&run, &idle);
    CHECK(!(run.events & EV_DOOR_ENTER));
    BoonId offer[BOON_CHOICES];
    CHECK_EQ(boon_offer(&run, offer), BOON_CHOICES);
    run_take_boon(&run, offer[0]);
    run_update(&run, &idle);
    CHECK(run.events & EV_DOOR_ENTER);
    CHECK_EQ(run.exit_door, 0);
}
