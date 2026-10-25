// Per-frame key and touch snapshot from libnds (input.h).

#include <nds.h>

#include "input.h"

Input input;

void input_init(void)
{
    keysSetRepeat(20, 5);
}

void input_update(void)
{
    scanKeys();
    input.down = keysDown();
    input.held = keysHeld();
    input.repeat = keysDownRepeat();

    input.touch_held = 0 != (input.held & KEY_TOUCH);
    input.touch_down = 0 != (input.down & KEY_TOUCH);
    if (input.touch_held) {
        touchPosition t;
        touchRead(&t);
        input.touch_x = t.px;
        input.touch_y = t.py;
    }
}

bool input_touched_rect(int x, int y, int w, int h)
{
    return input.touch_down
        && input.touch_x >= x && input.touch_x < x + w
        && input.touch_y >= y && input.touch_y < y + h;
}
