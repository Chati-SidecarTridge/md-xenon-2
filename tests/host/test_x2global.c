// units: rp/src/x2global.c rp/src/x2sprite.c
/* The shared graphics converted from a synthetic game image: masked sprites
 * (one and two groups wide), pre-shifted bullets, the 8 x 8 fonts and the
 * status bar picture placed where the converter reads them, every pixel
 * checked against the original's drawing rules applied directly to the
 * bytes; animation lists (one looping into another, a token's) whose
 * sprites are collected once each, sorted, and converted; the game image's
 * windows kept as they are and found by address; and an image cut short
 * has no magic. No game data. */
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "x2global.h"

#define GAME_SIZE 0x36000u
#define BASE 0x400u

static uint8_t game[GAME_SIZE];
static uint8_t image[0x20000];
static int fail_after = -1, writes;

static void put16(uint32_t addr, uint16_t v) {
  game[addr - BASE] = (uint8_t)(v >> 8);
  game[addr - BASE + 1] = (uint8_t)v;
}
static void put32(uint32_t addr, uint32_t v) {
  put16(addr, (uint16_t)(v >> 16));
  put16(addr + 2, (uint16_t)v);
}
static uint16_t get16(uint32_t addr) {
  return (uint16_t)((game[addr - BASE] << 8) | game[addr - BASE + 1]);
}
static int rd(void *c, uint32_t o, void *b, uint32_t n) {
  (void)c;
  if (o + n > GAME_SIZE) return -1;
  memcpy(b, game + o, n);
  return 0;
}
static int wr(void *c, uint32_t o, const void *b, uint32_t n) {
  (void)c;
  if (fail_after >= 0 && writes++ >= fail_after) return -1;
  if (o + n > sizeof(image)) return -1;
  memcpy(image + o, b, n);
  return 0;
}

static void masked(uint32_t a, int hx, int hy, int w, int lines) {
  put16(a, (uint16_t)hx);
  put16(a + 2, (uint16_t)hy);
  put16(a + 4, (uint16_t)w);
  put16(a + 6, (uint16_t)(lines - 1));
  int groups = (w + 15) / 16;
  for (int i = 0; i < lines * groups * 5; i++) put16(a + 8 + 2 * i, (uint16_t)rand());
}

static void bullet(uint32_t a, int lines) {
  put16(a, 3);
  put16(a + 2, 4);
  put16(a + 4, (uint16_t)(lines - 1));
  put16(a + 6, 38);
  for (int i = 0; i < lines * 2; i++) put16(a + 38 + 2 * i, (uint16_t)rand());
}

/* The original's rules, one pixel at a time. */
static uint8_t ref_masked(uint32_t a, int x, int y) {
  int w = get16(a + 4), groups = (w + 15) / 16, g = x / 16, b = 15 - x % 16;
  uint32_t line = a + 8 + (uint32_t)y * groups * 10;
  if (get16(line + 2 * (4 * groups + g)) >> b & 1) return X2G_KEY;
  int c = 0;
  for (int k = 0; k < 4; k++) c |= (get16(line + 2 * (k * groups + g)) >> b & 1) << k;
  return (uint8_t)c;
}
static uint8_t ref_bullet(uint32_t a, int x, int y) {
  uint32_t line = a + get16(a + 6) + (uint32_t)y * 4;
  int b = 15 - x, p0 = get16(line) >> b & 1, p1 = get16(line + 2) >> b & 1;
  return (p0 | p1) ? (uint8_t)(4 | p0 | p1 << 1) : X2G_KEY;
}
static uint8_t ref_glyph(uint32_t a, int x, int y) {
  int c = 0;
  for (int k = 0; k < 4; k++) c |= (game[a - BASE + y * 4 + k] >> (7 - x) & 1) << k;
  return (uint8_t)c;
}
static uint8_t ref_hit(uint32_t a, int x, int y) {
  (void)a;
  return (uint8_t)(get16(0x3519C + (uint32_t)y * 6 + (uint32_t)(x / 16) * 2) >>
                       (15 - x % 16) & 1);
}
static uint8_t ref_picture(int x, int y) {
  uint32_t w = 0x2CA42 + (uint32_t)y * 160 + (uint32_t)(x / 16) * 8;
  int c = 0;
  for (int k = 0; k < 4; k++) c |= (get16(w + 2 * k) >> (15 - x % 16) & 1) << k;
  return (uint8_t)c;
}

static int check(int id, uint8_t (*ref)(uint32_t, int, int), uint32_t a) {
  const x2global_header_t *h = (const x2global_header_t *)image;
  const x2g_bitmap_t *b = &h->bitmaps[id];
  const uint8_t *px = image + b->offset;
  int bad = 0;
  for (int y = 0; y < b->height; y++) {
    for (int x = 0; x < b->width; x++) {
      if (px[y * b->width + x] != ref(a, x, y)) bad++;
    }
  }
  return bad;
}

