/**
 * File: world.c
 * Description: The world drawn from a converted level (world.h).
 *
 * Every frame redraws all 192 lines: a line of a cell is four words, from
 * the backdrop (four packed bytes, each opened into four pixels by a table
 * in RAM) or an opaque tile, or for a masked tile (backdrop & keep) |
 * colour, four pixels at a time, the keep bits (4-5) shifted down onto
 * planes 0-1.
 */

#pragma GCC optimize("O3")

#include "world.h"

#include <string.h>

#include "pico/stdlib.h"

#define KEEP_LANES 0x03030303u
#define COLOUR_LANES 0x0F0F0F0Fu

/* A packed backdrop byte opened into four pixels, the leftmost lowest. */
static uint32_t open4[256];

int world_init(world_t *w, const x2level_header_t *image, uint16_t *map) {
  if (image->magic != X2LEVEL_MAGIC || image->version != X2LEVEL_VERSION ||
      image->map_rows == 0 || image->map_rows > WORLD_MAP_MAX_ROWS) {
    return -1;
  }
  for (uint32_t b = 0; b < 256; b++) {
    open4[b] = (b & 3u) | ((b >> 2) & 3u) << 8 | ((b >> 4) & 3u) << 16 |
               ((b >> 6) & 3u) << 24;
  }
  const uint8_t *base = (const uint8_t *)image;
  w->image = image;
  w->backdrop = base + image->backdrop_offset;
  w->tiles = base + image->tiles_offset;
  w->map = map;
  w->map_rows = image->map_rows;
  memcpy(map, base + image->map_offset,
         image->map_rows * X2LEVEL_COLS * sizeof(uint16_t));
  return 0;
}

void __not_in_flash_func(world_draw)(const world_t *w, uint32_t top,
                                     uint32_t backdrop_line, uint8_t *dst) {
  static const uint16_t no_row[X2LEVEL_COLS];
  for (uint32_t y = 0; y < X2LEVEL_LINES; y++) {
    uint32_t line = top + y;
    uint32_t row = line / X2LEVEL_CELL;
    uint32_t tile_line = (line % X2LEVEL_CELL) * X2LEVEL_CELL;
    const uint16_t *cells =
        row < w->map_rows ? w->map + row * X2LEVEL_COLS : no_row;
    const uint32_t *packed = (const uint32_t *)(w->backdrop +
                                                ((backdrop_line + y) %
                                                 X2LEVEL_LINES) *
                                                    X2LEVEL_BACKDROP_LINE);
    uint32_t *out = (uint32_t *)(dst + y * X2LEVEL_WIDTH);
    for (int c = 0; c < X2LEVEL_COLS; c++, out += 4) {
      uint32_t word = cells[c];
      uint32_t p = packed[c];
      if (word == 0) {
        out[0] = open4[p & 0xFFu];
        out[1] = open4[(p >> 8) & 0xFFu];
        out[2] = open4[(p >> 16) & 0xFFu];
        out[3] = open4[p >> 24];
        continue;
      }
      const uint32_t *t = (const uint32_t *)(w->tiles + (word & 0x7FFFu) * 32u +
                                             tile_line);
      if (!(word & 0x8000u)) {
        out[0] = t[0];
        out[1] = t[1];
        out[2] = t[2];
        out[3] = t[3];
        continue;
      }
      for (int i = 0; i < 4; i++, p >>= 8) {
        uint32_t k = (t[i] >> X2LEVEL_KEEP_SHIFT) & KEEP_LANES;
        out[i] = (open4[p & 0xFFu] & k) | (t[i] & COLOUR_LANES);
      }
    }
  }
}

bool world_solid(const world_t *w, uint32_t top, int32_t x, int32_t y) {
  if (x < 0 || x >= X2LEVEL_WIDTH || y < 0 || y >= X2LEVEL_LINES) return false;
  uint32_t line = top + (uint32_t)y, row = line / X2LEVEL_CELL;
  if (row >= w->map_rows) return false;
  uint32_t word = w->map[row * X2LEVEL_COLS + (uint32_t)x / X2LEVEL_CELL];
  if (word == 0) return false;
  if (!(word & 0x8000u)) return true;
  uint8_t px = w->tiles[(word & 0x7FFFu) * 32u +
                        (line % X2LEVEL_CELL) * X2LEVEL_CELL +
                        (uint32_t)x % X2LEVEL_CELL];
  return !(px & (1u << X2LEVEL_KEEP_SHIFT));
}

void world_draw_block(const world_t *w, const uint16_t *words, int cols,
                      int rows, int x, int y, bool flash,
                      uint8_t flash_colour, uint8_t *dst) {
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      uint16_t word = words[r * cols + c];
      if (word == 0) continue;
      const uint8_t *tile = w->tiles + (uint32_t)(word & 0x7FFF) * 32u;
      for (int ty = 0; ty < X2LEVEL_CELL; ty++) {
        int sy = y + r * X2LEVEL_CELL + ty;
        if (sy < 0 || sy >= X2LEVEL_LINES) continue;
        uint8_t *row = dst + sy * X2LEVEL_WIDTH;
        const uint8_t *src = tile + ty * X2LEVEL_CELL;
        for (int tx = 0; tx < X2LEVEL_CELL; tx++) {
          int sx = x + c * X2LEVEL_CELL + tx;
          if (sx < 0 || sx >= X2LEVEL_WIDTH) continue;
          uint8_t p = src[tx];
          if (!(word & 0x8000)) {
            row[sx] = flash ? flash_colour : p;
            continue;
          }
          uint8_t k0 = (p >> X2LEVEL_KEEP_SHIFT) & 1, k1 = (p >> (X2LEVEL_KEEP_SHIFT + 1)) & 1;
          uint8_t keep = (uint8_t)(k0 | k1 << 1 | k0 << 2 | k1 << 3);
          uint8_t colour = p & 15;
          if (flash) {
            if (keep != 15) row[sx] = flash_colour;
          } else {
            row[sx] = (uint8_t)((row[sx] & keep) | colour);
          }
        }
      }
    }
  }
}
