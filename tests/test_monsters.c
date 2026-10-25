// The Miniworld monster sheets (down, right, up; left mirrored).

#include "test.h"
#include "anim.h"
#include "art_layout.h"
#include "run.h"

void run_monsters_tests(void)
{
    static const EnemyType three[] = {
        ENEMY_DIGGER, ENEMY_GOLEM, ENEMY_MAGMA_BRUTE, ENEMY_IMP,
        ENEMY_MAGMA_LORD,
    };
    // Facing -> direction row and mirror.
    static const struct { Dir8 facing; int row; bool mirror; } rows[] = {
        { DIR_S, 0, false }, { DIR_SE, 1, false }, { DIR_E, 1, false },
        { DIR_NE, 1, false }, { DIR_N, 2, false }, { DIR_SW, 1, true },
        { DIR_W, 1, true }, { DIR_NW, 1, true },
    };
    for (unsigned t = 0; t < sizeof(three) / sizeof(three[0]); t++) {
        CHECK_EQ(enemy_dirs(three[t]), 3);
        int per = enemy_frames_per_dir(three[t]);
        Enemy e = { .type = three[t], .state = EST_RECOVER, .active = true };
        for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            SpriteFrame f = enemy_anim_frame(&e, rows[i].facing);
            CHECK_EQ(f.slot / per, rows[i].row);
            CHECK_EQ(f.mirror, rows[i].mirror);
            CHECK(f.slot % per < per);
        }
        // Dying runs through the death frames, the last one held.
        e.state = EST_DYING;
        e.timer = 0;
        SpriteFrame f = enemy_anim_frame(&e, DIR_S);
        CHECK(f.slot < per);
        CHECK(f.slot >= per - 3);
    }
    // Each sheet's three directions fit its texture.
    static const int sheets[][4] = {
        { IF_FRAMES_PER_DIR, IF_CELL, IF_SHEET_H, 0 },
        { UF_FRAMES_PER_DIR, UF_CELL, UF_SHEET_H, 0 },
        { RF_FRAMES_PER_DIR, RF_CELL, RF_SHEET_H, 0 },
        { YF_FRAMES_PER_DIR, YF_CELL, YF_SHEET_H, 0 },
        { GF_FRAMES_PER_DIR, GF_CELL, GF_SHEET_H, 0 },
    };
    for (unsigned s = 0; s < sizeof(sheets) / sizeof(sheets[0]); s++) {
        int cols = 256 / sheets[s][1];
        int lines = (3 * sheets[s][0] + cols - 1) / cols;
        CHECK(lines * sheets[s][1] <= sheets[s][2]);
    }
}
