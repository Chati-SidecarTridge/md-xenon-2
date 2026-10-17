/**
 * File: world.h
 * Description: Level 1's world drawn the way the original draws it: the map's
 *              cells (the backdrop alone, an opaque tile, or a masked tile
 *              over the backdrop) for the 192 playfield lines, from a
 *              converted level (x2level.h) in flash and the map in RAM.
 *              Plain C, no SDK beyond the placement macros.
 */

#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "x2level.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_MAP_MAX_ROWS 300 /* the largest map of the five levels */

typedef struct {
  const x2level_header_t *image; /* the converted level, read in place */
  const uint8_t *backdrop; /* packed, four pixels a byte */
  const uint8_t *tiles;
  uint16_t *map; /* map_rows x 20 words in RAM: the level's code writes it */
  uint32_t map_rows;
} world_t;

/* Point the world at a converted level and copy its map into `map`
 * (WORLD_MAP_MAX_ROWS rows). Returns 0, or -1 if the image is not a
 * complete level image or its map does not fit. */
int world_init(world_t *w, const x2level_header_t *image, uint16_t *map);

/* Draw the playfield into `dst` (X2LEVEL_LINES lines of X2LEVEL_WIDTH
 * bytes): `top` is the world line at the screen's top (the original's
 * $CD0: the map's row top / 16, its line top % 16), `backdrop_line` the
 * backdrop line at the screen's top (0-191). Rows past the map's end show
 * the backdrop. */
void world_draw(const world_t *w, uint32_t top, uint32_t backdrop_line,
                uint8_t *dst);

/* A block of `cols` x `rows` cells given by map words (row by row) drawn
 * over `dst` with its top-left corner at (x, y), as the original's $21AC
 * draws a boss: 0 leaves the screen, an opaque tile covers it, a masked
 * tile keeps the screen's planes where its mask is set (planes 0 and 2
 * under the mask's first word, 1 and 3 under its second). `flash`: every
 * pixel a tile draws in `flash_colour` instead ($2504). Clipped to the
 * playfield. */
void world_draw_block(const world_t *w, const uint16_t *words, int cols,
                      int rows, int x, int y, bool flash,
                      uint8_t flash_colour, uint8_t *dst);

/* A bitmap (`key` transparent) drawn with its top-left corner at (x, y)
 * only where the backdrop shows through the scenery (the diving ship,
 * $1912): a cell of 0, or a masked tile where it keeps the backdrop. */
void world_draw_under(const world_t *w, uint32_t top, const uint8_t *pixels,
                      int width, int height, int x, int y, uint8_t key,
                      uint8_t *dst);

/* Whether the playfield's pixel (x, y) shows solid scenery with the world
 * line `top` at the screen's top: an opaque tile everywhere, a masked tile
 * where it does not keep the backdrop (the original's $3582), never the
 * backdrop. */
bool world_solid(const world_t *w, uint32_t top, int32_t x, int32_t y);

#ifdef __cplusplus
}
#endif

#endif /* WORLD_H */
