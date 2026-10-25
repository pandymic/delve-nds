// Save file: options + meta-progress in a small versioned binary record.
//
// Header (little endian), all versions:
//    0  magic "DLVS"
//    4  u16 version
//    6  u16 payload size
//    8  u32 checksum (FNV-1a over the payload)
//   12  payload
//
// Version 1 payload, fixed 24 bytes: u32 total_runs, best_depth,
// currency, upgrades, total_kills, u8 music, sfx, zoom, reserved.
//
// Version 2+ payload: tagged fields, each "u8 tag, u8 length, data". The
// decoder starts from defaults, applies the tags it knows and skips the
// rest, so new fields only need a new tag (see SaveTag in save.c) and older
// files keep loading. Bump SAVE_VERSION only when a field's meaning changes,
// and add a migration step for it in save_decode().
//
// Encoding is portable C (stdio), so the whole module runs in host tests.
// Only the FAT init and the path choice are DS specific.
//
// Save slots: SAVE_SLOTS independent files, "<stem>-1.sav" and so
// on, each a complete record (options included, so players sharing a DS
// keep their own). Every record carries meta.last_played, a counter set
// above all other slots' when the slot is chosen: the highest is the slot
// played last.

#ifndef DELVE_SAVE_H
#define DELVE_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game.h"

#define SAVE_VERSION      2
#define SAVE_SLOTS        3
#define SAVE_HEADER_SIZE  12
#define SAVE_MAX_SIZE     1024  // buffer for encode/load; files are smaller
                                // (about 400 bytes with every table full)

// Writes g into buf and returns the record size in bytes.
size_t save_encode(const GameData *g, uint8_t buf[SAVE_MAX_SIZE]);

// Fills options and meta from buf (any supported version). Returns false
// (and leaves g untouched) on a bad magic, version, size or checksum.
bool save_decode(const uint8_t *buf, size_t len, GameData *g);

// DS: initialises FAT and picks where saves go (beside the ROM if the
// loader passed argv, else /saves/, else the card's root). Returns false
// when there is no usable storage. A single pre-slot save ("<stem>.sav",
// 2.4.0 and older) becomes slot 1 when no slot exists yet.
bool save_storage_init(int argc, char **argv);

// The save stem for a ROM path: the path without ".nds" (any case) and
// without a release's "-v<version>" ("fat:/nds/delve-v2.5.0.nds" ->
// "fat:/nds/delve"), so a new version finds the saves of the last. False
// when it is not a .nds path or the stem does not fit in size.
bool save_stem_for_rom(const char *rom, char *out, size_t size);

// Slot files are "<stem>-<slot + 1>.sav". save_select_slot() points
// save_load / save_write at one; save_set_path() at any file (tests).
void save_set_stem(const char *stem);
void save_select_slot(int slot);
int save_selected_slot(void);       // -1 after save_set_path()
void save_set_path(const char *path);
const char *save_path(void);        // the file save_load / save_write use

// Reads a slot without selecting it. False (and g set to defaults) when
// the slot is empty or unreadable.
bool save_peek(int slot, GameData *g);

// Deletes a slot's file (and its temporary copy). True when it is gone.
bool save_erase(int slot);

// The slot played last (highest meta.last_played), or -1 when all are
// empty.
int save_latest_slot(void);

// Makes a single pre-slot save ("<stem>.sav") slot 1 when no slot exists.
// Returns true when it did. Called by save_storage_init().
bool save_adopt_legacy(void);

// Selects slot and marks it played last: its last_played goes above every
// other slot's. The caller writes the save.
void save_mark_played(int slot, GameData *g);

// Loads into g. On a missing or corrupt file, g is set to defaults and
// false is returned. Never fails hard.
bool save_load(GameData *g);

// Writes g to "<path>.tmp", then replaces the save with it, so a power cut
// mid-write leaves either the old or the new file (save_load falls back to
// the .tmp copy). Returns false if storage is unavailable or the write
// failed.
bool save_write(const GameData *g);

#endif // DELVE_SAVE_H
