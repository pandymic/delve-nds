// Isometric camera: fixed yaw/pitch, orthographic, two zoom levels.
// Pure logic (no libnds); render.c turns it into GL matrices.
//
// Screen axes for a ground offset (dx, dz) from the target:
//   right = (dx - dz) / sqrt(2)
//   up    = -(dx + dz) * sin(pitch) / sqrt(2)
// so +x runs down-right on screen and +z runs down-left.

#ifndef DELVE_CAMERA_H
#define DELVE_CAMERA_H

#include <stdbool.h>

#include "fixed.h"
#include "game.h"

#define CAM_PITCH_DEG   35
#define CAM_YAW_DEG     45
#define CAM_DISTANCE    FX(24.0)

// View direction from target to eye: (cos p sin y, sin p, cos p cos y).
#define CAM_DIR_X       FX(0.57923)
#define CAM_DIR_Y       FX(0.57358)
#define CAM_DIR_Z       FX(0.57923)
#define CAM_COS_PITCH   FX(0.81915)
#define CAM_UP_K        FX(0.40558)  // sin(pitch) / sqrt(2)

// Ortho view widths in world units (tiles). Art is 16 texels per tile, so
// these give exactly 2 and 1 screen pixels per texel.
#define CAM_VIEW_CLOSE  FX(8.0)
#define CAM_VIEW_FAR    FX(16.0)
#define CAM_ZOOM_FRAMES 16
#define CAM_FOLLOW_DIV  6

typedef struct {
    fx x, z;            // current look-at point on the ground
    fx goal_x, goal_z;  // where it is heading
    fx view_w;          // current ortho width
    fx zoom_from, zoom_to;
    int zoom_t;         // frames into the tween, CAM_ZOOM_FRAMES when done
    ZoomLevel zoom;
    fx room_size;       // clamp bounds are [0, room_size] on x and z
} Camera;

// Places the camera on (x, z), clamped to the room, at a zoom level.
void camera_init(Camera *c, ZoomLevel zoom, fx room_size, fx x, fx z);
// Starts the tween to the other zoom level.
void camera_toggle_zoom(Camera *c);
// Sets the point the camera eases towards.
void camera_follow(Camera *c, fx x, fx z);
// One frame: advances the zoom tween and eases towards the goal.
void camera_update(Camera *c);

static inline fx camera_view_h(const Camera *c)
{
    return fx_mul(c->view_w, FX(0.75)); // 192 / 256
}

// Screen-space projection of a ground offset, in world units.
static inline fx camera_screen_right(fx dx, fx dz)
{
    return fx_mul(dx - dz, FX(0.70711));
}

static inline fx camera_screen_up(fx dx, fx dz)
{
    return -fx_mul(dx + dz, CAM_UP_K);
}

// Clamps a look-at point so the view stays on the room. Exposed for tests.
void camera_clamp(const Camera *c, fx *x, fx *z);

#endif // DELVE_CAMERA_H
