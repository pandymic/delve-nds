// Seeded room generation. Same inputs, same room. Pure logic.

#ifndef DELVE_ROOMGEN_H
#define DELVE_ROOMGEN_H

#include <stdint.h>

#include "room.h"

#define ROOMGEN_MAX_OBSTACLES 6

// room_seed = hash(run_seed, depth, door_index)
uint32_t roomgen_seed(uint32_t run_seed, int depth, int door_index);

// Builds a room of the given type, entered through (entry_side, entry_pos).
// Steps: spawn by the entry door, doors and their rewards, obstacles with a
// connectivity check, the room feature, enemy spawn points.
void roomgen_generate(Room *r, uint32_t seed, int depth, RoomType type,
                      WallSide entry_side, int entry_pos);

// The camp between runs: a fixed room with a campfire, torches and banners,
// one portal (the door on the upper-right wall) and a training dummy.
// Stations (the upgrade shrine) are at the HUB_* positions.
void roomgen_hub(Room *r);

#define HUB_SHRINE_X FX(2.5)
#define HUB_SHRINE_Z FX(8.5)
#define HUB_DUMMY_X  FX(9.5)
#define HUB_DUMMY_Z  FX(8.0)
#define HUB_PORTAL_POS 6     // on SIDE_NZ

#endif // DELVE_ROOMGEN_H
