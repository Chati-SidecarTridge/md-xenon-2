/**
 * File: x2sprite.h
 * Description: The original's masked sprites converted into byte-per-pixel
 *              bitmaps, found again by the address the original keeps them
 *              at, so its animation lists can be followed as they are.
 *              Shared by the level's and the shared graphics' converters
 *              and by the game at run time. Plain C, no SDK.
 *
 * A masked sprite (the original's draw routine $10A2): four words (hotspot
 * x, hotspot y, width, lines - 1), then each line plane-major: plane 0 of
 * every 16-pixel group, planes 1, 2, 3, the mask (a set bit keeps the
 * screen). Its bitmap is the sprite whole groups wide, a colour 0-15 or
 * X2SPRITE_KEY a pixel, row by row; it is drawn with its top-left corner at
 * (x - hot_x, y - hot_y). The four words before the sprite are its
 * collision box (the original's $3D58): its offset from the top-left
 * corner, its width and height; at (x, y) the box spans x - hot_x + box_x
 * to that + box_w - 1, and likewise in y.
 *
 * An animation list: pairs of a sprite's address (long) and the frames it
 * shows (word); a zero long ends it, followed by the routine that runs then
 * (long): X2SPRITE_LOOP (or the routine it jumps to, X2SPRITE_LOOP_AT) goes
 * on with the list whose address follows; any other ends the animation.
 */

#ifndef X2SPRITE_H
#define X2SPRITE_H

#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X2SPRITE_KEY 0x10u     /* transparent: never a colour */
#define X2SPRITE_LOOP 0xE3Cu   /* the list routine that goes on elsewhere: */
#define X2SPRITE_LOOP_AT 0x34EAu /* its jump vector, or the routine itself */
#define X2SPRITE_MAX_WIDTH 320u
#define X2SPRITE_MAX_LINES 200u

typedef struct {
  uint32_t source; /* where the original keeps the sprite */
  int16_t hot_x;
  int16_t hot_y;
  uint16_t width; /* whole 16-pixel groups */
  uint16_t height;
  int16_t box_x; /* the collision box, from the top-left corner */
  int16_t box_y;
  int16_t box_w;
  int16_t box_h;
  uint32_t offset; /* of the pixels, from the image's start */
} x2_sprite_t;

/* The sprites named by the animation lists at `anims` and the single
 * sprites at `singles` (addresses in the original's memory; `src` holds the
 * memory from `base` on), sorted, each once, into `addrs` (room for `max`).
 * `*count` gets how many. X2_OK, X2_ERR_RANGE (more than `max`),
 * X2_ERR_FORMAT (a list that runs on) or a read error. */
int x2sprite_collect(const x2_source_t *src, uint32_t base,
                     const uint32_t *anims, int n_anims,
                     const uint32_t *singles, int n_singles, uint32_t *addrs,
                     int max, int *count);

/* The shape of the sprite at `addr` into `s` (all but the offset). */
int x2sprite_shape(const x2_source_t *src, uint32_t base, uint32_t addr,
                   x2_sprite_t *s);

/* The table of the sprites at `addrs` (sorted), their pixels laid out one
 * after another from `first`; `*end` gets the offset after the last. */
int x2sprite_layout(const x2_source_t *src, uint32_t base,
                    const uint32_t *addrs, int count, x2_sprite_t *table,
                    uint32_t first, uint32_t *end);

/* Write one sprite's pixels, row by row, at `*at` (advanced). */
int x2sprite_pixels(const x2_source_t *src, uint32_t base,
                    const x2_sprite_t *s, const x2_sink_t *out, uint32_t *at);

/* The sprite the original keeps at `source` in a sorted table, or NULL. */
const x2_sprite_t *x2sprite_find(const x2_sprite_t *table, int count,
                                 uint32_t source);

#ifdef __cplusplus
}
#endif

#endif /* X2SPRITE_H */
