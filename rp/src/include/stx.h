/**
 * File: stx.h
 * Description: Pasti (.stx) floppy images: the track records indexed once,
 *              then any sector's data read on demand, so an image (about
 *              930 KB) never has to be in RAM. Plain C, no SDK.
 */

#ifndef STX_H
#define STX_H

#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STX_MAX_TRACKS 168 /* 84 tracks, two sides */
#define STX_SECTOR_BYTES 512u

typedef struct {
  x2_source_t src;
  uint16_t track_count;
  uint32_t rec_offset[STX_MAX_TRACKS]; /* each track record's file offset */
  uint8_t rec_number[STX_MAX_TRACKS];  /* its track number, bit 7 the side */
} stx_image_t;

/* Index the image's track records. X2_OK, X2_ERR_READ or X2_ERR_FORMAT. */
int stx_open(stx_image_t *img, const x2_source_t *src);

/* A sector's 512 bytes, by physical track, side and sector ID. A sector
 * stored with a CRC error still gives its data; one without data (record
 * not found) or of another size is X2_ERR_FORMAT; a missing track or ID is
 * X2_ERR_RANGE. */
int stx_read_sector(const stx_image_t *img, uint8_t track, uint8_t side,
                    uint8_t id, uint8_t out[STX_SECTOR_BYTES]);

/* 2 when the image has tracks on side 1, else 1. */
int stx_sides(const stx_image_t *img);

/* Side 0, ten sectors a track, numbered from 1: logical sector `lsn` is
 * track lsn / 10, ID lsn % 10 + 1 (the layout of the game's disks). */
int stx_read_logical(const stx_image_t *img, uint32_t lsn,
                     uint8_t out[STX_SECTOR_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* STX_H */
