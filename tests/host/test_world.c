// units: rp/src/x2level.c rp/src/world.c
/* A synthetic level laid out like the original's (a header of block
 * addresses, a backdrop of 20 planar columns, a map of big-endian words,
 * a tile block of opaque and masked records packed from unit 1) converted
 * by x2level_convert and drawn by world_draw, against a reference that
 * composes every pixel straight from the planar data as the original does.
 * No game data. */
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "world.h"
#include "x2level.h"

#define SIZE 0x8000u
#define BD 0x1000u
#define MAP 0x5000u
#define ROWS 30u
#define PAL (MAP + ROWS * 40u)
#define TILES 0x6000u
#define BASE X2LEVEL_BASE

static uint8_t level[SIZE];
static uint8_t image[0x40000];
static uint32_t image_size;
static int fail_after = -1, writes;
static uint32_t tiles_end;

static void put16(uint32_t at, uint16_t v) {
  level[at] = (uint8_t)(v >> 8);
  level[at + 1] = (uint8_t)v;
}
static void put32(uint32_t at, uint32_t v) {
  put16(at, (uint16_t)(v >> 16));
  put16(at + 2, (uint16_t)v);
}
static uint16_t get16(uint32_t at) {
  return (uint16_t)((level[at] << 8) | level[at + 1]);
}

static int mem_read(void *ctx, uint32_t off, void *buf, uint32_t len) {
  (void)ctx;
  if (off + len > SIZE) return -1;
  memcpy(buf, level + off, len);
  return 0;
}
static int mem_write(void *ctx, uint32_t off, const void *buf, uint32_t len) {
  (void)ctx;
  if (fail_after >= 0 && writes++ >= fail_after) return -1;
  if (off + len > sizeof(image)) return -1;
  memcpy(image + off, buf, len);
  if (off + len > image_size) image_size = off + len;
  return 0;
}

/* Records: id -> masked? Packed from unit 1. */
static const struct { uint32_t id; int masked; } recs[] = {
    {1, 0}, {9, 1}, {21, 0}, {29, 1}, {41, 1}, {53, 0}};
#define RECS (sizeof(recs) / sizeof(recs[0]))

static void build(void) {
  memset(level, 0, sizeof(level));
  put32(0x00, BASE + BD);
  put32(0x04, BASE + MAP);
  put32(0x08, BASE + TILES);
  put32(0x0C, 0x60000010u); /* a code entry: not an address */
  put32(0x1C, BASE + PAL);
  tiles_end = TILES + (53 + 8 + 3) * 16; /* three units of padding */
  put32(0x20, BASE + tiles_end);
  for (uint32_t i = BD; i < BD + 15360; i++) level[i] = (uint8_t)rand();
  for (int i = 0; i < 16; i++) put16(PAL + 2 * i, (uint16_t)(i * 0x111));
  for (uint32_t i = 0; i < RECS; i++) {
    uint32_t at = TILES + recs[i].id * 16;
    for (int line = 0; line < 16; line++) {
      if (recs[i].masked) {
        uint16_t m = (uint16_t)rand(), d0 = (uint16_t)rand() & ~m,
                 d1 = (uint16_t)rand() & ~m, p2 = (uint16_t)rand() & ~m,
                 p3 = (uint16_t)rand() & ~m;
        uint32_t l = at + line * 12;
        put16(l, m);
        put16(l + 2, m);
        put16(l + 4, d0);
        put16(l + 6, d1);
        put16(l + 8, p2);
        put16(l + 10, p3);
      } else {
        for (int w = 0; w < 4; w++) {
          put16(at + line * 8 + w * 2, (uint16_t)(rand() | 0x8001));
        }
      }
    }
  }
  static const uint16_t words[] = {0, 1, 0x8009, 21, 0x801D, 0x8029, 53};
  for (uint32_t r = 0; r < ROWS; r++) {
    for (int c = 0; c < 20; c++) {
      put16(MAP + (r * 20 + c) * 2, words[rand() % 7]);
    }
  }
}

static int bit(uint16_t w, int x) { return (w >> (15 - x)) & 1; }

