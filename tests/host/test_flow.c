// units: rp/src/flow.c rp/src/options.c rp/src/screen.c rp/src/rng.c rp/src/x2screens.c rp/src/x2level.c rp/src/x2sprite.c
/* The screens around the levels on a synthetic screens image laid out like
 * the original's (the zoom tables, the scales' patterns, the texts, a big
 * and a small font whose characters are solid blocks, the logo): the text
 * growing a step at every VBL and drawn as it is, the stars moving at every
 * VBL, the attract left on fire, SELECT's choices and its music switch,
 * "GET READY" waiting for fire and "GAME OVER" not, CONTINUE taken or let
 * run out, the high scores in order with their initials entered with the
 * stick. No game data. */
#include <stdlib.h>
#include <string.h>

#include "fb_chunked.h"
#include "flow.h"
#include "options.h"
#include "palette.h"
#include "test.h"

#define LOW 0x83D4u   /* the tables and texts */
#define LOW_END 0x8F90u
#define HIGH 0x9DC0u  /* the fonts and the logo */
#define HIGH_END 0xE2E0u

uint8_t fb_chunked_buffer[FB_CHUNKED_SIZE];
void palette_set_entry(uint8_t idx, uint16_t color) {
  (void)idx;
  (void)color;
}

static uint8_t game[HIGH_END];
static uint8_t image[sizeof(x2screens_header_t) + (LOW_END - LOW) + (HIGH_END - HIGH)];
static uint8_t global_img[sizeof(x2global_header_t)];
static flow_scores_t scores;
static int songs, stops, fades, effects[16], n_effects;

static void song(void *ctx, int tempo) {
  (void)ctx;
  songs += tempo;
}
static void stop(void *ctx) {
  (void)ctx;
  stops++;
}
static void fade(void *ctx) {
  (void)ctx;
  fades++;
}
static void effect(void *ctx, int e) {
  (void)ctx;
  if (n_effects < 16) effects[n_effects++] = e;
}

static void put16(uint32_t a, uint32_t v) {
  game[a] = (uint8_t)(v >> 8);
  game[a + 1] = (uint8_t)v;
}
static void text(uint32_t a, const char *s) { memcpy(&game[a], s, strlen(s)); }

static void table(uint32_t a, const int16_t *v, int n) {
  for (int i = 0; i < n; i++) put16(a + 2u * (uint32_t)i, (uint16_t)v[i]);
}

