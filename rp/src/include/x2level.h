/**
 * File: x2level.h
 * Description: A level converted for drawing at 50 fps: the unpacked level
 *              (LEVELn.X2, as the original holds it at $4F000) turned into an
 *              image of byte-per-pixel graphics the world renderer reads
 *              straight from flash. Plain C, no SDK.
 *
 * The image, in order (offsets from its start, native byte order):
 *   the header (x2level_header_t, 256 bytes), with the palette;
 *   the map: map_rows rows of 20 words, the level's start in the last row.
 *     A word of 0 shows the backdrop; a positive word is an opaque tile, a
 *     negative one a masked tile, at tiles + (word & $7FFF) * 32;
 *   the backdrop: 192 lines of 80 bytes, colours 0-3 four pixels to a
 *     byte, the leftmost in bits 0-1; it repeats every 192 lines. Packed,
 *     it stays in the flash cache while the screen is drawn;
 *   the tiles: each record of the original's tile block at twice its
 *     offset, so the map's words address them unchanged. A tile is 16 x 16
 *     bytes. Opaque: the colour. Masked: the colour in bits 0-3, and in bits
 *     4-5 the planes 0-1 the backdrop shows through where the tile leaves
 *     them (the pixel is (backdrop & bits 4-5) | colour).
 * The header's magic is written last: an image cut short has none.
 */

#ifndef X2LEVEL_H
#define X2LEVEL_H

#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X2LEVEL_MAGIC 0x564C3258u /* "X2LV" */
#define X2LEVEL_VERSION 2u
#define X2LEVEL_BASE 0x4F000u      /* where the original loads a level */
#define X2LEVEL_COLS 20            /* cells across the screen */
#define X2LEVEL_CELL 16            /* a cell is 16 x 16 pixels */
#define X2LEVEL_WIDTH 320
#define X2LEVEL_LINES 192          /* the playfield and the backdrop */
#define X2LEVEL_HEADER_SIZE 256u
#define X2LEVEL_BACKDROP_LINE (X2LEVEL_WIDTH / 4)
#define X2LEVEL_BACKDROP_SIZE (X2LEVEL_BACKDROP_LINE * X2LEVEL_LINES)
#define X2LEVEL_TILE_SIZE 256u     /* 16 x 16 bytes */
#define X2LEVEL_KEEP_SHIFT 4       /* masked tiles: bits 4-5 keep the backdrop */
/* The caller's work buffer for x2level_convert: the planar backdrop. */
#define X2LEVEL_WORK_SIZE (X2LEVEL_COLS * X2LEVEL_LINES * 4u)

typedef struct {
  uint32_t magic; /* X2LEVEL_MAGIC once the image is complete */
  uint32_t version;
  uint32_t level;      /* 1-5 */
  uint32_t source_crc; /* CRC-32 of the unpacked level it was made from */
  uint32_t size;       /* the whole image, header included */
  uint32_t map_offset;
  uint32_t map_rows;
  uint32_t backdrop_offset;
  uint32_t tiles_offset;
  uint32_t tiles_size;
  uint32_t opaque_tiles; /* records converted, for the record */
  uint32_t masked_tiles;
  uint16_t palette[16]; /* ST words, 0RRR0GGG0BBB */
  uint8_t reserved[X2LEVEL_HEADER_SIZE - 12 * 4 - 16 * 2];
} x2level_header_t;

/* The image's size for an unpacked level of `size` bytes whose header gives
 * the block addresses, or 0 if the level's header makes no sense. `head`
 * is the level's first 64 bytes. */
uint32_t x2level_image_size(const uint8_t *head, uint32_t size);

/* Convert the unpacked level `src` (`size` bytes) into the image, written
 * from offset 0 of `out` in order, the magic last. `work` is the caller's,
 * X2LEVEL_WORK_SIZE bytes. X2_OK, X2_ERR_FORMAT or a read/write error. */
int x2level_convert(const x2_source_t *src, uint32_t size, uint32_t level,
                    uint32_t source_crc, const x2_sink_t *out, uint8_t *work,
                    x2_progress_fn progress, void *progress_ctx);

#ifdef __cplusplus
}
#endif

#endif /* X2LEVEL_H */
