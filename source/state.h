// Game state machine: one active state, each with enter/update/draw/exit.
//
// state_set() only records the request; the switch happens at the start of
// the next frame so a state never exits while its own update() is running.

#ifndef DELVE_STATE_H
#define DELVE_STATE_H

typedef enum {
    ST_NONE = -1,
    ST_TITLE = 0,
    ST_MENU,
    ST_OPTIONS,
    ST_UPGRADES,
    ST_CREDITS,
    ST_ABOUT,
    ST_PLAY,
    ST_SUMMARY,
    ST_ROGUE,       // Easter egg
    ST_HISTORY,     // the last runs
    ST_SLOTS,       // save slots
    ST_EGG,         // the Easter egg's version splash, Code Genie
    ST_COUNT
} StateId;

typedef struct {
    const char *name;
    void (*enter)(void);
    void (*update)(void);
    void (*draw)(void);   // called inside the 3D frame; may also draw 2D
    void (*exit)(void);
} State;

// Adds a state to the table (main.c registers them all at boot).
void state_register(StateId id, const State *state);
// Requests a switch, applied at the start of the next state_update().
void state_set(StateId id);

// The state that was active before the current one (ST_NONE at first).
StateId state_previous(void);

// Applies a pending switch, then runs update().
void state_update(void);
// Runs the current state's draw(), inside the 3D frame.
void state_draw(void);

// Frames since the current state was entered.
unsigned state_frames(void);

#endif // DELVE_STATE_H