/* The original's composition, pixel by pixel, from the planar level. */
static uint8_t reference(uint32_t top, uint32_t bline, int y, int px) {
  uint32_t line = top + y, row = line / 16, tl = line % 16;
  int c = px / 16, x = px % 16;
  uint32_t b = BD + c * 768 + ((bline + y) % 192) * 4;
  int b0 = bit(get16(b), x), b1 = bit(get16(b + 2), x);
  uint16_t w = row < ROWS ? get16(MAP + (row * 20 + c) * 2) : 0;
  if (w == 0) return (uint8_t)(b0 | b1 << 1);
  if (!(w & 0x8000)) {
    uint32_t t = TILES + w * 16 + tl * 8;
    return (uint8_t)(bit(get16(t), x) | bit(get16(t + 2), x) << 1 |
                     bit(get16(t + 4), x) << 2 | bit(get16(t + 6), x) << 3);
  }
  uint32_t t = TILES + (w & 0x7FFF) * 16 + tl * 12;
  int p0 = (bit(get16(t), x) & b0) | bit(get16(t + 4), x);
  int p1 = (bit(get16(t + 2), x) & b1) | bit(get16(t + 6), x);
  return (uint8_t)(p0 | p1 << 1 | bit(get16(t + 8), x) << 2 |
                   bit(get16(t + 10), x) << 3);
}

int main(void) {
  srand(7);
  build();
  x2_source_t src = {NULL, mem_read};
  x2_sink_t out = {NULL, mem_write};
  static uint8_t work[X2LEVEL_WORK_SIZE];

  /* A conversion cut short leaves no magic: the world refuses it. */
  memset(image, 0xFF, sizeof(image));
  fail_after = 200;
  writes = 0;
  CHECK(x2level_convert(&src, SIZE, 1, 0x1234, &out, work, NULL, NULL) != 0);
  world_t w;
  static uint16_t map[WORLD_MAP_MAX_ROWS * 20];
  CHECK_EQ(world_init(&w, (const x2level_header_t *)image, map), -1);

  fail_after = -1;
  image_size = 0;
  CHECK_EQ(x2level_convert(&src, SIZE, 1, 0x1234, &out, work, NULL, NULL), 0);
  const x2level_header_t *h = (const x2level_header_t *)image;
  CHECK_EQ(h->magic, X2LEVEL_MAGIC);
  CHECK_EQ(h->size, image_size);
  CHECK_EQ(h->size, x2level_image_size(level, SIZE));
  CHECK_EQ(h->map_rows, ROWS);
  CHECK_EQ(h->source_crc, 0x1234);
  CHECK_EQ(h->opaque_tiles, 3);
  CHECK_EQ(h->masked_tiles, 3);
  CHECK_EQ(h->tiles_size, 2 * (tiles_end - TILES));
  CHECK_EQ(h->palette[5], 0x555);
  CHECK_EQ(world_init(&w, h, map), 0);
  CHECK_EQ(map[3], get16(MAP + 6));

  static uint8_t dst[X2LEVEL_WIDTH * X2LEVEL_LINES];
  static const uint32_t tops[] = {0, 5, 16, 100, 287, 300, ROWS * 16 - 10};
  for (unsigned i = 0; i < sizeof(tops) / sizeof(tops[0]); i++) {
    for (uint32_t bline = 0; bline < 192; bline += 61) {
      memset(dst, 0xEE, sizeof(dst));
      world_draw(&w, tops[i], bline, dst);
      int bad = 0;
      for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 320; x++) {
          if (dst[y * 320 + x] != reference(tops[i], bline, y, x)) bad++;
        }
      }
      CHECK_EQ(bad, 0);
    }
  }

  /* The map is the world's own copy in RAM: a word written there (as the
   * level's code does) is drawn at once. */
  map[0] = 0x8029;
  put16(MAP, 0x8029);
  world_draw(&w, 0, 0, dst);
  int bad = 0;
  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x++) {
      if (dst[y * 320 + x] != reference(0, 0, y, x)) bad++;
    }
  }
  CHECK_EQ(bad, 0);
  TEST_END();
}
