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
 *   (no shift) is converted, the others being it shifted right;
 * - for the enemies, the game image's windows kept as they are: the
 *   addresses the game hands the levels ($F6E: a shot's sprite, the
 *   direction table, the explosions' and cash's lists...), the animation
 *   lists of the capsule ($2D30), cash ($2E9E, $2E3E), explosions ($2EFE,
 *   $2F2A) and the tokens, the token table ($3F96: a type and a list each,
 *   19), the shots' 16 directions ($4140, $4160), the tokens' routines and
 *   flags ($5040: by token at a pickup; a shot capsule reads it by the
 *   token's type, up to $52F0), the direction table ($9C80: 256 + 64
 *   bytes); for the weapons the pods' places ($2BD0), the homing missiles'
 *   animations and speeds ($4310), the drone's dots ($45C8), the laser's
 *   sprites and beam patterns ($6CA2, $6E22) and the damages ($7AEA); and
 *   every
 *   sprite those lists name (with the weapons' animations), with the
 *   shots' ($27618, $275E0), the ship's (its collision box), the double
 *   gun's ($2BC3E) and the laser's.
 */

#include "x2global.h"

#include <stdlib.h>
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
    [X2G_BULLET_LEFT0] = {BULLET, 0xE2E8}, [X2G_BULLET_LEFT0 + 1] = {BULLET, 0xEAA8},
    [X2G_BULLET_LEFT0 + 2] = {BULLET, 0xF5E8}, [X2G_BULLET_RIGHT0] = {BULLET, 0xE6C8},
    [X2G_BULLET_RIGHT0 + 1] = {BULLET, 0xF048}, [X2G_BULLET_RIGHT0 + 2] = {BULLET, 0xFD88},
    [X2G_BULLET_DOWN0] = {BULLET, 0xE89A}, [X2G_BULLET_DOWN0 + 1] = {BULLET, 0xF2FA},
    [X2G_BULLET_DOWN0 + 2] = {BULLET, 0x1013A},
    [X2G_SCORE_DIGIT0] = {GLYPH, 0x2C3A2}, [X2G_LIVES_DIGIT0] = {GLYPH, 0x2C1A2},
    [X2G_STATUS_BAR] = {PICTURE, 0x2CA42},
    [X2G_EXPLODE0] = {MASKED, 0x21882},     [X2G_EXPLODE0 + 1] = {MASKED, 0x21A72},
    [X2G_EXPLODE0 + 2] = {MASKED, 0x21C62}, [X2G_EXPLODE0 + 3] = {MASKED, 0x21EA2},
    [X2G_EXPLODE0 + 4] = {MASKED, 0x22132}, [X2G_EXPLODE0 + 5] = {MASKED, 0x223C2},
    [X2G_EXPLODE0 + 6] = {MASKED, 0x22652}, [X2G_EXPLODE0 + 7] = {MASKED, 0x228E2},
    [X2G_EXPLODE0 + 8] = {MASKED, 0x22B72}, [X2G_SHIP_HIT] = {HITMASK, 0x3515C},
};

static const x2g_window_t windows[X2G_WINDOWS] = {
    {0xF6E, 0x1C, 0},  {0x2CC0, 0x802, 0}, {0x3F96, 0x8A, 0},
    {0x4140, 0x40, 0}, {0x5040, 0x2C0, 0}, {0x9C80, 0x140, 0},
    {0x2BD0, 0x10, 0}, {0x4310, 0x50, 0}, {0x45C8, 0xB8, 0},
    {0x6CA2, 0x18, 0}, {0x6E22, 0x3C, 0}, {0x7AEA, 0x14, 0},
};

