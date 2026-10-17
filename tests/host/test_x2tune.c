// units: rp/src/x2tune.c
/* The title tune converted from a synthetic boot program laid out like the
 * original's (a section list of counts and addresses ending with a count
 * of 0, samples in two blocks): the sections relative to the samples'
 * start, every sample turned into 127 - the original byte, the magic last;
 * a list that runs past the program refused. No game data. */
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "x2tune.h"

#define BASE 0x40000u
static uint8_t boot[0x28000];
static uint8_t image[0x30000];
static int fail_after = -1, writes;

static void put32(uint32_t addr, uint32_t v) {
  uint8_t *p = boot + (addr - BASE);
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}
static int rd(void *c, uint32_t o, void *b, uint32_t n) {
  (void)c;
  if (o + n > sizeof(boot)) return -1;
  memcpy(b, boot + o, n);
  return 0;
}
static int wr(void *c, uint32_t o, const void *b, uint32_t n) {
  (void)c;
  if (fail_after >= 0 && writes++ >= fail_after) return -1;
  memcpy(image + o, b, n);
  return 0;
}

int main(void) {
  srand(3);
  for (uint32_t a = 0x50000; a < 0x50000 + 3000; a++) boot[a - BASE] = (uint8_t)rand();
  put32(0x41760, 1000);
  put32(0x41764, 0x50000 + 200);  /* the second block's piece first */
  put32(0x41768, 500);
  put32(0x4176C, 0x50000);        /* then the start */
  put32(0x41770, 300);
  put32(0x41774, 0x50000 + 2700);
  put32(0x41778, 0);
  /* the volume table: 8, vA, 9, vB, 10, vC, 0, 0 a sample */
  for (uint32_t b = 0; b < 256; b++) {
    uint8_t *e = boot + (0x41822 - BASE) + 8 * b;
    e[0] = 8;
    e[2] = 9;
    e[4] = 10;
  }
  boot[(0x41822 - BASE) + 1] = boot[(0x41822 - BASE) + 3] = boot[(0x41822 - BASE) + 5] = 15;
  boot[(0x41822 - BASE) + 8 + 1] = 13;                                   /* 13, 0, 0 */
  boot[(0x41822 - BASE) + 16 + 1] = boot[(0x41822 - BASE) + 16 + 5] = 12; /* 12, 0, 12 */
  boot[(0x41822 - BASE) + 24 + 1] = boot[(0x41822 - BASE) + 24 + 3] = 13;
  boot[(0x41822 - BASE) + 24 + 5] = 13;                                  /* 13 x 3 */
  x2_source_t src = {NULL, rd};
  x2_sink_t out = {NULL, wr};

  fail_after = 2;
  memset(image, 0, sizeof(image));
  CHECK(x2tune_convert(&src, 9, &out) != 0);
  CHECK(((const x2tune_header_t *)image)->magic != X2TUNE_MAGIC);

  fail_after = -1;
  CHECK_EQ(x2tune_convert(&src, 9, &out), 0);
  const x2tune_header_t *h = (const x2tune_header_t *)image;
  CHECK_EQ(h->magic, X2TUNE_MAGIC);
  CHECK_EQ(h->source_crc, 9);
  CHECK_EQ(h->sections, 3);
  CHECK_EQ(h->samples, 3000);
  CHECK_EQ(h->size, sizeof(*h) + 3000);
  CHECK_EQ(h->size, x2tune_image_size(&src));
  CHECK_EQ(h->section[0].offset, 200);
  CHECK_EQ(h->section[1].offset, 0);
  CHECK_EQ(h->section[2].count, 300);
  CHECK_EQ(h->section[2].offset, 2700);
  const int8_t *pcm = (const int8_t *)(image + sizeof(*h));
  int bad = 0;
  for (int i = 0; i < 3000; i++) {
    if (pcm[i] != (int8_t)(127 - boot[0x50000 - BASE + i])) bad++;
  }
  CHECK_EQ(bad, 0);
  /* the two volumes nearest the three, by PCM value (127 - byte) + 128 */
  CHECK_EQ(h->ym_pairs[255][0], 15); /* byte 0: 15 x 3, at most 15 + 15 */
  CHECK_EQ(h->ym_pairs[255][1], 15);
  CHECK_EQ(h->ym_pairs[254][0], 0);  /* byte 1: 13 alone */
  CHECK_EQ(h->ym_pairs[254][1], 13);
  CHECK_EQ(h->ym_pairs[253][0], 12); /* byte 2: 12 + 12 as it is */
  CHECK_EQ(h->ym_pairs[253][1], 12);
  CHECK_EQ(h->ym_pairs[252][0], 13); /* byte 3: 13 x 3 = 13 + 15 */
  CHECK_EQ(h->ym_pairs[252][1], 15);
  CHECK_EQ(h->ym_pairs[0][0], 0);    /* byte 255: silence */
  CHECK_EQ(h->ym_pairs[0][1], 0);

  put32(0x41774, 0x40000 + 0x28000 - 10); /* past the program's end */
  CHECK_EQ(x2tune_image_size(&src), 0);
  TEST_END();
}
