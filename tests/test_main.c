// Host test runner: runs every suite and reports the totals.

#include "test.h"

int test_checks;
int test_failures;

typedef struct {
    const char *name;
    void (*run)(void);
} Suite;

static const Suite suites[] = {
    { "rng", run_rng_tests },
    { "fixed", run_fixed_tests },
    { "camera", run_camera_tests },
    { "room", run_room_tests },
    { "player", run_player_tests },
    { "fade", run_fade_tests },
    { "combat", run_combat_tests },
    { "roomgen", run_roomgen_tests },
    { "difficulty", run_difficulty_tests },
    { "save", run_save_tests },
    { "anim", run_anim_tests },
    { "decor", run_decor_tests },
    { "rogue", run_rogue_tests },
    { "kit", run_kit_tests },
    { "props", run_props_tests },
    { "shapes", run_shapes_tests },
    { "roster", run_roster_tests },
    { "boons", run_boons_tests },
    { "hub", run_hub_tests },
    { "bosses", run_bosses_tests },
    { "hints", run_hints_tests },
    { "areas", run_areas_tests },
    { "caverns", run_caverns_tests },
    { "molten", run_molten_tests },
    { "paths", run_paths_tests },
    { "monsters", run_monsters_tests },
    { "cheats", run_cheats_tests },
};

int main(void)
{
    for (unsigned i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        int before = test_failures;
        suites[i].run();
        printf("%-12s %s\n", suites[i].name,
               test_failures == before ? "ok" : "FAILED");
    }
    printf("%d checks, %d failures\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
