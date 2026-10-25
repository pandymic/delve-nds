// Player movement, collision, dash and actions.

#include "test.h"
#include "entity.h"

static void walk(Player *p, const Room *r, Dir8 d, int frames)
{
    PlayerInput in = { .move = d };
    for (int i = 0; i < frames; i++)
        player_update(p, r, &in);
}

void run_player_tests(void)
{
    Room empty, test;
    room_clear(&empty);
    room_make_test(&test);
    Player p;

    // Direction vectors are unit length.
    for (int d = 0; d < DIR_COUNT; d++) {
        fx x, z;
        dir8_vector((Dir8)d, &x, &z);
        fx len2 = fx_mul(x, x) + fx_mul(z, z);
        CHECK(fx_abs(len2 - FX_ONE) < FX(0.01));
    }
    CHECK_EQ(dir8_from_input(0, 0), DIR_NONE);
    CHECK_EQ(dir8_from_input(1, 1), DIR_SE);
    CHECK_EQ(dir8_from_input(-1, -1), DIR_NW);

    // Walls stop the player at exactly its half-size from the edge.
    player_init(&p, FX(6.0), FX(6.0));
    walk(&p, &empty, DIR_NW, 300); // NW = -x
    CHECK_EQ(p.x, PLAYER_HALF);
    CHECK_EQ(p.z, FX(6.0));
    walk(&p, &empty, DIR_SW, 300); // SW = +z
    CHECK_EQ(p.z, FX_INT(ROOM_SIZE) - PLAYER_HALF);

    // Pillar at cell (3,3) with its inset blocks a walk along +x at z = 3.5.
    player_init(&p, FX(1.0), FX(3.5));
    walk(&p, &test, DIR_SE, 200); // SE = +x
    CHECK_EQ(p.x, FX_INT(3) + room_cell_inset(CELL_PILLAR) - PLAYER_HALF);
    CHECK(!room_blocked(&test, p.x, p.z, PLAYER_HALF));

    // Sliding: moving diagonally into a wall keeps the parallel component.
    player_init(&p, FX(6.0), FX(1.0));
    fx z0 = p.z;
    walk(&p, &empty, DIR_N, 100); // N = (-x, -z)
    CHECK(p.z < z0);
    CHECK_EQ(p.z, PLAYER_HALF);
    CHECK(p.x < FX(6.0));

    // Dash: uses a charge, moves further than walking, grants i-frames.
    player_init(&p, FX(6.0), FX(6.0));
    PlayerInput dash = { .move = DIR_SE, .dash = true };
    PlayerInput none = { .move = DIR_NONE };
    player_update(&p, &empty, &dash);
    CHECK_EQ(p.action, PACT_DASH);
    CHECK_EQ(p.dash_charges, PLAYER_DASH_CHARGES - 1);
    CHECK(p.iframes > 0);
    for (int i = 0; i < PLAYER_DASH_FRAMES; i++)
        player_update(&p, &empty, &none);
    CHECK_EQ(p.action, PLAYER_DASH_FRAMES > 0 ? PACT_IDLE : PACT_DASH);
    CHECK(p.x - FX(6.0) > fx_mul(PLAYER_SPEED, FX_INT(PLAYER_DASH_FRAMES)));

    // Second dash after the gap, then no charges left.
    for (int i = 0; i < PLAYER_DASH_GAP; i++)
        player_update(&p, &empty, &none);
    player_update(&p, &empty, &dash);
    CHECK_EQ(p.dash_charges, 0);
    // Within the recharge time a third dash is refused.
    for (int i = 0; i < PLAYER_DASH_GAP; i++)
        player_update(&p, &empty, &none);
    CHECK_EQ(p.dash_charges, 0);
    player_update(&p, &empty, &dash);
    CHECK(PACT_DASH != p.action);

    // Charges come back over time.
    for (int i = 0; i < PLAYER_DASH_RECHARGE * PLAYER_DASH_CHARGES + 5; i++)
        player_update(&p, &empty, &none);
    CHECK_EQ(p.dash_charges, PLAYER_DASH_CHARGES);

    // Attack: one active frame, after the wind-up.
    player_init(&p, FX(6.0), FX(6.0));
    PlayerInput atk = { .move = DIR_NONE, .attack = true };
    player_update(&p, &empty, &atk);
    int hits = 0, first = -1;
    for (int i = 0; i < PLAYER_ATTACK_FRAMES + 2; i++) {
        if (player_attack_hits_now(&p)) {
            hits++;
            if (first < 0)
                first = i;
        }
        player_update(&p, &empty, &none);
    }
    CHECK_EQ(hits, 1);
    CHECK_EQ(p.action, PACT_IDLE);
}
