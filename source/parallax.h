// Parallax background behind the 3D scene (main engine BG2, BG3). Tiles and
// maps are generated procedurally at init.
//
// There are no spare BG layers, so depth comes from per-scanline scrolling:
// HBlank DMA rewrites each layer's scroll registers on every line from a
// table. The star layer splits into PLX_BANDS bands moving at different
// speeds, and the fog layer ripples.

#ifndef DELVE_PARALLAX_H
#define DELVE_PARALLAX_H

#include <stdbool.h>
#include <stdint.h>

// Sets up both layers and draws their tiles from the seed.
void parallax_init(uint32_t seed);

// Advances the scroll. Speeds are in 1/256 pixel per frame for the far
// layer; the near layer moves at 3x that.
void parallax_update(int speed_x, int speed_y);

// Offsets both layers by a camera position in pixels, scaled per layer, on
// top of the drift. Used in rooms so the void moves with the camera.
void parallax_set_camera(int cam_x, int cam_y);


// Restarts the per-line scroll DMA for the coming frame. Call once per
// frame in vblank, after bgUpdate() (video_wait_vblank does).
void parallax_vblank(void);

// Colour themes for the sky (play.c picks one per area and depth band).
#define PLX_THEMES 10
#define PLX_THEME_NIGHT 5   // the camp
// Recolours the stars, fog and backdrop (theme 0..PLX_THEMES-1).
void parallax_set_theme(int theme);

#define PLX_BANDS 8

// Heat haze over the Molten Depths: both layers waver.
void parallax_set_shimmer(bool on);

#endif // DELVE_PARALLAX_H
