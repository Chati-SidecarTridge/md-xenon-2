/**
 * File: x2screens.h
 * Description: The screens outside play (the shop, the attract, the
 *              menus and messages), converted from the game image (GAME.X2,
 *              as the original holds it from $400) for their ports to read:
 *              the parts of the game image they read, kept as they are (raw
 *              windows, read by address): the shop's strings, tables and
 *              pictures ($10520-$1FB92), the game's own last advice, the
 *              pictures the shop's monitors show for each item (the items'
 *              in-game sprites, from the lists at $10F10), the attract's
 *              and the menus' tables and texts ($83D4-$8F90), their two
 *              fonts and the logo ($9DC0-$E2E0). The shop's sample is the
 *              sound driver's, kept with the driver (x2global.h). Plain C,
 *              no SDK.
 *
 * The pictures stay in the original's formats: raw blocks of 4 interleaved
 * planes (8 bytes per 16 pixels) and masked sprites (a header of hotspot x,
 * y, width and lines - 1, then each line's 16-pixel groups plane by plane,
 * then their mask: 1 keeps what is under it).
 */

#ifndef X2SCREENS_H
#define X2SCREENS_H

#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X2SCREENS_MAGIC 0x50533258u /* "X2SP" */
#define X2SCREENS_VERSION 3u
#define X2SCREENS_WINDOWS 40

typedef struct {
  uint32_t start, size; /* in the game image's address space */
  uint32_t offset;      /* from the image's start */
} x2screens_window_t;

typedef struct {
  uint32_t magic; /* X2SCREENS_MAGIC once the image is complete */
  uint32_t version;
  uint32_t source_crc; /* CRC-32 of the game image it was made from */
  uint32_t size;       /* the whole image */
  uint32_t windows;
  x2screens_window_t window[X2SCREENS_WINDOWS];
} x2screens_header_t;

/* The image's size, or 0 if the game image makes no sense. */
uint32_t x2screens_image_size(const x2_source_t *game);

/* Convert the game image into the screens' image, written from offset 0 of
 * `out` in order, the magic last. */
int x2screens_convert(const x2_source_t *game, uint32_t source_crc,
                   const x2_sink_t *out);

/* `len` bytes of the game image at `addr` as the image keeps them, or NULL
 * when no window holds them. */
const uint8_t *x2screens_raw(const x2screens_header_t *h, uint32_t addr,
                          uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* X2SCREENS_H */
