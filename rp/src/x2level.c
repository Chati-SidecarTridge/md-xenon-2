/**
 * File: x2level.c
 * Description: A level converted for drawing at 50 fps (x2level.h).
 *
 * The original's level, as it sits at $4F000: its first longs are the
 * addresses of its blocks (+$00 the backdrop, +$04 the map, +$08 the tile
 * block, +$1C the palette; the others are code entries and values). The map
 * is 20 big-endian words a row and ends where the next block starts. The
 * backdrop is 20 columns of 192 lines of planes 0-1, 4 bytes a line. The
 * tile block is a run of records from its 16-byte unit 1 to its end, each
 * opaque (16 lines of planes 0-3, a word each: 128 bytes) or masked (16
 * lines of a mask for planes 0-1, planes 0-1, planes 2-3: 192 bytes); a map
 * word is a record's offset / 16, negative for a masked one.
 *
 * A record's kind is in the words that name it, but the level's code names
 * tiles the map does not (a scenery object's cells take other tiles as it
 * comes into view). So the block is walked from its start and each record
 * classified by its content: a masked tile never sets a pixel where its
 * mask keeps the backdrop. On the five levels of the Image Works release
 * this tells every record the map names correctly, and the walk ends at the
 * block's end.
 */

#include "x2level.h"

#include <string.h>

#define HEAD_SIZE 64u   /* the level's header: its first 16 longs */
#define HEAD_LONGS 15u  /* scanned for block addresses */
#define OPAQUE_SIZE 128u
#define MASKED_SIZE 192u
#define UNIT 16u        /* the tile block's granularity */

typedef struct {
  uint32_t map, map_rows, backdrop, tiles, tiles_size, palette;
} layout_t;

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

/* The block that starts after `addr`: the lowest header address above it,
 * or the level's end. */
static uint32_t block_end(const uint8_t *head, uint32_t size, uint32_t addr) {
  uint32_t end = X2LEVEL_BASE + size;
  for (uint32_t i = 0; i < HEAD_LONGS; i++) {
    uint32_t a = be32(head + 4 * i);
    if (a > addr && a < end) end = a;
  }
  return end;
}

static int layout(const uint8_t *head, uint32_t size, layout_t *l) {
  uint32_t top = X2LEVEL_BASE + size;
  uint32_t backdrop = be32(head + 0x00), map = be32(head + 0x04);
  uint32_t tiles = be32(head + 0x08), palette = be32(head + 0x1C);
  if (backdrop < X2LEVEL_BASE || backdrop + X2LEVEL_WORK_SIZE > top ||
      map < X2LEVEL_BASE || map >= top || tiles < X2LEVEL_BASE ||
      tiles >= top || palette < X2LEVEL_BASE || palette + 32 > top) {
    return X2_ERR_FORMAT;
  }
  l->map = map - X2LEVEL_BASE;
  l->map_rows = (block_end(head, size, map) - map) / (X2LEVEL_COLS * 2);
  l->backdrop = backdrop - X2LEVEL_BASE;
  l->tiles = tiles - X2LEVEL_BASE;
  l->tiles_size = (block_end(head, size, tiles) - tiles) / UNIT * UNIT;
  l->palette = palette - X2LEVEL_BASE;
  return l->map_rows && l->tiles_size > UNIT ? X2_OK : X2_ERR_FORMAT;
}

uint32_t x2level_image_size(const uint8_t *head, uint32_t size) {
  layout_t l;
  if (layout(head, size, &l) != X2_OK) return 0;
  return X2LEVEL_HEADER_SIZE + l.map_rows * X2LEVEL_COLS * 2 +
         X2LEVEL_BACKDROP_SIZE + 2 * l.tiles_size;
}

/* A masked record never sets a pixel of planes 0-3 where its mask keeps
 * the backdrop. */
static int looks_masked(const uint8_t *rec) {
  for (int line = 0; line < X2LEVEL_CELL; line++) {
    const uint8_t *p = rec + line * 12;
    uint16_t m0 = be16(p), m1 = be16(p + 2), d0 = be16(p + 4);
    uint16_t d1 = be16(p + 6), p2 = be16(p + 8), p3 = be16(p + 10);
    if ((m0 & d0) || (m1 & d1) || (m0 & p2) || (m0 & p3)) return 0;
  }
  return 1;
}