#define TOKEN_TABLE 0x3F96u
#define TOKENS 19
static const uint32_t anim_roots[] = {
    0x2D30, 0x2E9E, 0x2E3E, 0x2EFE, 0x2F2A, /* capsule, cash, explosions */
    0x302A, 0x2D0C, 0x2F86, 0x2D8A, 0x31F2, 0x30A2, 0x30C0, 0x2FFA, 0x2FE2,
    0x30DE, 0x307E, 0x2DDE, 0x348C, 0x3108, 0x2DAE, 0x2D6C, /* weapons */
    0x3102, 0x3120, 0x313E, 0x315C, 0x317A, 0x3198, 0x31B6, 0x31D4};
/* The shots' sprites, the ship's by bank (for its collision box), the
 * double gun's. */
static const uint32_t single_sprites[] = {0x27618, 0x275E0, 0x20492, 0x206BE,
                                          0x208EA, 0x20B16, 0x20D42, 0x2BC3E};
/* The laser's tail and tip sprites by power: two tables of pointers. */
#define LASER_SPRITES 0x6CA2u
#define LASER_SPRITE_COUNT 6

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
    case MASKED: {
      x2_sprite_t s;
      if ((e = x2sprite_shape(game, GAME_BASE, addr, &s)) != X2_OK) return e;
      b->hot_x = s.hot_x;
      b->hot_y = s.hot_y;
      b->width = s.width;
      b->height = s.height;
      break;
    }
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
      x2_sprite_t s = {addr, b->hot_x, b->hot_y, b->width, b->height, b->width, 0, 0, 0, 0, 0, 0};
      return x2sprite_pixels(game, GAME_BASE, &s, out, at);
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

/* The sprites the enemies' animations name, sorted (x2sprite_collect). */
static int sprites(const x2_source_t *game, uint32_t *addrs, int *count) {
  uint32_t roots[sizeof(anim_roots) / 4 + TOKENS];
  int n = 0, e;
  for (; n < (int)(sizeof(anim_roots) / 4); n++) roots[n] = anim_roots[n];
  for (int t = 0; t < TOKENS; t++, n++) {
    if ((e = read_at(game, TOKEN_TABLE + 6u * (uint32_t)t + 2u, 4)) != X2_OK) {
      return e;
    }
    roots[n] = ((uint32_t)be16(record) << 16) | be16(record + 2);
  }
  uint32_t singles[sizeof(single_sprites) / 4 + LASER_SPRITE_COUNT];
  int n_singles = 0;
  for (; n_singles < (int)(sizeof(single_sprites) / 4); n_singles++) {
    singles[n_singles] = single_sprites[n_singles];
  }
  for (int i = 0; i < LASER_SPRITE_COUNT; i++) {
    if ((e = read_at(game, LASER_SPRITES + 4u * (uint32_t)i, 4)) != X2_OK) return e;
    singles[n_singles++] = ((uint32_t)be16(record) << 16) | be16(record + 2);
  }
  return x2sprite_collect(game, GAME_BASE, roots, n, singles, n_singles, addrs,
                          X2G_MAX_SPRITES, count);
}

static int layout(const x2_source_t *game, const uint32_t *addrs, int count,
                  x2global_header_t *h) {
  uint32_t offset = sizeof(*h);
  h->driver_offset = offset;
  offset += X2G_DRIVER_SIZE;
  for (int w = 0; w < X2G_WINDOWS; w++) {
    h->windows[w] = windows[w];
    h->windows[w].offset = offset;
    offset += (windows[w].size + 3u) & ~3u;
  }
  h->sprites_offset = offset;
  h->sprite_count = (uint32_t)count;
  offset += (uint32_t)count * sizeof(x2_sprite_t);
  for (int id = 0; id < X2G_COUNT; id++) {
    int e = shape(game, id, &h->bitmaps[id]);
    if (e != X2_OK) return e;
    h->bitmaps[id].offset = offset;
    offset += (uint32_t)h->bitmaps[id].width * h->bitmaps[id].height;
  }
  for (int i = 0; i < count; i++) {
    x2_sprite_t s;
    int e = x2sprite_shape(game, GAME_BASE, addrs[i], &s);
    if (e != X2_OK) return e;
    offset += (uint32_t)s.width * s.height;
  }
  h->size = offset;
  return X2_OK;
}

