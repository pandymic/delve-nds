// Sprite animation: frame sequences with per-frame durations, and the frame
// choice for the player and enemies. Pure logic (no libnds), so animation
// timing can be checked against gameplay timing in host tests.

#ifndef DELVE_ANIM_H
#define DELVE_ANIM_H

#include <stdbool.h>
#include <stdint.h>

#include "entity.h"

typedef struct {
    uint8_t first;          // first slot within a direction (art_layout.h)
    uint8_t count;          // frames
    uint8_t tick;           // frames per animation frame when ticks is NULL
    bool loop;              // else holds the last frame
    const uint8_t *ticks;   // optional per-frame durations (count entries)
} AnimDef;

// Total duration of one pass, in game frames.
int anim_length(const AnimDef *a);

// Frame offset (0..count-1) after t game frames.
int anim_offset(const AnimDef *a, int t);

// Game frame at which frame offset k starts.
int anim_start_of(const AnimDef *a, int k);

// A frame of a character sheet: slot within the sheet, and whether to draw
// it mirrored (left-facing directions reuse the right-facing rows).
typedef struct {
    int slot;
    bool mirror;
} SpriteFrame;

// Sheet direction row and mirror flag for a screen-space facing.
void anim_dir_row(Dir8 facing, int *row, bool *mirror);

// Screen-space facing from a world-space offset (dx, dz).
Dir8 anim_facing_towards(fx dx, fx dz);

// Player animation tracking: which animation is showing and since when.
typedef enum {
    PANIM_IDLE,
    PANIM_WALK,
    PANIM_DASH,
    PANIM_SWORD,
    PANIM_THROW,
    PANIM_BLAST,    // a spin: the sword arc frame through every direction
    PANIM_HURT,
    PANIM_DEATH,
    PANIM_COUNT
} PlayerAnim;

typedef struct {
    PlayerAnim anim;
    uint32_t start;         // game frame the animation started
} AnimTrack;

extern const AnimDef player_anims[PANIM_COUNT];

// Frames the hurt pose is shown after a hit.
#define PLAYER_HURT_POSE 14

// Picks the player's animation for this frame. now = game frame counter.
PlayerAnim player_anim_select(const Player *p);
SpriteFrame player_anim_frame(AnimTrack *track, const Player *p,
                              uint32_t now);

// Enemy frame from its state and timers.
SpriteFrame enemy_anim_frame(const Enemy *e, Dir8 facing);

// Layout of an enemy type's sheet: frames per direction, and directions
// drawn (5, or 1 for the slime, mirrored).
int enemy_frames_per_dir(EnemyType t);
int enemy_dirs(EnemyType t);

#endif // DELVE_ANIM_H
