// Camera projection, zoom and clamping.

#include "test.h"
#include "camera.h"
#include "config.h"

#define NEAR(a, b) (fx_abs((a) - (b)) <= FX(0.01))

void run_camera_tests(void)
{
    Camera c;
    const fx size = FX_INT(ROOM_SIZE);
    const fx mid = size / 2;

    // The room centre is a valid look-at point at the close zoom. The far
    // view is nearly as tall as the whole room, so it may shift the centre
    // a little to keep the back wall tops on screen.
    camera_init(&c, ZOOM_CLOSE, size, mid, mid);
    CHECK(NEAR(c.x, mid) && NEAR(c.z, mid));
    camera_init(&c, ZOOM_FAR, size, mid, mid);
    CHECK(fx_abs(c.x - mid) < FX(1.0) && fx_abs(c.z - mid) < FX(1.0));

    // Close zoom: a far corner is pulled back towards the room.
    camera_init(&c, ZOOM_CLOSE, size, 0, 0);
    CHECK(c.x > 0 && c.z > 0);

    // Clamp is idempotent and keeps points inside a sane box.
    for (int z = -4; z <= ROOM_SIZE + 4; z += 2) {
        for (int x = -4; x <= ROOM_SIZE + 4; x += 2) {
            fx px = FX_INT(x), pz = FX_INT(z);
            camera_clamp(&c, &px, &pz);
            fx qx = px, qz = pz;
            camera_clamp(&c, &qx, &qz);
            CHECK(NEAR(px, qx) && NEAR(pz, qz));
            CHECK(px > -size && px < 2 * size);
            CHECK(pz > -size && pz < 2 * size);
        }
    }

    // Zoom tween ends exactly on the target width.
    camera_init(&c, ZOOM_CLOSE, size, mid, mid);
    camera_toggle_zoom(&c);
    CHECK_EQ(c.zoom, ZOOM_FAR);
    for (int i = 0; i < CAM_ZOOM_FRAMES; i++)
        camera_update(&c);
    CHECK_EQ(c.view_w, CAM_VIEW_FAR);
    camera_toggle_zoom(&c);
    for (int i = 0; i < CAM_ZOOM_FRAMES + 5; i++)
        camera_update(&c);
    CHECK_EQ(c.view_w, CAM_VIEW_CLOSE);

    // Follow converges on a reachable goal.
    camera_follow(&c, mid + FX(1.0), mid);
    for (int i = 0; i < 200; i++)
        camera_update(&c);
    CHECK(NEAR(c.x, mid + FX(1.0)) && NEAR(c.z, mid));
}