static void build(void) {
  memset(game, ' ', sizeof(game));
  static const int16_t credits_zoom[] = {16, -1, 16, -1, 16, -1};
  static const int16_t message[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                    13, 14, 15, 16, 17, 16, -1};
  static const int16_t in[] = {16, -1}, out[] = {0, -1};
  table(0x83D4, credits_zoom, 6);
  table(0x83FE, message, 19);
  table(0x8BEC, in, 2);
  table(0x8C0E, out, 2);
  /* a scale's pattern: its top bits, so scale 8 keeps the first 8 */
  for (int s = 0; s <= 16; s++) put16(0x876C + 2u * (uint32_t)s, (uint16_t)(0xFFFF0000u >> s));
  memcpy(&game[0x871A], "ABCDEFGHIJKLMNOPQRSTUVWXYZ.:><0123456789+", 42);
  game[0x871A + 41] = 0;
  memcpy(&game[0x8744], "ABCDEFGHIJKLMNOPQRSTUVWXYZ.:0123456789", 38);
  game[0x8744 + 38] = 0;
  for (int i = 0; i < 16; i++) put16(0x86FA + 2u * (uint32_t)i, 0x111u * (uint32_t)(i & 7));
  for (int i = 0; i < 8; i++) game[0x867C + i] = 7;
  for (int k = 0; k < 6; k++) text(0x878E + 40u * (uint32_t)k, "AB");
  text(0x887E, " GET READY PLAYER 1 ");
  text(0x8892, "      GAME OVER     ");
  text(0x8C32, "     HIGH SCORE     ");
  text(0x8DC8, "       SELECT       ");
  text(0x8F22, "   CONTINUE GAME    ");
  memcpy(&game[0x8F6A], "CREDITS 3", 10);
  static const uint32_t powers[] = {1000000, 100000, 10000, 1000, 100, 10, 1, 0};
  for (int i = 0; i < 8; i++) {
    put16(0x8BCC + 4u * (uint32_t)i, powers[i] >> 16);
    put16(0x8BCE + 4u * (uint32_t)i, powers[i] & 0xFFFFu);
  }
  /* the fonts: every character a solid block of colour 1 (plane 0) */
  memset(&game[HIGH], 0, HIGH_END - HIGH);
  for (uint32_t a = HIGH; a < 0xB9F0u; a += 8) put16(a, 0xFFFF);
  for (uint32_t a = 0xB9F0u; a < 0xCCF0u; a += 8) put16(a, 0xFFFF);

  x2screens_header_t *h = (x2screens_header_t *)image;
  h->magic = X2SCREENS_MAGIC;
  h->version = X2SCREENS_VERSION;
  h->windows = 2;
  h->window[0] = (x2screens_window_t){LOW, LOW_END - LOW, sizeof(*h)};
  memcpy(image + sizeof(*h), &game[LOW], LOW_END - LOW);
  h->window[1] = (x2screens_window_t){HIGH, HIGH_END - HIGH, sizeof(*h) + (LOW_END - LOW)};
  memcpy(image + sizeof(*h) + (LOW_END - LOW), &game[HIGH], HIGH_END - HIGH);
}

/* `n` VBLs with the stick held, fire pressed at the first. */
static bool run(int n, uint8_t stick, bool fire) {
  bool on = true;
  for (int i = 0; i < n && on; i++) {
    on = flow_vbl(stick, fire && i == 0);
    flow_draw();
  }
  return on;
}

/* Three initials, each the letter it starts on. */
static bool initials(void) {
  run(20, 0, false);
  for (int i = 0; i < 3; i++) run(10, 0, true);
  return !run(300, 0, false);
}

static int lit(int x0, int y0, int w, int h) {
  int n = 0;
  for (int y = y0; y < y0 + h; y++) {
    for (int x = x0; x < x0 + w; x++) n += fb_chunked_buffer[y * FB_CHUNKED_W + x] == 1;
  }
  return n;
}

