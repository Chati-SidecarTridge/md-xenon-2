/**
 * File: play.c
 * Description: A level in play (play.h).
 *
 * The world is redrawn every VBL into the chunked buffer's top 192 lines;
 * the original's frames are stepped by the ST's own VBL count
 * (fb_st_vbls), three VBLs each, so a late frame does not slow the world.
 * worldDrawUs and worldDrawMaxUs hold the drawing's cost, readable over
 * SWD. The ship and the status bar come from the store's shared graphics
 * (x2global.h); for now the ship stands at its start and the bar shows the
 * start's values.
 */

#include "play.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "fb.h"
#include "fb_blit.h"
#include "fb_chunked.h"
#include "fb_font.h"
#include "ikbd.h"
#include "palette.h"
#include "pico/time.h"
#include "scroll.h"
#include "store.h"
#include "world.h"
#include "x2game.h"
#include "x2set.h"

extern const struct FB_FONT font8x8; /* fb.c */

#define SCAN_SPACE 0x39
#define MAX_CATCH_UP_VBLS 10 /* after a stall, resume rather than race */
#define READ_CHUNK 2048u
/* The ship's start and the status bar's places and values at the start
 * (the original's level start and status bar routines). */
#define SHIP_START_X 160
#define SHIP_START_Y 176
#define SCORE_X 24     /* player 1; 7 digits, 8 pixels apart */
#define SCORE2_X 240   /* player 2 */
#define SCORE_DIGITS 7
#define LIVES_X 136
#define LIVES_Y 193
#define LIVES_START 3
#define SHIELD_X 91
#define SHIELD_Y 195
#define SHIELD_LINES 3
#define SHIELD_FULL 39
#define SHIELD_COLOUR 14

volatile uint32_t worldDrawUs;
volatile uint32_t worldDrawMaxUs;

static world_t world;
static const x2global_header_t *gfx;
static uint16_t map[WORLD_MAP_MAX_ROWS * X2LEVEL_COLS];
static scroll_t scroll;
static scroll_pace_t pace;
static uint32_t last_vbls;

/* ---- the set's level file, read through a small window ---- */

typedef struct {
  const char *folder;
  const char *name;
  uint32_t size;
  uint32_t base;
  uint32_t len;
  uint8_t buf[READ_CHUNK];
} window_t;

static int window_read(void *ctx, uint32_t offset, void *out, uint32_t len) {
  window_t *w = ctx;
  uint8_t *o = out;
  if (offset + len > w->size) return X2_ERR_READ;
  while (len) {
    if (offset < w->base || offset >= w->base + w->len) {
      w->base = offset / READ_CHUNK * READ_CHUNK;
      w->len = w->size - w->base < READ_CHUNK ? w->size - w->base : READ_CHUNK;
      if (x2set_read(w->folder, w->name, w->base, w->buf, w->len) != X2_OK) {
        w->len = 0;
        return X2_ERR_READ;
      }
    }
    uint32_t n = w->base + w->len - offset;
    if (n > len) n = len;
    memcpy(o, w->buf + (offset - w->base), n);
    o += n;
    offset += n;
    len -= n;
  }
  return X2_OK;
}

static const x2level_header_t *ready_level(const char *folder, uint32_t level,
                                           x2_progress_fn progress,
                                           void *progress_ctx) {
  const x2_known_t *k = &x2_known[X2_OUT_LEVEL1 + level - 1];
  const x2level_header_t *h = store_level(level, k->crc32);
  if (h) return h;
  window_t *w = malloc(sizeof(window_t));
  if (!w) return NULL;
  char name[16];
  snprintf(name, sizeof(name), "LEVEL%lu.X2", (unsigned long)level);
  w->folder = folder;
  w->name = name;
  w->size = k->size;
  w->base = 0;
  w->len = 0;
  x2_source_t src = {w, window_read};
  uint32_t t0 = time_us_32();
  int e = store_write_level(&src, k->size, level, k->crc32, progress,
                            progress_ctx);
  DPRINTF("play: level %lu into the store: %d after %lu ms\n",
          (unsigned long)level, e,
          (unsigned long)((time_us_32() - t0) / 1000));
  free(w);
  return e == X2_OK ? store_level(level, k->crc32) : NULL;
}

/* ---- the shared graphics ---- */

static const x2global_header_t *ready_global(const char *folder,
                                             x2_progress_fn progress,
                                             void *progress_ctx) {
  const x2_known_t *k = &x2_known[X2_OUT_GAME];
  const x2global_header_t *h = store_global(k->crc32);
  if (h) return h;
  window_t *w = malloc(sizeof(window_t));
  if (!w) return NULL;
  w->folder = folder;
  w->name = "GAME.X2";
  w->size = k->size;
  w->base = 0;
  w->len = 0;
  x2_source_t src = {w, window_read};
  int e = store_write_global(&src, k->crc32, progress, progress_ctx);
  free(w);
  return e == X2_OK ? store_global(k->crc32) : NULL;
}

/* A shared bitmap with its hotspot at (x, y), clipped to lines [0, lines). */
static void draw_bitmap(int id, int x, int y, int lines) {
  const x2g_bitmap_t *b = &gfx->bitmaps[id];
  struct FB_BITMAP bm = {b->width, b->height,
                         (const uint8_t *)gfx + b->offset};
  fb_blit_key_band(&bm, x - b->hot_x, y - b->hot_y, X2G_KEY, 0, lines);
}

