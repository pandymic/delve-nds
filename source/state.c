// The game state machine (state.h).

#include <stddef.h>

#include "state.h"

static const State *states[ST_COUNT];
static StateId current = ST_NONE;
static StateId previous = ST_NONE;
static StateId pending = ST_NONE;
static unsigned frames;

void state_register(StateId id, const State *state)
{
    states[id] = state;
}

void state_set(StateId id)
{
    pending = id;
}


StateId state_previous(void)
{
    return previous;
}

unsigned state_frames(void)
{
    return frames;
}

static void apply_pending(void)
{
    if (ST_NONE == pending)
        return;
    StateId next = pending;
    pending = ST_NONE;

    if (ST_NONE != current && states[current]->exit)
        states[current]->exit();
    previous = current;
    current = next;
    frames = 0;
    if (states[current]->enter)
        states[current]->enter();
}

void state_update(void)
{
    apply_pending();
    if (ST_NONE == current)
        return;
    if (states[current]->update)
        states[current]->update();
    frames++;
}

void state_draw(void)
{
    if (ST_NONE != current && states[current]->draw)
        states[current]->draw();
}
