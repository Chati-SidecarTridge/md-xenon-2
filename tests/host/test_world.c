// units: rp/src/x2level.c rp/src/x2sprite.c rp/src/world.c
/* A synthetic level laid out like the original's (a header of block
 * addresses, a backdrop of 20 planar columns, a map of big-endian words,
 * a tile block of opaque and masked records packed from unit 1) converted
 * by x2level_convert and drawn by world_draw, against a reference that
 * composes every pixel straight from the planar data as the original does;
 * the level kept as it is, and the sprites its enemy types' animations and
 * its shot name, found through its wave handler, converted once each.
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

static void sprite(uint32_t at, int hx, int hy, int w, int lines) {
  put16(at, (uint16_t)hx);
  put16(at + 2, (uint16_t)hy);
  put16(at + 4, (uint16_t)w);
  put16(at + 6, (uint16_t)(lines - 1));
  for (int i = 0; i < lines * ((w + 15) / 16) * 5; i++) {
    put16(at + 8 + 2 * (uint32_t)i, (uint16_t)rand());
  }
}

/* A masked sprite's pixel as the original draws it. */
static uint8_t ref_sprite(uint32_t at, int x, int y) {
  int groups = (get16(at + 4) + 15) / 16, g = x / 16, b = x % 16;
  uint32_t line = at + 8 + (uint32_t)(y * groups * 10);
  if ((get16(line + 2 * (4 * groups + g)) >> (15 - b)) & 1) return X2SPRITE_KEY;
  int c = 0;
  for (int k = 0; k < 4; k++) c |= ((get16(line + 2 * (k * groups + g)) >> (15 - b)) & 1) << k;
  return (uint8_t)c;
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
  /* The wave handler (+$10): kind 1's type has two parts, the first
   * animated by a list that loops, the second by one that ends; kind 2
   * has none. The shot's sprite at +$28. */
  put16(0x10, 0x6000);
  put16(0x12, 0x100 - 0x12);
  put32(0x10A + 4, BASE + 0x180);
  put16(0x180, 0x45F9);
  put32(0x182, BASE + 0x200);
  put32(0x200 + 16, BASE + 0x300);
  put16(0x218, 0x108);
  put32(0x218 + 16, BASE + 0x340);
  put32(0x300, BASE + 0x6800);
  put16(0x304, 2);
  put32(0x306, BASE + 0x6900);
  put16(0x30A, 1);
  put32(0x310, X2SPRITE_LOOP);
  put32(0x314, BASE + 0x300);
  put32(0x340, BASE + 0x6900);
  put16(0x344, 3);
  put32(0x34A, 0x34FA);
  put32(0x28, BASE + 0x6A00);
  sprite(0x6800, 3, 1, 16, 4);
  sprite(0x6900, -2, 0, 20, 3);
  sprite(0x6A00, 0, 0, 8, 2);
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
  CHECK(x2level_convert(&src, SIZE, 2, 0x1234, &out, work, NULL, NULL) != 0);
  world_t w;
  static uint16_t map[WORLD_MAP_MAX_ROWS * 20];
  CHECK_EQ(world_init(&w, (const x2level_header_t *)image, map), -1);

  fail_after = -1;
  image_size = 0;
  CHECK_EQ(x2level_convert(&src, SIZE, 2, 0x1234, &out, work, NULL, NULL), 0);
  const x2level_header_t *h = (const x2level_header_t *)image;
  CHECK_EQ(h->magic, X2LEVEL_MAGIC);
  CHECK_EQ(h->size, image_size);
  CHECK_EQ(h->size, x2level_image_size(&src, SIZE, 2, work));
  CHECK_EQ(h->raw_size, SIZE);
  CHECK_EQ(memcmp(image + h->raw_offset, level, SIZE), 0);
  CHECK(x2level_raw(h, BASE + 0x300, 8) == image + h->raw_offset + 0x300);
  CHECK(x2level_raw(h, BASE + SIZE - 2, 4) == NULL);
  CHECK(x2level_raw(h, BASE - 2, 2) == NULL);
  CHECK_EQ(h->sprite_count, 3);
  const x2_sprite_t *sp = x2level_sprites(h);
  static const uint32_t named[] = {0x6800, 0x6900, 0x6A00};
  for (int i = 0; i < 3; i++) {
    CHECK_EQ(sp[i].source, BASE + named[i]);
    int bad = 0;
    for (int y = 0; y < sp[i].height; y++) {
      for (int x = 0; x < sp[i].width; x++) {
        if (image[sp[i].offset + (uint32_t)(y * sp[i].width + x)] != ref_sprite(named[i], x, y)) bad++;
      }
    }
    CHECK_EQ(bad, 0);
  }
  CHECK_EQ(sp[1].width, 32);
  CHECK_EQ(sp[1].hot_x, -2);
  CHECK_EQ(sp[2].offset + 16u * 2u, h->size);
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
