// Minimal host test harness. Each tests/test_*.c file exposes one
// run_<name>_tests() function, listed in tests/test_main.c.

#ifndef DELVE_TEST_H
#define DELVE_TEST_H

#include <stdio.h>

extern int test_checks;
extern int test_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        test_checks++;                                                  \
        if (!(cond)) {                                                  \
            test_failures++;                                            \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        long long va_ = (long long)(a), vb_ = (long long)(b);           \
        test_checks++;                                                  \
        if (va_ != vb_) {                                               \
            test_failures++;                                            \
            printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n",           \
                   __FILE__, __LINE__, #a, #b, va_, vb_);               \
        }                                                               \
    } while (0)

// The test suites, one per tests/test_*.c file.
void run_rng_tests(void);
void run_cheats_tests(void);
void run_fixed_tests(void);
void run_camera_tests(void);
void run_room_tests(void);
void run_player_tests(void);
void run_fade_tests(void);
void run_combat_tests(void);
void run_roomgen_tests(void);
void run_difficulty_tests(void);
void run_anim_tests(void);
void run_decor_tests(void);
void run_rogue_tests(void);
void run_kit_tests(void);
void run_props_tests(void);
void run_shapes_tests(void);
void run_roster_tests(void);
void run_boons_tests(void);
void run_hub_tests(void);
void run_bosses_tests(void);
void run_hints_tests(void);
void run_areas_tests(void);
void run_caverns_tests(void);
void run_molten_tests(void);
void run_paths_tests(void);
void run_monsters_tests(void);
void run_save_tests(void);

#endif // DELVE_TEST_H
