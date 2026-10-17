/**
 * File: x2global.h
 * Description: The graphics every level shares (the ship, its flames, its
 *              bullets, the status bar and its fonts), converted from the
 *              game image (GAME.X2, as the original holds it at $400) into
 *              byte-per-pixel bitmaps the firmware blits with
 *              fb_blit_key. Plain C, no SDK.
 *
 * The image, in order (offsets from its start, native byte order):
 *   the header (x2global_header_t), with a table of X2G_COUNT entries;
 *   each bitmap's pixels, width x height bytes, row by row: a colour 0-15,
 *   or X2G_KEY where the original leaves the screen as it is (X2G_SHIP_HIT
 *   holds 0 and 1).
 * A bitmap is drawn with its top-left corner at (x - hot_x, y - hot_y), as
 * the original draws it from an object's position. The header's magic is
 * written last.
 */

#ifndef X2GLOBAL_H
#define X2GLOBAL_H

#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X2GLOBAL_MAGIC 0x4C473258u /* "X2GL" */
#define X2GLOBAL_VERSION 2u
#define X2G_KEY 0x10u /* transparent: never a colour */

typedef enum {
  X2G_SHIP_LEFT2, /* the ship by bank: -6..-4 */
  X2G_SHIP_LEFT1, /* -3..-1 */
  X2G_SHIP,       /* 0 */
  X2G_SHIP_RIGHT1,
  X2G_SHIP_RIGHT2,
  X2G_JUMP0, /* the jump, smaller and smaller */
  X2G_JUMP1,
  X2G_JUMP2,
  X2G_JUMP3,
  X2G_FLAME0, /* the four flames, two frames each */
  X2G_FLAME0B,
  X2G_FLAME1,
  X2G_FLAME1B,
  X2G_FLAME2,
  X2G_FLAME2B,
  X2G_FLAME3,
  X2G_FLAME3B,
  X2G_BULLET0, /* weapon 1's bullet by power level */
  X2G_BULLET1,
  X2G_BULLET2,
  X2G_SCORE_DIGIT0, /* the score's digits 0-9 */
  X2G_LIVES_DIGIT0 = X2G_SCORE_DIGIT0 + 10, /* the lives' digits 0-9 */
  X2G_STATUS_BAR = X2G_LIVES_DIGIT0 + 10,  /* the status bar's picture */
  X2G_EXPLODE0, /* the ship's explosion, 9 frames */
  X2G_SHIP_HIT = X2G_EXPLODE0 + 9, /* the ship's shape against the scenery:
                                      1 where it collides, 0 elsewhere;
                                      its top-left at (x - 14, y - 14) */
  X2G_COUNT
} x2g_id_t;

typedef struct {
  int16_t hot_x;
  int16_t hot_y;
  uint16_t width;
  uint16_t height;
  uint32_t offset; /* of the pixels, from the image's start */
} x2g_bitmap_t;

typedef struct {
  uint32_t magic; /* X2GLOBAL_MAGIC once the image is complete */
  uint32_t version;
  uint32_t source_crc; /* CRC-32 of the game image it was made from */
  uint32_t size;       /* the whole image */
  x2g_bitmap_t bitmaps[X2G_COUNT];
} x2global_header_t;

/* Convert the game image `game` into the image, written from offset 0 of
 * `out` in order, the magic last. X2_OK, X2_ERR_FORMAT (a sprite that makes
 * no sense) or a read/write error. `*size` (optional) gets the image's
 * size. */
int x2global_convert(const x2_source_t *game, uint32_t source_crc,
                     const x2_sink_t *out, uint32_t *size);

/* The image's size, computed without writing it. 0 on an error. */
uint32_t x2global_image_size(const x2_source_t *game);

#ifdef __cplusplus
}
#endif

#endif /* X2GLOBAL_H */
