// The obstacle occlusion fade.

#include "test.h"
#include "room.h"

#define HW FX(0.3125)
#define H  FX(1.1444)

void run_fade_tests(void)
{
    Room r;
    room_make_test(&r); // pillars at (3,3) (8,3) (3,8) (8,8)

    // Standing just behind (camera-wise) the pillar at (3,3): hidden.
    CHECK(room_occludes(&r, 3, 3, FX(2.84), FX(3.5), HW, H));
    CHECK(room_occludes(&r, 3, 3, FX(2.5), FX(2.5), HW, H));
    // In front of it: never hidden by it.
    CHECK(!room_occludes(&r, 3, 3, FX(4.5), FX(4.5), HW, H));
    // Behind but far to the side on screen: not hidden.
    CHECK(!room_occludes(&r, 3, 3, FX(1.0), FX(5.5), HW, H));
    // Far behind, above its top on screen: not hidden.
    CHECK(!room_occludes(&r, 3, 3, FX(0.5), FX(0.5), HW, H));
    // Floor cells never occlude.
    CHECK(!room_occludes(&r, 0, 0, FX(0.5), FX(0.5), HW, H));

    RoomAlpha a;
    room_fade_reset(a);
    CHECK_EQ(a[3][3], FADE_ALPHA_OPAQUE);
    for (int i = 0; i < 20; i++)
        room_fade_update(&r, a, FX(2.84), FX(3.5), HW, H);
    CHECK_EQ(a[3][3], FADE_ALPHA_MIN);
    CHECK_EQ(a[8][8], FADE_ALPHA_OPAQUE);
    for (int i = 0; i < 20; i++)
        room_fade_update(&r, a, FX(6.0), FX(11.0), HW, H);
    CHECK_EQ(a[3][3], FADE_ALPHA_OPAQUE);
}
