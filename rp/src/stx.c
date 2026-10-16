/**
 * File: stx.c
 * Description: Pasti (.stx) floppy image reader (stx.h).
 *
 * A Pasti image is a 16-byte header ("RSY\0", version 3, the track count),
 * then one record per track: a 16-byte descriptor (record size, fuzzy-mask
 * size, sector count, flags, track length, track number with the side in
 * bit 7); with flag bit 0, 16 bytes per sector (data offset, bit position,
 * read time, the ID field, the FDC status), the fuzzy masks, then the track
 * data, where each sector's bytes sit at its data offset; without it, the
 * sectors are 512 bytes each, numbered from 1, right after the descriptor.
 * Integers are little-endian.
 */

#include "stx.h"

#include <string.h>

#define TRK_SECT 0x01u
#define FDC_RNF 0x10u

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

int stx_open(stx_image_t *img, const x2_source_t *src) {
  uint8_t hdr[16];
  memset(img, 0, sizeof(*img));
  img->src = *src;
  if (src->read(src->ctx, 0, hdr, sizeof(hdr)) != 0) return X2_ERR_READ;
  if (memcmp(hdr, "RSY\0", 4) != 0 || le16(hdr + 4) != 3) return X2_ERR_FORMAT;
  uint16_t count = hdr[10];
  if (count > STX_MAX_TRACKS) return X2_ERR_FORMAT;
  uint32_t pos = 16;
  for (uint16_t t = 0; t < count; t++) {
    uint8_t d[16];
    if (src->read(src->ctx, pos, d, sizeof(d)) != 0) return X2_ERR_READ;
    uint32_t size = le32(d);
    if (size < 16) return X2_ERR_FORMAT;
    img->rec_offset[t] = pos;
    img->rec_number[t] = d[14];
    pos += size;
  }
  img->track_count = count;
  return X2_OK;
}

int stx_read_sector(const stx_image_t *img, uint8_t track, uint8_t side,
                    uint8_t id, uint8_t out[STX_SECTOR_BYTES]) {
  const uint8_t number = (uint8_t)(track | (side << 7));
  for (uint16_t t = 0; t < img->track_count; t++) {
    if (img->rec_number[t] != number) continue;
    const uint32_t rec = img->rec_offset[t];
    uint8_t d[16];
    if (img->src.read(img->src.ctx, rec, d, sizeof(d)) != 0) {
      return X2_ERR_READ;
    }
    const uint32_t fuzzy = le32(d + 4);
    const uint16_t sectors = le16(d + 8);
    const uint16_t flags = le16(d + 10);
    if (!(flags & TRK_SECT)) {
      if (id < 1 || id > sectors) return X2_ERR_RANGE;
      return img->src.read(img->src.ctx,
                           rec + 16 + (uint32_t)(id - 1) * STX_SECTOR_BYTES,
                           out, STX_SECTOR_BYTES) == 0
                 ? X2_OK
                 : X2_ERR_READ;
    }
    const uint32_t data_start = rec + 16 + (uint32_t)sectors * 16 + fuzzy;
    for (uint16_t s = 0; s < sectors; s++) {
      uint8_t sd[16];
      if (img->src.read(img->src.ctx, rec + 16 + (uint32_t)s * 16, sd,
                        sizeof(sd)) != 0) {
        return X2_ERR_READ;
      }
      if (sd[10] != id) continue;
      if ((sd[14] & FDC_RNF) || (128u << (sd[11] & 3)) != STX_SECTOR_BYTES) {
        return X2_ERR_FORMAT;
      }
      return img->src.read(img->src.ctx, data_start + le32(sd), out,
                           STX_SECTOR_BYTES) == 0
                 ? X2_OK
                 : X2_ERR_READ;
    }
    return X2_ERR_RANGE;
  }
  return X2_ERR_RANGE;
}

int stx_sides(const stx_image_t *img) {
  for (uint16_t t = 0; t < img->track_count; t++) {
    if (img->rec_number[t] & 0x80) return 2;
  }
  return 1;
}

int stx_read_logical(const stx_image_t *img, uint32_t lsn,
                     uint8_t out[STX_SECTOR_BYTES]) {
  return stx_read_sector(img, (uint8_t)(lsn / 10), 0, (uint8_t)(lsn % 10 + 1),
                         out);
}