static void opaque_tile(const uint8_t *rec, uint8_t *out) {
  for (int line = 0; line < X2LEVEL_CELL; line++) {
    const uint8_t *p = rec + line * 8;
    uint16_t p0 = be16(p), p1 = be16(p + 2), p2 = be16(p + 4),
             p3 = be16(p + 6);
    for (int x = 0; x < X2LEVEL_CELL; x++) {
      int b = 15 - x;
      *out++ = (uint8_t)(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) |
                         (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3));
    }
  }
}

static void masked_tile(const uint8_t *rec, uint8_t *out) {
  for (int line = 0; line < X2LEVEL_CELL; line++) {
    const uint8_t *p = rec + line * 12;
    uint16_t m0 = be16(p), m1 = be16(p + 2), d0 = be16(p + 4);
    uint16_t d1 = be16(p + 6), p2 = be16(p + 8), p3 = be16(p + 10);
    for (int x = 0; x < X2LEVEL_CELL; x++) {
      int b = 15 - x;
      uint8_t colour = (uint8_t)(((d0 >> b) & 1) | (((d1 >> b) & 1) << 1) |
                                 (((p2 >> b) & 1) << 2) |
                                 (((p3 >> b) & 1) << 3));
      uint8_t keep = (uint8_t)((((m0 >> b) & 1) | (((m1 >> b) & 1) << 1))
                               << X2LEVEL_KEEP_SHIFT);
      *out++ = colour | keep;
    }
  }
}

static int put(const x2_sink_t *out, uint32_t *at, const void *buf,
               uint32_t len) {
  if (out->write(out->ctx, *at, buf, len) != 0) return X2_ERR_WRITE;
  *at += len;
  return X2_OK;
}

static const uint8_t zeros[128];

static int put_zeros(const x2_sink_t *out, uint32_t *at, uint32_t len) {
  while (len) {
    uint32_t n = len < sizeof(zeros) ? len : sizeof(zeros);
    if (put(out, at, zeros, n) != X2_OK) return X2_ERR_WRITE;
    len -= n;
  }
  return X2_OK;
}

