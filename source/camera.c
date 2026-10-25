// The isometric camera: zoom tween, smoothed follow, room clamping (camera.h).

#include "camera.h"

// How far the view may show past the room edge, in world units.
#define CAM_MARGIN      FX(1.0)
// Height of the back walls in screen units (wall height * cos pitch), so
// their tops stay visible when the camera clamps upwards.
#define CAM_WALL_SCREEN FX(1.3)

void camera_init(Camera *c, ZoomLevel zoom, fx room_size, fx x, fx z)
{
    c->zoom = zoom;
    c->view_w = ZOOM_FAR == zoom ? CAM_VIEW_FAR : CAM_VIEW_CLOSE;
    c->zoom_from = c->zoom_to = c->view_w;
    c->zoom_t = CAM_ZOOM_FRAMES;
    c->room_size = room_size;
    camera_clamp(c, &x, &z);
    c->x = c->goal_x = x;
    c->z = c->goal_z = z;
}

void camera_toggle_zoom(Camera *c)
{
    c->zoom = ZOOM_FAR == c->zoom ? ZOOM_CLOSE : ZOOM_FAR;
    c->zoom_from = c->view_w;
    c->zoom_to = ZOOM_FAR == c->zoom ? CAM_VIEW_FAR : CAM_VIEW_CLOSE;
    c->zoom_t = 0;
}

void camera_follow(Camera *c, fx x, fx z)
{
    c->goal_x = x;
    c->goal_z = z;
}

// Clamps v to [lo, hi], or centres it when the range is inverted (the view
// is wider than the allowed area).
static fx clamp_or_centre(fx v, fx lo, fx hi)
{
    if (lo > hi)
        return (lo + hi) / 2;
    return fx_clamp(v, lo, hi);
}

void camera_clamp(const Camera *c, fx *x, fx *z)
{
    fx half = c->room_size / 2;
    fx dx = *x - half;
    fx dz = *z - half;

    // Room extents in screen space, relative to its centre.
    fx ext_r = fx_mul(c->room_size, FX(0.70711));       // half diagonal
    fx ext_up = fx_mul(c->room_size, CAM_UP_K) + CAM_WALL_SCREEN;
    fx ext_down = fx_mul(c->room_size, CAM_UP_K);

    fx hw = c->view_w / 2;
    fx hh = camera_view_h(c) / 2;

    fx r = camera_screen_right(dx, dz);
    fx u = camera_screen_up(dx, dz);
    r = clamp_or_centre(r, -ext_r + hw - CAM_MARGIN, ext_r - hw + CAM_MARGIN);
    u = clamp_or_centre(u, -ext_down + hh - CAM_MARGIN, ext_up - hh + CAM_MARGIN);

    // Invert the projection: dx - dz = r * sqrt(2), dx + dz = -u / K.
    fx diff = fx_mul(r, FX(1.41421));
    fx sum = -fx_div(u, CAM_UP_K);
    *x = half + (sum + diff) / 2;
    *z = half + (sum - diff) / 2;
}

void camera_update(Camera *c)
{
    if (c->zoom_t < CAM_ZOOM_FRAMES) {
        c->zoom_t++;
        fx t = fx_smooth(c->zoom_t * FX_ONE / CAM_ZOOM_FRAMES);
        c->view_w = fx_lerp(c->zoom_from, c->zoom_to, t);
    }

    fx gx = c->goal_x, gz = c->goal_z;
    camera_clamp(c, &gx, &gz);
    c->x += (gx - c->x) / CAM_FOLLOW_DIV;
    c->z += (gz - c->z) / CAM_FOLLOW_DIV;
}
