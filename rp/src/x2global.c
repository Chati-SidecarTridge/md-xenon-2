/**
 * File: x2global.c
 * Description: The shared graphics converted from the game image
 *              (x2global.h).
 *
 * Where the original keeps them (addresses in the game image, at $400 in
 * the ST's memory) and how it draws them:
 * - masked sprites (the ship, the jump, the flames; draw routine $10A2):
 *   four words (hotspot x, hotspot y, width, lines - 1), then each line
 *   plane-major: plane 0 of every 16-pixel group, planes 1, 2, 3, the mask
 *   (a set bit keeps the screen). Drawn whole groups wide;
 * - bullets (draw routine $1CD6): hotspot x, hotspot y, lines - 1, sixteen
 *   word offsets (one per pixel shift), then each line's planes 0 and 1;
 *   a pixel is drawn where either is set, with plane 2 set and plane 3
 *   clear (colours 5-7). The unshifted copy is converted;
 * - the fonts: 8 x 8 characters of four bytes a line (planes 0-3, written
 *   with movep), every pixel drawn: the score's digits at $2C3A2, the
 *   lives' at $2C1A2 + (digit + 59) x 32;
 * - the status bar: 8 lines of the ST's screen at $2CA42;
 * - the explosion: the masked sprites the ship's death animation ($2F50)
 *   steps through, a frame each;
 * - the sound driver ($2CF42 and its tables, about $2C6C6-$2F111), copied
 *   as it is: the RP runs it in an interpreter (sfx.c);
 * - the ship's shape against the scenery ($387C): 16 pre-shifted masks at
 *   $3515C, each a word then 37 longs: 25 lines of 48 pixels; the first
 *   (no shift) is converted, the others being it shifted right.
 */

#include "x2global.h"

#include <string.h>

#define GAME_BASE 0x400u
#define PICTURE_WIDTH 320u
#define PICTURE_LINES 8u
#define MAX_SOURCE 1280u /* the largest record read at once: the picture */

typedef enum { MASKED, BULLET, GLYPH, PICTURE, HITMASK } kind_t;

typedef struct {
  kind_t kind;
  uint32_t addr;
} source_t;

static const source_t sources[X2G_COUNT] = {
    [X2G_SHIP_LEFT2] = {MASKED, 0x20492},  [X2G_SHIP_LEFT1] = {MASKED, 0x206BE},
    [X2G_SHIP] = {MASKED, 0x208EA},        [X2G_SHIP_RIGHT1] = {MASKED, 0x20B16},
    [X2G_SHIP_RIGHT2] = {MASKED, 0x20D42}, [X2G_JUMP0] = {MASKED, 0x253EE},
    [X2G_JUMP1] = {MASKED, 0x255DE},       [X2G_JUMP2] = {MASKED, 0x25792},
    [X2G_JUMP3] = {MASKED, 0x2590A},       [X2G_FLAME0] = {MASKED, 0x273D0},
    [X2G_FLAME0B] = {MASKED, 0x2741C},     [X2G_FLAME1] = {MASKED, 0x27454},
    [X2G_FLAME1B] = {MASKED, 0x274A0},     [X2G_FLAME2] = {MASKED, 0x2755C},
    [X2G_FLAME2B] = {MASKED, 0x275A8},     [X2G_FLAME3] = {MASKED, 0x274D8},
    [X2G_FLAME3B] = {MASKED, 0x27524},     [X2G_BULLET0] = {BULLET, 0xE4BA},
    [X2G_BULLET1] = {BULLET, 0xED5A},      [X2G_BULLET2] = {BULLET, 0xF99A},
    [X2G_SCORE_DIGIT0] = {GLYPH, 0x2C3A2}, [X2G_LIVES_DIGIT0] = {GLYPH, 0x2C1A2},
    [X2G_STATUS_BAR] = {PICTURE, 0x2CA42},
    [X2G_EXPLODE0] = {MASKED, 0x21882},     [X2G_EXPLODE0 + 1] = {MASKED, 0x21A72},
    [X2G_EXPLODE0 + 2] = {MASKED, 0x21C62}, [X2G_EXPLODE0 + 3] = {MASKED, 0x21EA2},
    [X2G_EXPLODE0 + 4] = {MASKED, 0x22132}, [X2G_EXPLODE0 + 5] = {MASKED, 0x223C2},
    [X2G_EXPLODE0 + 6] = {MASKED, 0x22652}, [X2G_EXPLODE0 + 7] = {MASKED, 0x228E2},
    [X2G_EXPLODE0 + 8] = {MASKED, 0x22B72}, [X2G_SHIP_HIT] = {HITMASK, 0x3515C},
};

#define HIT_WIDTH 48u
#define HIT_LINES 25u

static uint8_t record[MAX_SOURCE];
static uint8_t row[PICTURE_WIDTH];