int x2level_convert(const x2_source_t *src, uint32_t size, uint32_t level,
                    uint32_t source_crc, const x2_sink_t *out, uint8_t *work,
                    x2_progress_fn progress, void *progress_ctx) {
  uint8_t head[HEAD_SIZE];
  layout_t l;
  if (size < HEAD_SIZE || src->read(src->ctx, 0, head, HEAD_SIZE) != 0) {
    return X2_ERR_READ;
  }
  if (layout(head, size, &l) != X2_OK) return X2_ERR_FORMAT;

  x2level_header_t h;
  memset(&h, 0xFF, sizeof(h)); /* unwritten flash; the magic comes last */
  h.version = X2LEVEL_VERSION;
  h.level = level;
  h.source_crc = source_crc;
  h.size = x2level_image_size(head, size);
  h.map_offset = X2LEVEL_HEADER_SIZE;
  h.map_rows = l.map_rows;
  h.backdrop_offset = h.map_offset + l.map_rows * X2LEVEL_COLS * 2;
  h.tiles_offset = h.backdrop_offset + X2LEVEL_BACKDROP_SIZE;
  h.tiles_size = 2 * l.tiles_size;
  h.opaque_tiles = 0;
  h.masked_tiles = 0;
  uint8_t pal[32];
  if (src->read(src->ctx, l.palette, pal, sizeof(pal)) != 0) {
    return X2_ERR_READ;
  }
  for (int i = 0; i < 16; i++) h.palette[i] = be16(pal + 2 * i);

  /* Count the records first: the header goes out before them. */
  uint32_t units = l.tiles_size / UNIT;
  uint8_t rec[MASKED_SIZE];
  for (uint32_t pos = 1; pos + OPAQUE_SIZE / UNIT <= units;) {
    int masked = 0;
    if (pos + MASKED_SIZE / UNIT <= units) {
      if (src->read(src->ctx, l.tiles + pos * UNIT, rec, MASKED_SIZE) != 0) {
        return X2_ERR_READ;
      }
      masked = looks_masked(rec);
    }
    if (masked) {
      h.masked_tiles++;
      pos += MASKED_SIZE / UNIT;
    } else {
      h.opaque_tiles++;
      pos += OPAQUE_SIZE / UNIT;
    }
  }

  uint32_t at = 0;
  if (put(out, &at, &h, sizeof(h)) != X2_OK) return X2_ERR_WRITE;

  /* The map, a row at a time, in native byte order. */
  for (uint32_t r = 0; r < l.map_rows; r++) {
    uint8_t raw[X2LEVEL_COLS * 2];
    uint16_t row[X2LEVEL_COLS];
    if (src->read(src->ctx, l.map + r * sizeof(raw), raw, sizeof(raw)) != 0) {
      return X2_ERR_READ;
    }
    for (int c = 0; c < X2LEVEL_COLS; c++) row[c] = be16(raw + 2 * c);
    if (put(out, &at, row, sizeof(row)) != X2_OK) return X2_ERR_WRITE;
  }

  /* The backdrop, from columns of planes 0-1 to lines of packed pixels. */
  if (src->read(src->ctx, l.backdrop, work, X2LEVEL_WORK_SIZE) != 0) {
    return X2_ERR_READ;
  }
  for (uint32_t line = 0; line < X2LEVEL_LINES; line++) {
    uint8_t px[X2LEVEL_BACKDROP_LINE] = {0};
    for (int c = 0; c < X2LEVEL_COLS; c++) {
      const uint8_t *p = work + c * (X2LEVEL_LINES * 4) + line * 4;
      uint16_t p0 = be16(p), p1 = be16(p + 2);
      for (int x = 0; x < X2LEVEL_CELL; x++) {
        int b = 15 - x, i = c * X2LEVEL_CELL + x;
        px[i / 4] |= (uint8_t)((((p0 >> b) & 1) | (((p1 >> b) & 1) << 1))
                               << (2 * (i % 4)));
      }
    }
    if (put(out, &at, px, sizeof(px)) != X2_OK) return X2_ERR_WRITE;
  }

  /* The tiles, each record at twice its offset: unit 0 holds none. */
  if (put_zeros(out, &at, 2 * UNIT) != X2_OK) return X2_ERR_WRITE;
  uint32_t pos = 1, done = 0, total = h.opaque_tiles + h.masked_tiles;
  while (pos + OPAQUE_SIZE / UNIT <= units) {
    uint8_t tile[X2LEVEL_TILE_SIZE];
    uint32_t len = pos + MASKED_SIZE / UNIT <= units ? MASKED_SIZE
                                                     : OPAQUE_SIZE;
    if (src->read(src->ctx, l.tiles + pos * UNIT, rec, len) != 0) {
      return X2_ERR_READ;
    }
    int masked = len == MASKED_SIZE && looks_masked(rec);
    if (masked) {
      masked_tile(rec, tile);
    } else {
      opaque_tile(rec, tile);
    }
    if (put(out, &at, tile, sizeof(tile)) != X2_OK) return X2_ERR_WRITE;
    /* A masked record spans 12 units: 384 bytes here, 256 used. */
    if (masked && put_zeros(out, &at, 2 * (MASKED_SIZE - OPAQUE_SIZE)) !=
                      X2_OK) {
      return X2_ERR_WRITE;
    }
    pos += (masked ? MASKED_SIZE : OPAQUE_SIZE) / UNIT;
    if (progress) progress(progress_ctx, ++done, total);
  }
  if (put_zeros(out, &at, (units - pos) * 2 * UNIT) != X2_OK) {
    return X2_ERR_WRITE;
  }
  if (at != h.size) return X2_ERR_FORMAT;

  uint32_t magic = X2LEVEL_MAGIC;
  return out->write(out->ctx, 0, &magic, sizeof(magic)) == 0 ? X2_OK
                                                              : X2_ERR_WRITE;
}
