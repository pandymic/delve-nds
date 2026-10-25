// Per-frame input snapshot. Call input_update() once per frame.

#ifndef DELVE_INPUT_H
#define DELVE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t down;     // pressed this frame
    uint32_t held;     // currently held
    uint32_t repeat;   // pressed this frame or auto-repeating (menus)
    bool touch_down;   // stylus touched this frame
    bool touch_held;
    int touch_x, touch_y;
} Input;

extern Input input;

// Sets the menu key-repeat rate.
void input_init(void);
// Reads the keys and the touch screen into `input`.
void input_update(void);

// True if the stylus went down this frame inside the given pixel rectangle.
bool input_touched_rect(int x, int y, int w, int h);

#endif // DELVE_INPUT_H
