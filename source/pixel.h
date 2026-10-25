// Pixel-art helpers: build 4bpp tiles from readable ASCII rows.
//
// Each tile is 8 strings of 8 characters. '.' or '0' is transparent, '1'-'9'
// and 'a'-'e' are palette indices 1-14. Index 15 is left to the text console.

#ifndef DELVE_PIXEL_H
#define DELVE_PIXEL_H

#include <stdint.h>

typedef const char *const PixelTile[8];

// Writes one 4bpp tile (32 bytes) using 32-bit stores (VRAM-safe).
void pixel_tile4(volatile uint32_t *dst, const PixelTile rows);

// A tile and the slot (tile index from base) it is written to.
typedef struct {
    int slot;
    const PixelTile *rows;
} PixelTileSlot;

// Writes n tiles, each at base + slot * 8 words.
void pixel_tiles4(volatile uint32_t *base, const PixelTileSlot *tiles, int n);

// Writes a tile filled with one palette index.
void pixel_tile4_fill(volatile uint32_t *dst, unsigned index);

// Writes a tile filled with a 2x2 ordered dither between two indices.
// level 0..4 = share of index b (0 = all a, 4 = all b).
void pixel_tile4_dither(volatile uint32_t *dst, unsigned a, unsigned b,
                        unsigned level);

#endif // DELVE_PIXEL_H
