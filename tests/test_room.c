// Room geometry, collision and connectivity.

#include "test.h"
#include "room.h"

void run_room_tests(void)
{
    Room r;
    room_make_test(&r);
    CHECK(room_solid(&r, -1, 0));
    CHECK(room_solid(&r, 0, ROOM_SIZE));
    CHECK(!room_solid(&r, 0, 0));
    CHECK(room_solid(&r, 3, 3));
    CHECK_EQ(room_cell(&r, 3, 3), CELL_PILLAR);
    CHECK_EQ(room_cell(&r, 5, 6), CELL_CRATE);
}
