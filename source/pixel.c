// Builds 4bpp tiles from ASCII-art rows (pixel.h).

#include "pixel.h"

static unsigned pixel_index(char c)
{
    if (c >= '1' && c <= '9')
        return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (unsigned)(c - 'a' + 10);
    return 0;
}

void pixel_tile4(volatile uint32_t *dst, const PixelTile rows)
{
    for (int y = 0; y < 8; y++) {
        uint32_t row = 0;
        for (int x = 0; x < 8; x++)
            row |= (uint32_t)pixel_index(rows[y][x]) << (x * 4);
        dst[y] = row;
    }
}

void pixel_tiles4(volatile uint32_t *base, const PixelTileSlot *tiles, int n)
{
    for (int i = 0; i < n; i++)
        pixel_tile4(base + tiles[i].slot * 8, *tiles[i].rows);
}

void pixel_tile4_fill(volatile uint32_t *dst, unsigned index)
{
    uint32_t row = (index & 0xF) * 0x11111111u;
    for (int y = 0; y < 8; y++)
        dst[y] = row;
}

void pixel_tile4_dither(volatile uint32_t *dst, unsigned a, unsigned b,
                        unsigned level)
{
    // 2x2 Bayer thresholds: pixel uses b when its threshold < level.
    static const unsigned bayer[2][2] = { { 0, 2 }, { 3, 1 } };
    for (int y = 0; y < 8; y++) {
        uint32_t row = 0;
        for (int x = 0; x < 8; x++) {
            unsigned idx = bayer[y & 1][x & 1] < level ? b : a;
            row |= (uint32_t)(idx & 0xF) << (x * 4);
        }
        dst[y] = row;
    }
}