static uint16_t be16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t source_addr(int id) {
  if (id > X2G_SCORE_DIGIT0 && id < X2G_SCORE_DIGIT0 + 10) {
    return sources[X2G_SCORE_DIGIT0].addr + (uint32_t)(id - X2G_SCORE_DIGIT0) * 32u;
  }
  if (id >= X2G_LIVES_DIGIT0 && id < X2G_LIVES_DIGIT0 + 10) {
    return sources[X2G_LIVES_DIGIT0].addr +
           (uint32_t)(id - X2G_LIVES_DIGIT0 + 59) * 32u;
  }
  return sources[id].addr;
}

static kind_t source_kind(int id) {
  if (id > X2G_SCORE_DIGIT0 && id < X2G_LIVES_DIGIT0 + 10) return GLYPH;
  return sources[id].kind;
}

static int read_at(const x2_source_t *game, uint32_t addr, uint32_t len) {
  if (addr < GAME_BASE || len > sizeof(record)) return X2_ERR_FORMAT;
  return game->read(game->ctx, addr - GAME_BASE, record, len) == 0
             ? X2_OK
             : X2_ERR_READ;
}

/* The bitmap's shape (hotspot, size) from its record. */
static int shape(const x2_source_t *game, int id, x2g_bitmap_t *b) {
  uint32_t addr = source_addr(id);
  int e;
  switch (source_kind(id)) {
    case MASKED:
      if ((e = read_at(game, addr, 8)) != X2_OK) return e;
      b->hot_x = (int16_t)be16(record);
      b->hot_y = (int16_t)be16(record + 2);
      b->width = (uint16_t)((be16(record + 4) + 15u) / 16u * 16u);
      b->height = (uint16_t)(be16(record + 6) + 1u);
      break;
    case BULLET:
      if ((e = read_at(game, addr, 8)) != X2_OK) return e;
      b->hot_x = (int16_t)be16(record);
      b->hot_y = (int16_t)be16(record + 2);
      b->width = 16;
      b->height = (uint16_t)(be16(record + 4) + 1u);
      if ((int16_t)be16(record + 6) < 0) return X2_ERR_FORMAT;
      break;
    case GLYPH:
      b->hot_x = b->hot_y = 0;
      b->width = b->height = 8;
      break;
    case HITMASK:
      b->hot_x = b->hot_y = 14;
      b->width = HIT_WIDTH;
      b->height = HIT_LINES;
      break;
    default:
      b->hot_x = b->hot_y = 0;
      b->width = PICTURE_WIDTH;
      b->height = PICTURE_LINES;
      break;
  }
  if (b->width == 0 || b->width > PICTURE_WIDTH || b->height == 0 ||
      b->height > 64) {
    return X2_ERR_FORMAT;
  }
  return X2_OK;
}

static int put(const x2_sink_t *out, uint32_t *at, const void *buf,
               uint32_t len) {
  if (out->write(out->ctx, *at, buf, len) != 0) return X2_ERR_WRITE;
  *at += len;
  return X2_OK;
}