int main(void) {
  srand(5);
  static const uint32_t ships[] = {0x20492, 0x206BE, 0x208EA, 0x20B16, 0x20D42};
  for (int i = 0; i < 5; i++) masked(ships[i], 10 + i, 15, i == 2 ? 32 : 24, 27);
  static const uint32_t small[] = {0x253EE, 0x255DE, 0x25792, 0x2590A, 0x273D0,
                                   0x2741C, 0x27454, 0x274A0, 0x2755C, 0x275A8,
                                   0x274D8, 0x27524};
  for (int i = 0; i < 12; i++) masked(small[i], -7 + i, 3 - i, 4 + i % 13, 2 + i % 3); /* within their spacing */
  static const uint32_t boom[] = {0x21882, 0x21A72, 0x21C62, 0x21EA2, 0x22132,
                                  0x223C2, 0x22652, 0x228E2, 0x22B72};
  for (int i = 0; i < 9; i++) masked(boom[i], 15, 15, 32, 24);
  put16(0x3515C, 0x0003);
  put16(0x3515E, 0x519C);
  for (int i = 0; i < 75; i++) put16(0x3519C + 2 * i, (uint16_t)rand());
  bullet(0xE4BA, 6);
  bullet(0xED5A, 8);
  bullet(0xF99A, 10);
  static const uint32_t more_bullets[] = {0xE2E8, 0xEAA8, 0xF5E8, 0xE6C8, 0xF048,
                                          0xFD88, 0xE89A, 0xF2FA, 0x1013A};
  for (int i = 0; i < 9; i++) bullet(more_bullets[i], 5 + i % 3);
  /* the double gun, and the laser's six tail and tip sprites */
  masked(0x2BC3E, 6, 17, 16, 10);
  static const uint32_t laser[] = {0x26800, 0x26880, 0x26900, 0x26980, 0x26A00, 0x26A80};
  for (int i = 0; i < 6; i++) {
    masked(laser[i], 1, 1, 8, 3);
    put32(0x6CA2 + 4 * (uint32_t)i, laser[i]);
  }
  /* the countdown dials' ten sprites */
  for (int i = 0; i < 10; i++) {
    masked(0x26C00 + 0x40 * (uint32_t)i, 0, 0, 16, 4);
    put32(0x6B6C + 4 * (uint32_t)i, 0x26C00 + 0x40 * (uint32_t)i);
  }
  for (uint32_t a = 0x2C1A2; a < 0x2CA42 + 1280; a++) game[a - BASE] = (uint8_t)rand();

  /* The enemies' animations: the capsule's list ($2D30) shows two sprites
   * then loops into a list ($2D60) that shows one of them and a third, then
   * loops back; token 0's list ($3000) shows a fourth and ends. The shots'
   * two sprites are named outright. */
  masked(0x26000, 1, 2, 16, 8);
  put16(0x26000 - 8, 2); put16(0x26000 - 6, 3); put16(0x26000 - 4, 11); put16(0x26000 - 2, 5);
  masked(0x26200, 3, 4, 20, 5);
  masked(0x26400, 0, 0, 8, 3);
  masked(0x26600, -2, 7, 32, 6);
  masked(0x27618, 0, 0, 4, 4);
  masked(0x275E0, 1, 1, 4, 4);
  put32(0x2D30, 0x26200); put16(0x2D34, 2);
  put32(0x2D36, 0x26000); put16(0x2D3A, 2);
  put32(0x2D3C, 0); put32(0x2D40, X2SPRITE_LOOP); put32(0x2D44, 0x2D60);
  put32(0x2D60, 0x26000); put16(0x2D64, 1);
  put32(0x2D66, 0x26400); put16(0x2D6A, 1);
  put32(0x2D6C, 0); put32(0x2D70, X2SPRITE_LOOP); put32(0x2D74, 0x2D30);
  put16(0x3F96, 116); put32(0x3F98, 0x3000);
  put32(0x3000, 0x26600); put16(0x3004, 3);
  put32(0x3006, 0); put32(0x300A, 0x34FA);
  for (uint32_t a = 0x9C80; a < 0x9DC0; a++) game[a - BASE] = (uint8_t)rand();
  for (uint32_t a = 0x5040; a < 0x5300; a++) game[a - BASE] = (uint8_t)rand();

  x2_source_t src = {NULL, rd};
  x2_sink_t out = {NULL, wr};
  fail_after = 3;
  CHECK(x2global_convert(&src, 7, &out, NULL) != 0);
  CHECK(((const x2global_header_t *)image)->magic != X2GLOBAL_MAGIC);

  fail_after = -1;
  uint32_t size = 0;
  CHECK_EQ(x2global_convert(&src, 7, &out, &size), 0);
  const x2global_header_t *h = (const x2global_header_t *)image;
  CHECK_EQ(h->magic, X2GLOBAL_MAGIC);
  CHECK_EQ(h->source_crc, 7);
  CHECK_EQ(h->size, size);
  CHECK_EQ(size, x2global_image_size(&src));
  CHECK_EQ(memcmp(image + h->driver_offset, game + X2G_DRIVER_START - BASE,
                  X2G_DRIVER_SIZE), 0);
  CHECK_EQ(h->bitmaps[X2G_SHIP].width, 32);
  CHECK_EQ(h->bitmaps[X2G_SHIP].height, 27);
  CHECK_EQ(h->bitmaps[X2G_SHIP_LEFT2].width, 32); /* 24 pixels: whole groups */
  CHECK_EQ(h->bitmaps[X2G_SHIP_LEFT2].hot_x, 10);
  CHECK_EQ(h->bitmaps[X2G_FLAME0].hot_x, -7 + 4);
  CHECK_EQ(h->bitmaps[X2G_BULLET2].height, 10);

  for (int i = 0; i < 5; i++) CHECK_EQ(check(X2G_SHIP_LEFT2 + i, ref_masked, ships[i]), 0);
  for (int i = 0; i < 12; i++) CHECK_EQ(check(X2G_JUMP0 + i, ref_masked, small[i]), 0);
  CHECK_EQ(check(X2G_BULLET0, ref_bullet, 0xE4BA), 0);
  CHECK_EQ(check(X2G_BULLET1, ref_bullet, 0xED5A), 0);
  CHECK_EQ(check(X2G_BULLET2, ref_bullet, 0xF99A), 0);
  for (int d = 0; d < 10; d++) {
    CHECK_EQ(check(X2G_SCORE_DIGIT0 + d, ref_glyph, 0x2C3A2 + 32u * d), 0);
    CHECK_EQ(check(X2G_LIVES_DIGIT0 + d, ref_glyph, 0x2C1A2 + 32u * (d + 59)), 0);
  }
  for (int i = 0; i < 9; i++) CHECK_EQ(check(X2G_EXPLODE0 + i, ref_masked, boom[i]), 0);
  CHECK_EQ(h->bitmaps[X2G_SHIP_HIT].width, 48);
  CHECK_EQ(h->bitmaps[X2G_SHIP_HIT].height, 25);
  CHECK_EQ(h->bitmaps[X2G_SHIP_HIT].hot_x, 14);
  CHECK_EQ(check(X2G_SHIP_HIT, ref_hit, 0), 0);
  const x2g_bitmap_t *bar = &h->bitmaps[X2G_STATUS_BAR];
  int bad = 0;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 320; x++) {
      if (image[bar->offset + y * 320 + x] != ref_picture(x, y)) bad++;
    }
  }
  CHECK_EQ(bad, 0);

  /* The sprites, sorted, each converted as the original draws it. */
  static const uint32_t named[] = {0x20492, 0x206BE, 0x208EA, 0x20B16, 0x20D42,
                                   0x26000, 0x26200, 0x26400, 0x26600, 0x26800,
                                   0x26880, 0x26900, 0x26980, 0x26A00, 0x26A80,
                                   0x26C00, 0x26C40, 0x26C80, 0x26CC0, 0x26D00,
                                   0x26D40, 0x26D80, 0x26DC0, 0x26E00, 0x26E40,
                                   0x275E0, 0x27618, 0x2BC3E};
  const x2_sprite_t *sp = x2global_sprites(h);
  CHECK_EQ(h->sprite_count, 28);
  for (int i = 5; i < 28; i++) { /* the ship's (0-4) are checked as its bitmaps */
    CHECK_EQ(sp[i].source, named[i]);
    CHECK(x2sprite_find(sp, (int)h->sprite_count, named[i]) == &sp[i]);
    bad = 0;
    for (int y = 0; y < sp[i].height; y++) {
      for (int x = 0; x < sp[i].width; x++) {
        if (image[sp[i].offset + (uint32_t)y * sp[i].width + (uint32_t)x] !=
            ref_masked(named[i], x, y)) {
          bad++;
        }
      }
    }
    CHECK_EQ(bad, 0);
  }
  CHECK_EQ(sp[0].source, 0x20492);
  CHECK(x2sprite_find(sp, 28, x2global_source(X2G_SHIP)) == &sp[2]);
  CHECK_EQ(sp[5].box_x, 2);
  CHECK_EQ(sp[5].box_y, 3);
  CHECK_EQ(sp[5].box_w, 11);
  CHECK_EQ(sp[5].box_h, 5);
  CHECK_EQ(sp[6].width, 32);
  CHECK_EQ(sp[8].hot_x, -2);
  CHECK(x2sprite_find(sp, (int)h->sprite_count, 0x26002) == NULL);
  CHECK(sp[27].offset + 16u * 10u == h->size);
  for (int i = 0; i < 9; i++) {
    CHECK_EQ(check(X2G_BULLET_LEFT0 + i, ref_bullet, more_bullets[i]), 0);
  }

  /* The windows, read back by address. */
  const uint8_t *raw = x2global_raw(h, 0x9C80, 320);
  CHECK(raw != NULL && memcmp(raw, game + 0x9C80 - BASE, 320) == 0);
  raw = x2global_raw(h, 0x2D60, 24);
  CHECK(raw != NULL && memcmp(raw, game + 0x2D60 - BASE, 24) == 0);
  raw = x2global_raw(h, 0x52FC, 4);
  CHECK(raw != NULL && memcmp(raw, game + 0x52FC - BASE, 4) == 0);
  CHECK(x2global_raw(h, 0x52FC, 5) == NULL);
  CHECK(x2global_raw(h, 0x9000, 2) == NULL);
  TEST_END();
}