uint32_t x2global_image_size(const x2_source_t *game) {
  x2global_header_t h;
  uint32_t *addrs = malloc(X2G_MAX_SPRITES * sizeof(uint32_t));
  int count = 0;
  int ok = addrs && sprites(game, addrs, &count) == X2_OK &&
           layout(game, addrs, count, &h) == X2_OK;
  free(addrs);
  return ok ? h.size : 0;
}

static int convert(const x2_source_t *game, uint32_t source_crc,
                   const x2_sink_t *out, uint32_t *size, uint32_t *addrs) {
  x2global_header_t h;
  int count = 0, e;
  memset(&h, 0xFF, sizeof(h)); /* unwritten flash; the magic comes last */
  if ((e = sprites(game, addrs, &count)) != X2_OK) return e;
  if ((e = layout(game, addrs, count, &h)) != X2_OK) return e;
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
  for (int w = 0; w < X2G_WINDOWS; w++) {
    uint32_t len = (windows[w].size + 3u) & ~3u;
    for (uint32_t done = 0; done < len; done += sizeof(row)) {
      uint32_t n = len - done < sizeof(row) ? len - done : sizeof(row);
      uint32_t real = done + n <= windows[w].size ? n : windows[w].size - done;
      memset(row, 0, n);
      if ((e = read_at(game, windows[w].start + done, real)) != X2_OK) return e;
      memcpy(row, record, real);
      if ((e = put(out, &at, row, n)) != X2_OK) return e;
    }
  }
  uint32_t pixels_at = h.bitmaps[X2G_COUNT - 1].offset +
                       (uint32_t)h.bitmaps[X2G_COUNT - 1].width *
                           h.bitmaps[X2G_COUNT - 1].height;
  for (int i = 0; i < count; i++) {
    x2_sprite_t s;
    if ((e = x2sprite_shape(game, GAME_BASE, addrs[i], &s)) != X2_OK) return e;
    s.offset = pixels_at;
    pixels_at += (uint32_t)s.width * s.height;
    if ((e = put(out, &at, &s, sizeof(s))) != X2_OK) return e;
  }
  for (int id = 0; id < X2G_COUNT; id++) {
    if ((e = pixels(game, out, &at, id, &h.bitmaps[id])) != X2_OK) return e;
  }
  for (int i = 0; i < count; i++) {
    x2_sprite_t s;
    if ((e = x2sprite_shape(game, GAME_BASE, addrs[i], &s)) != X2_OK) return e;
    if ((e = x2sprite_pixels(game, GAME_BASE, &s, out, &at)) != X2_OK) return e;
  }
  if (at != h.size) return X2_ERR_FORMAT;
  if (size) *size = h.size;
  uint32_t magic = X2GLOBAL_MAGIC;
  return out->write(out->ctx, 0, &magic, sizeof(magic)) == 0 ? X2_OK
                                                              : X2_ERR_WRITE;
}

int x2global_convert(const x2_source_t *game, uint32_t source_crc,
                     const x2_sink_t *out, uint32_t *size) {
  uint32_t *addrs = malloc(X2G_MAX_SPRITES * sizeof(uint32_t));
  if (!addrs) return X2_ERR_RANGE;
  int e = convert(game, source_crc, out, size, addrs);
  free(addrs);
  return e;
}

const uint8_t *x2global_raw(const x2global_header_t *h, uint32_t addr,
                            uint32_t len) {
  for (int w = 0; w < X2G_WINDOWS; w++) {
    const x2g_window_t *win = &h->windows[w];
    if (addr >= win->start && addr + len <= win->start + win->size) {
      return (const uint8_t *)h + win->offset + (addr - win->start);
    }
  }
  return NULL;
}

uint32_t x2global_source(int id) { return source_addr(id); }

const x2_sprite_t *x2global_sprites(const x2global_header_t *h) {
  return (const x2_sprite_t *)((const uint8_t *)h + h->sprites_offset);
}