/* Write one bitmap's pixels, row by row. */
static int pixels(const x2_source_t *game, const x2_sink_t *out, uint32_t *at,
                  int id, const x2g_bitmap_t *b) {
  uint32_t addr = source_addr(id);
  int e;
  switch (source_kind(id)) {
    case MASKED: {
      uint32_t groups = b->width / 16u, line_bytes = groups * 10u;
      for (uint32_t y = 0; y < b->height; y++) {
        if ((e = read_at(game, addr + 8 + y * line_bytes, line_bytes)) != X2_OK) {
          return e;
        }
        for (uint32_t g = 0; g < groups; g++) {
          uint16_t p[5];
          for (int k = 0; k < 5; k++) p[k] = be16(record + 2 * (k * groups + g));
          for (int x = 0; x < 16; x++) {
            uint16_t m = (uint16_t)(0x8000u >> x);
            row[g * 16 + x] =
                (p[4] & m) ? X2G_KEY
                           : (uint8_t)(((p[0] & m) ? 1 : 0) | ((p[1] & m) ? 2 : 0) |
                                       ((p[2] & m) ? 4 : 0) | ((p[3] & m) ? 8 : 0));
          }
        }
        if ((e = put(out, at, row, b->width)) != X2_OK) return e;
      }
      return X2_OK;
    }
    case BULLET: {
      if ((e = read_at(game, addr + 6, 2)) != X2_OK) return e;
      uint32_t data = addr + be16(record);
      if ((e = read_at(game, data, b->height * 4u)) != X2_OK) return e;
      for (uint32_t y = 0; y < b->height; y++) {
        uint16_t p0 = be16(record + 4 * y), p1 = be16(record + 4 * y + 2);
        for (int x = 0; x < 16; x++) {
          uint16_t m = (uint16_t)(0x8000u >> x);
          row[x] = ((p0 | p1) & m) ? (uint8_t)(4u | ((p0 & m) ? 1u : 0u) |
                                               ((p1 & m) ? 2u : 0u))
                                   : X2G_KEY;
        }
        if ((e = put(out, at, row, 16)) != X2_OK) return e;
      }
      return X2_OK;
    }
    case GLYPH: {
      if ((e = read_at(game, addr, 32)) != X2_OK) return e;
      for (uint32_t y = 0; y < 8; y++) {
        const uint8_t *p = record + 4 * y;
        for (int x = 0; x < 8; x++) {
          uint8_t m = (uint8_t)(0x80u >> x);
          row[x] = (uint8_t)(((p[0] & m) ? 1 : 0) | ((p[1] & m) ? 2 : 0) |
                             ((p[2] & m) ? 4 : 0) | ((p[3] & m) ? 8 : 0));
        }
        if ((e = put(out, at, row, 8)) != X2_OK) return e;
      }
      return X2_OK;
    }
    case HITMASK: {
      if ((e = read_at(game, addr, 4)) != X2_OK) return e;
      uint32_t mask = ((uint32_t)be16(record) << 16) | be16(record + 2);
      if ((e = read_at(game, mask, HIT_LINES * 6u)) != X2_OK) return e;
      for (uint32_t y = 0; y < HIT_LINES; y++) {
        for (uint32_t x = 0; x < HIT_WIDTH; x++) {
          row[x] = (uint8_t)((be16(record + y * 6u + (x / 16u) * 2u) >>
                              (15u - x % 16u)) & 1u);
        }
        if ((e = put(out, at, row, HIT_WIDTH)) != X2_OK) return e;
      }
      return X2_OK;
    }
    default: {
      if ((e = read_at(game, addr, PICTURE_LINES * 160u)) != X2_OK) return e;
      for (uint32_t y = 0; y < PICTURE_LINES; y++) {
        for (uint32_t g = 0; g < 20; g++) {
          const uint8_t *w = record + y * 160u + g * 8u;
          uint16_t p[4] = {be16(w), be16(w + 2), be16(w + 4), be16(w + 6)};
          for (int x = 0; x < 16; x++) {
            uint16_t m = (uint16_t)(0x8000u >> x);
            row[g * 16 + x] = (uint8_t)(((p[0] & m) ? 1 : 0) | ((p[1] & m) ? 2 : 0) |
                                        ((p[2] & m) ? 4 : 0) | ((p[3] & m) ? 8 : 0));
          }
        }
        if ((e = put(out, at, row, PICTURE_WIDTH)) != X2_OK) return e;
      }
      return X2_OK;
    }
  }
}

static int layout(const x2_source_t *game, x2global_header_t *h) {
  uint32_t offset = sizeof(*h);
  h->driver_offset = offset;
  offset += X2G_DRIVER_SIZE;
  for (int id = 0; id < X2G_COUNT; id++) {
    int e = shape(game, id, &h->bitmaps[id]);
    if (e != X2_OK) return e;
    h->bitmaps[id].offset = offset;
    offset += (uint32_t)h->bitmaps[id].width * h->bitmaps[id].height;
  }
  h->size = offset;
  return X2_OK;
}

uint32_t x2global_image_size(const x2_source_t *game) {
  x2global_header_t h;
  return layout(game, &h) == X2_OK ? h.size : 0;
}

int x2global_convert(const x2_source_t *game, uint32_t source_crc,
                     const x2_sink_t *out, uint32_t *size) {
  x2global_header_t h;
  memset(&h, 0xFF, sizeof(h)); /* unwritten flash; the magic comes last */
  int e = layout(game, &h);
  if (e != X2_OK) return e;
  h.version = X2GLOBAL_VERSION;
  h.source_crc = source_crc;
  uint32_t at = 0;
  if ((e = put(out, &at, &h, sizeof(h))) != X2_OK) return e;
  for (uint32_t done = 0; done < X2G_DRIVER_SIZE; done += sizeof(row)) {
    uint32_t n = X2G_DRIVER_SIZE - done < sizeof(row) ? X2G_DRIVER_SIZE - done
                                                      : sizeof(row);
    if (game->read(game->ctx, X2G_DRIVER_START + done - GAME_BASE, row, n) != 0) {
      return X2_ERR_READ;
    }
    if ((e = put(out, &at, row, n)) != X2_OK) return e;
  }
  for (int id = 0; id < X2G_COUNT; id++) {
    if ((e = pixels(game, out, &at, id, &h.bitmaps[id])) != X2_OK) return e;
  }
  if (at != h.size) return X2_ERR_FORMAT;
  if (size) *size = h.size;
  uint32_t magic = X2GLOBAL_MAGIC;
  return out->write(out->ctx, 0, &magic, sizeof(magic)) == 0 ? X2_OK
                                                              : X2_ERR_WRITE;
}