int main(void) {
  build();
  flow_hooks_t hooks = {NULL, song, stop, fade, effect};
  flow_init((const x2screens_header_t *)image, (const x2global_header_t *)global_img,
            7, &scores, &hooks);
  CHECK_EQ(memcmp(scores.text[0], " 1. 0000000 :::", 15), 0);
  CHECK_EQ(memcmp(scores.text[9], "10. 0000000 :::", 15), 0);

  /* At 50 frames a second: "GET READY" grows at every VBL, not at every
   * original frame (three VBLs or more), and the stars move at every VBL. */
  static uint8_t before[FB_CHUNKED_SIZE];
  flow_get_ready(0);
  int last = 0, grew = 0, moved = 0;
  for (int i = 0; i < 40; i++) {
    memcpy(before, fb_chunked_buffer, sizeof(before));
    flow_vbl(0, false);
    flow_draw();
    int n = lit(0, 0, FB_CHUNKED_W, FB_CHUNKED_H);
    CHECK(n >= last);
    grew += n > last;
    last = n;
    moved += memcmp(before, fb_chunked_buffer, sizeof(before)) != 0;
  }
  CHECK(grew > 30);
  CHECK_EQ(moved, 40);

  /* "GET READY": its text drawn as it is, centred on line 100, 16 x 22 a
   * character; it waits for fire at its middle step. */
  flow_get_ready(0);
  CHECK(run(60, 0, false));
  CHECK_EQ(lit(16 * 1, 92, 16, 22), 16 * 22); /* "G" */
  CHECK_EQ(lit(0, 92, 16, 22), 0);            /* a space */
  CHECK(!run(30, 0, true));

  /* "GAME OVER" goes on its own. */
  flow_game_over();
  CHECK(!run(60, 0, false));

  /* The attract: song 0 at tempo 15; fire leaves it. */
  flow_attract();
  CHECK(run(200, 0, false));
  CHECK_EQ(songs, 15);
  CHECK(!run(400, 0, true));
  CHECK_EQ(stops + fades, 1);

  /* SELECT: down twice to MUSIC, fire switches it and stays; up once to
   * 2 PLAYERS, fire: two players. Each move effect 11, each fire 13. */
  bool music = true;
  flow_select(3, &music);
  CHECK(run(20, 0, false));
  CHECK(run(4, 2, false));
  CHECK(run(4, 0, false));
  CHECK(run(4, 2, false));
  CHECK(run(4, 0, false));
  CHECK(run(10, 0, true));
  CHECK(!music);
  CHECK(run(4, 1, false));
  CHECK(run(4, 0, false));
  CHECK(!run(60, 0, true));
  CHECK_EQ(flow_result(), 2);
  CHECK_EQ(n_effects, 5);
  CHECK_EQ(effects[0], 11);
  CHECK_EQ(effects[2], 13);

  /* The options (not the original's): SELECT's way in, the lines in the
   * small font from line 32, 16 apart, standing until fire; a change shows
   * on the next frame (F3 DAMAGE: "ORIGINAL", then "1"). */
  options_original(&options);
  flow_options();
  CHECK(run(40, 0, false));
  CHECK_EQ(lit(0, 32, 16, 16), 16 * 16);       /* "F" */
  CHECK_EQ(lit(12 * 16, 64, 16, 16), 16 * 16); /* "O" of ORIGINAL */
  options_step(&options, OPTIONS_DAMAGE);
  CHECK(run(10, 0, false));
  CHECK_EQ(lit(12 * 16, 64, 16, 16), 0);
  CHECK(!run(60, 0, true));
  options_original(&options);

  /* CONTINUE: fire takes it (a press before its title is in does not
   * count); nothing lets it run out. */
  flow_continue(2);
  CHECK(run(20, 0, true));
  CHECK(run(20, 0, false));
  CHECK(!run(100, 0, true));
  CHECK_EQ(flow_result(), 1);
  flow_continue(1);
  CHECK(run(100, 0, false));
  CHECK(!run(400, 0, false));
  CHECK_EQ(flow_result(), 0);

  /* The high scores: above the tenth only; in order, a tie below the
   * older; the initials from 'A', right steps, fire takes each. */
  CHECK(!flow_high_score(0));
  CHECK(flow_high_score(500));
  CHECK(run(10, 0, false));
  CHECK(run(4, 8, false)); /* right: 'B' */
  CHECK(run(4, 0, false));
  CHECK(run(10, 0, true)); /* B */
  CHECK(run(10, 0, true)); /* B again: the same letter to start */
  CHECK(!run(200, 0, true));
  CHECK_EQ(scores.score[0], 500);
  CHECK_EQ(memcmp(scores.text[0], " 1. 0000500 BBB", 15), 0);
  CHECK(flow_high_score(700));
  CHECK(initials());
  CHECK_EQ(scores.score[0], 700);
  CHECK_EQ(scores.score[1], 500);
  CHECK_EQ(memcmp(scores.text[1], " 2. 0000500 BBB", 15), 0);
  CHECK_EQ(memcmp(scores.text[0] + 4, "0000700", 7), 0);
  CHECK(flow_high_score(500));
  CHECK(initials());
  CHECK_EQ(scores.score[2], 500); /* the tie below the older */
  CHECK_EQ(memcmp(scores.text[2] + 12, "AAA", 3), 0);
  TEST_END();
}
