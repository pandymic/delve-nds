// First-time tips.

#include <string.h>

#include "test.h"
#include "hint.h"
#include "meta.h"
#include "roomgen.h"
#include "run.h"

static void text_tests(void)
{
    for (int h = 0; h < HINT_COUNT; h++) {
        CHECK(strlen(hint_title((HintId)h)) > 0);
        for (int lefty = 0; lefty < 2; lefty++) {
            CHECK(NULL != hint_line((HintId)h, 0, lefty));
            for (int i = 0; i < HINT_LINES; i++) {
                const char *s = hint_line((HintId)h, i, lefty);
                CHECK(NULL == s || strlen(s) <= HINT_LINE_MAX);
            }
        }
    }
    CHECK(hint_line(HINT_FIGHT, 1, true) != hint_line(HINT_FIGHT, 1, false));
    CHECK(hint_line(HINT_CAMP, 0, true) == hint_line(HINT_CAMP, 0, false));
    CHECK(NULL == hint_line(HINT_NONE, 0, false));
}

static void room_tests(void)
{
    static Run run;
    MetaProgress m = { 0 };

    // The camp: welcome first, then the shrine once an upgrade is
    // affordable.
    run_start_hub(&run);
    CHECK_EQ(hint_for_room(&run, &m), HINT_CAMP);
    hint_mark_seen(&m, HINT_CAMP);
    CHECK(hint_seen(&m, HINT_CAMP));
    CHECK_EQ(hint_for_room(&run, &m), HINT_NONE);
    m.currency = upgrade_defs[UPG_VITALITY].cost;
    CHECK_EQ(hint_for_room(&run, &m), HINT_SHRINE);
    hint_mark_seen(&m, HINT_SHRINE);
    CHECK_EQ(hint_for_room(&run, &m), HINT_NONE);

    // A run: the first fight, then pits and traps when a room has them.
    run_start(&run, 3);
    CHECK_EQ(hint_for_room(&run, &m), HINT_FIGHT);
    hint_mark_seen(&m, HINT_FIGHT);
    bool pits = false, traps = false;
    for (uint32_t seed = 1; seed < 400 && !(pits && traps); seed++) {
        roomgen_generate(&run.room, seed * 31u, seed & 1 ? 15 : 3, RT_COMBAT,
                         SIDE_PZ, 6);
        HintId h = hint_for_room(&run, &m);
        pits |= HINT_PITS == h;
        traps |= HINT_TRAPS == h;
        CHECK(HINT_NONE == h || HINT_PITS == h || HINT_TRAPS == h);
    }
    CHECK(pits && traps);
    roomgen_generate(&run.room, 99, 10, RT_BOSS, SIDE_PZ, 6);
    CHECK_EQ(hint_for_room(&run, &m), HINT_BOSS);
    hint_mark_seen(&m, HINT_BOSS);
    hint_mark_seen(&m, HINT_NONE);   // ignored
    CHECK_EQ(m.hints_seen, (1u << HINT_CAMP) | (1u << HINT_SHRINE)
                           | (1u << HINT_FIGHT) | (1u << HINT_BOSS));
}

void run_hints_tests(void)
{
    text_tests();
    room_tests();
}
