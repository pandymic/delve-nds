// Areas as data, waypoints.

#include <string.h>

#include "test.h"
#include "meta.h"
#include "roomgen.h"
#include "run.h"
#include "save.h"

static void table_tests(void)
{
    for (int a = 0; a < AREA_COUNT; a++) {
        const AreaDef *d = &area_defs[a];
        CHECK(d->banner && d->name && d->waypoint);
        CHECK(enemy_is_boss((EnemyType)d->boss));
        int shapes = 0;
        for (int s = 0; s < SHAPE_COUNT; s++)
            shapes += d->shapes[s];
        CHECK(shapes > 0);
        for (int k = 0; k < 3; k++)
            CHECK(d->traps[k] < HAZ_KIND_COUNT);
        CHECK_EQ(room_area(area_first_depth(a)), a);
        CHECK_EQ(room_area(area_first_depth(a) + AREA_DEPTHS - 1), a);
        // Each area's boss arena holds its boss.
        static Room r;
        roomgen_generate(&r, 77u + (uint32_t)a, (a + 1) * AREA_DEPTHS, RT_BOSS,
                         SIDE_PZ, 6);
        CHECK_EQ(r.enemy_count, 1);
        CHECK_EQ(r.enemies[0].type, d->boss);
        CHECK_EQ(r.area, a);
    }
    // After the last area the order starts again.
    CHECK_EQ(room_area(AREA_COUNT * AREA_DEPTHS + 1), AREA_DUNGEON);
}

static void waypoint_tests(void)
{
    MetaProgress m = { 0 };
    CHECK(meta_waypoint_open(&m, 0));
    CHECK(!meta_waypoint_open(&m, 1));
    CHECK(!meta_unlock_waypoint(&m, 9));     // not a boss depth
    CHECK(meta_unlock_waypoint(&m, 10));     // the first boss: area 1
    CHECK(meta_waypoint_open(&m, 1));
    CHECK(!meta_unlock_waypoint(&m, 10));    // already open
    // A boss on a later way down opens nothing new.
    CHECK(!meta_unlock_waypoint(&m, AREA_COUNT * AREA_DEPTHS));

    // Starting at a waypoint: its first depth, a boon per area skipped.
    static Run run;
    run_start_at(&run, 1234, 1);
    CHECK_EQ(run.depth, area_first_depth(1));
    CHECK_EQ(run.start_depth, area_first_depth(1));
    CHECK_EQ(run.room.area, 1);
    CHECK_EQ(run.rooms_cleared, 0);
    int boons = 0;
    for (int i = 0; i < BOON_COUNT; i++)
        boons += run.boons[i];
    CHECK_EQ(boons, WAYPOINT_BOONS);
    run_start(&run, 1234);
    CHECK_EQ(run.depth, 1);
    CHECK_EQ(run.start_depth, 1);

    // Only the depths gone down count for the depth bonus.
    RunResult r = { .depth = 13, .start_depth = 11, .kills = 0, .gold = 0 };
    CHECK_EQ(meta_depths_descended(&r), 3);
    MetaProgress p = { 0 };
    meta_finish_run(&p, &r);
    CHECK_EQ(r.earned, 3 * META_GOLD_PER_DEPTH);
    RunResult old = { .depth = 4 };          // start 0: from depth 1
    CHECK_EQ(meta_depths_descended(&old), 4);
}

// A 2.1.0 save's history (27-byte entries, no start depth) still loads.
static void old_history_tests(void)
{
    uint8_t payload[3 + 27];
    memset(payload, 0, sizeof(payload));
    payload[0] = 33;           // TAG_RUN_HISTORY
    payload[1] = 1 + 27;
    payload[2] = 1;            // one run
    payload[3 + 4] = 9;        // depth 9
    uint8_t buf[SAVE_HEADER_SIZE + sizeof(payload)];
    memcpy(buf, "DLVS", 4);
    buf[4] = 2;
    buf[5] = 0;
    buf[6] = sizeof(payload);
    buf[7] = 0;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(payload); i++) {
        h ^= payload[i];
        h *= 16777619u;
    }
    for (int i = 0; i < 4; i++)
        buf[8 + i] = (uint8_t)(h >> (8 * i));
    memcpy(buf + SAVE_HEADER_SIZE, payload, sizeof(payload));
    GameData g;
    game_set_defaults(&g);
    CHECK(save_decode(buf, sizeof(buf), &g));
    CHECK_EQ(g.history.count, 1);
    CHECK_EQ(g.history.runs[0].depth, 9);
    CHECK_EQ(g.history.runs[0].start_depth, 1);

    // And the waypoints round-trip.
    g.meta.waypoints = 0x6;
    g.history.runs[0].start_depth = 21;
    uint8_t out[SAVE_MAX_SIZE];
    size_t n = save_encode(&g, out);
    GameData back;
    game_set_defaults(&back);
    CHECK(save_decode(out, n, &back));
    CHECK_EQ(back.meta.waypoints, 0x6);
    CHECK_EQ(back.history.runs[0].start_depth, 21);
}

void run_areas_tests(void)
{
    table_tests();
    waypoint_tests();
    old_history_tests();
}