static void draw_status_bar(void) {
  draw_bitmap(X2G_STATUS_BAR, 0, X2LEVEL_LINES, FB_CHUNKED_H);
  for (int i = 0; i < SCORE_DIGITS; i++) {
    draw_bitmap(X2G_SCORE_DIGIT0, SCORE_X + 8 * i, X2LEVEL_LINES, FB_CHUNKED_H);
    draw_bitmap(X2G_SCORE_DIGIT0, SCORE2_X + 8 * i, X2LEVEL_LINES, FB_CHUNKED_H);
  }
  draw_bitmap(X2G_LIVES_DIGIT0 + LIVES_START, LIVES_X, LIVES_Y, FB_CHUNKED_H);
  fb_fill_rect(SHIELD_X, SHIELD_Y, SHIELD_FULL, SHIELD_LINES, SHIELD_COLOUR);
}

/* ---- the fly-through (debug builds) ---- */

#if defined(_DEBUG) && (_DEBUG != 0)
#define SCAN_UP 0x48
#define SCAN_DOWN 0x50
#define SCAN_F 0x21
#define SCAN_R 0x13
#define SCAN_S 0x1F
#define SCAN_E 0x12
#define SCAN_D 0x20
#define SCAN_V 0x2F
#define ONE_VBL_Q8 256u  /* V: an original frame every VBL */
#define FLY_LINES 4      /* lines a frame while an arrow is held */
#define SHOP_LINE 0x9C0  /* where level 1's code puts the world at its shop */

static bool frozen, show_timing, fly_up, fly_down, every_vbl;

static bool fly_key(uint8_t scancode, bool press) {
  if (scancode == SCAN_UP) fly_up = press;
  else if (scancode == SCAN_DOWN) fly_down = press;
  else if (!press) return false;
  else if (scancode == SCAN_F) frozen = !frozen;
  else if (scancode == SCAN_D) show_timing = !show_timing;
  else if (scancode == SCAN_V) {
    every_vbl = !every_vbl;
    scroll_pace_init(&pace, every_vbl ? ONE_VBL_Q8 : SCROLL_VBLS_PER_FRAME_Q8);
  }
  else if (scancode == SCAN_R) scroll_seek(&scroll, SCROLL_START_LINE);
  else if (scancode == SCAN_S) scroll_seek(&scroll, SHOP_LINE);
  else if (scancode == SCAN_E) scroll_seek(&scroll, 0);
  else return false;
  return true;
}

static void fly_frame(void) {
  if (fly_up) scroll_seek(&scroll, scroll.line - FLY_LINES);
  if (fly_down) scroll_seek(&scroll, scroll.line + FLY_LINES);
}

static void timing(void) {
  if (!show_timing) return;
  char line[41];
  snprintf(line, sizeof(line), "LINE %4ld DRAW %4luUS C2P %4luUS%s",
           (long)scroll.line, (unsigned long)worldDrawUs,
           (unsigned long)fb_last_convert_us(), every_vbl ? " V" : "");
  font_set_font(&font8x8);
  font_set_color(7); /* the level's near-white */
  font_move(0, 0);
  font_print(line);
}
#else
static bool frozen;
static bool fly_key(uint8_t scancode, bool press) {
  (void)scancode;
  (void)press;
  return false;
}
static void fly_frame(void) {}
static void timing(void) {}
#endif

/* ---- the level ---- */

static void draw(void) {
  uint32_t t0 = time_us_32();
  world_draw(&world, (uint32_t)scroll.line, scroll.backdrop,
             fb_chunked_buffer);
  draw_bitmap(X2G_SHIP, SHIP_START_X, SHIP_START_Y, X2LEVEL_LINES);
  worldDrawUs = time_us_32() - t0;
  if (worldDrawUs > worldDrawMaxUs) worldDrawMaxUs = worldDrawUs;
  timing();
}

bool play_start(const char *folder, uint32_t level, x2_progress_fn progress,
                void *progress_ctx) {
  gfx = ready_global(folder, progress, progress_ctx);
  const x2level_header_t *h = ready_level(folder, level, progress,
                                          progress_ctx);
  if (!gfx || !h || world_init(&world, h, map) != 0) return false;
  fb_chunked_clear(0);
  draw_status_bar();
  palette_set(h->palette);
  scroll_start(&scroll);
  scroll_pace_init(&pace, SCROLL_VBLS_PER_FRAME_Q8);
#if defined(_DEBUG) && (_DEBUG != 0)
  every_vbl = false;
#endif
  last_vbls = fb_st_vbls();
  worldDrawMaxUs = 0;
  frozen = false;
  draw();
  return true;
}

bool play_frame(void) {
  bool leave = false;
  ikbd_key_event_t k;
  while (ikbd_pop_key(&k)) {
    if (fly_key(k.scancode, k.is_press)) continue;
    if (k.is_press && k.scancode == SCAN_SPACE) leave = true;
  }
  ikbd_joystick_t j;
  ikbd_read_joystick(1, &j);
  if (j.pressed & IKBD_JOY_FIRE) leave = true;
  if (leave) return false;

  uint32_t now = fb_st_vbls();
  uint32_t vbls = now - last_vbls;
  last_vbls = now;
  if (vbls > MAX_CATCH_UP_VBLS) vbls = MAX_CATCH_UP_VBLS;
  if (!frozen) {
    while (vbls--) {
      for (uint32_t n = scroll_pace(&pace); n; n--) scroll_tick(&scroll, 0, 100);
    }
  }
  fly_frame();
  draw();
  return true;
}
