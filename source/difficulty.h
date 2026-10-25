// Difficulty curve: one function of depth drives every scaling knob, so
// tuning happens in one place. Pure logic.

#ifndef DELVE_DIFFICULTY_H
#define DELVE_DIFFICULTY_H

#include <stdbool.h>

#include "entity.h"

typedef struct {
    int enemy_count;     // enemies in a combat room (capped by the pool)
    int hp_pct;          // enemy HP multiplier, percent
    int damage_bonus;    // added to enemy contact/projectile damage
    int elite_chance;    // percent per enemy
    int weights[ENEMY_TYPE_COUNT];   // relative spawn chances (0 = locked)
    int hazards;         // traps per combat room (0 = locked)
    int hazard_kinds;    // 1 spikes, 2 + fire jets, 3 + saw blades
    int explosive_pct;   // chance a barrel is explosive (0 = locked)
} Difficulty;

// The scaling for a depth (1 and up; lower depths count as 1).
Difficulty difficulty(int depth);

#endif // DELVE_DIFFICULTY_H
