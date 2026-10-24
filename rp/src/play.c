/**
 * File: play.c
 * Description: A level in play (play.h).
 *
 * The world is redrawn every VBL into the chunked buffer's top 192 lines;
 * the original's frames are stepped by the ST's own VBL count
 * (fb_st_vbls), three VBLs each, so a late frame does not slow the world.
 * worldDrawUs and worldDrawMaxUs hold the drawing's cost, readable over
 * SWD. Each original frame steps the ship (ship.h), the enemies against it
 * and its bullets (enemies.h), applies its step to the scroll and moves the
 * stars; each VBL draws, in the original's order, the scenery, the stars,
 * the flames, the ship, its bullets, the enemies, their explosions, cash,
 * tokens and shots, and the status bar, from the store's shared graphics
 * (x2global.h) and the level's sprites. The sound: the
 * original driver (sfx.h) ticked every VBL, its sound chip writes sent to
 * the ST through the register ring (ym_ring.h), which owns the chip while
 * the game plays.
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
#include "cart_shared.h"
#include "constants.h"
#include "enemies.h"
#include "weapons.h"
#include "scroll.h"
#include "aconfig.h"
#include "audio.h"
#include "flow.h"
#include "screen.h"
#include "shop.h"
#include "sfx.h"
#include "ship.h"
#include "stars.h"
#include "store.h"
#include "world.h"
#include "x2game.h"
#include "x2set.h"
#include "x2tune.h"
#include "ym_ring.h"

extern const struct FB_FONT font8x8; /* fb.c */

#define MAX_CATCH_UP_VBLS 10 /* after a stall, resume rather than race */
#define SCAN_ESC 0x01          /* in play, as in the original: to the attract */
#define SCAN_P 0x19            /* pause ($7B3E) */
#define SCAN_SPACE 0x39        /* the dive ($7B06) */
/* The status bar's places (the original's status bar routines). */
#define SCORE_X 24     /* player 1; 7 digits, 8 pixels apart */
#define SCORE2_X 240   /* player 2 */
#define SCORE_DIGITS 7
#define LIVES_X 136
#define LIVES_Y 193
#define SHIELD_X 91
#define LIVES2_X 176   /* player 2's lives and shield ($1CAC, $1BFC) */
#define SHIELD2_X 190
#define SHIELD_Y 195
#define SHIELD_LINES 3
#define SHIELD_COLOUR 14
#define RESTART_DEATH_COUNT 8 /* the death counter at a restart ($76DC) */
/* The flames (four objects, $70CC): two at the ship's tail while the stick
 * is up, two at its sides while it is down; each pair closes in as the
 * ship banks ($70B0). */
static const int8_t flame_in[SHIP_BANK_MAX + 1] = {0, 1, 1, 1, 2, 2, 2};

volatile uint32_t worldDrawUs;
volatile uint32_t worldDrawMaxUs;

static world_t world;
static const x2global_header_t *gfx;
static uint16_t map[WORLD_MAP_MAX_ROWS * X2LEVEL_COLS];
static scroll_t scroll;
static scroll_pace_t pace;
static uint32_t last_vbls;
static ship_t ship;
static stars_t stars;
static ship_hitmask_t hit;
static bool fire_pending;
static uint8_t stick;
static int32_t restart_line, restart_x;
static enemies_t *enemies; /* on the heap, made for each level */
static weapons_t *weapons; /* the same: only their save goes on */
static weapons_save_t weapon_save; /* what the next level makes again */
static bool new_game;
static uint32_t score, cash, cash_saved; /* $C92, $C9A, $CA6 */
static uint32_t level_number;
static int loop_count; /* times round the five levels: enemies' hit points */
static bool ship_flash; /* hit this frame: drawn in the flash colour */
static bool cheat;      /* debug builds: hits leave the shield as it is */
static const x2level_header_t *level_h;
static const x2screens_header_t *screens_image; /* NULL: no shops */
static bool shop_asked;   /* $A6C: the level's code wants its mid shop */
static bool shopping;     /* the shop's code runs */
static bool shop_is_end;  /* the level's end shop: the next level after it */
static uint16_t nashwan, shades; /* $CA2, $C90: frames bought in the shop */
static int16_t dive_dial = -1, nashwan_dial = -1; /* their dials, -1 none */

/* The game around the levels: the attract and SELECT, the messages, the
 * high scores and the continues, each a screen of flow.h. */
typedef enum {
  M_ATTRACT, M_SELECT, M_READY, M_FADE, M_PLAY, M_SCORE, M_CONTINUE, M_OVER
} play_mode_t;
static play_mode_t mode = M_PLAY;
static bool music = true;   /* $9FC: SELECT's MUSIC ON / OFF */
static int credits;         /* $CF0: the continues left, plus one */
static int players = 1;     /* $422 */
static bool ready_fades;    /* the message starts a level: a fade in after */
static bool flow_ready;
static bool paused;         /* P ($7B3E) */
static flow_scores_t scores;
static screen_t fade;       /* the level's fade in ($88EA) */

/* Two players: what each keeps ($765A swaps it on every turn): the ship
 * and its numbers, the score, the cash, the weapons saved, the scroll and
 * the restart point, the continues, the shop's advice, and the map words
 * the level's code changed: the words that differ from the store's map,
 * in two lists in the cartridge window's scratch (CART_PLAYERS_OFFSET),
 * the waiting player's and the one being made. Three or more words in a
 * row changed to one value are a run: (its first word | MAP_RUN, the
 * value), then (its length, 0). */
#define MAP_CHANGES 640
#define MAP_RUN 0x8000u
typedef struct {
  uint16_t at, to;
} map_change_t;
typedef struct {
  ship_t ship;
  uint32_t score, cash, cash_saved;
  weapons_save_t saved;
  int16_t invulnerable;
  uint16_t nashwan, shades;
  int credits;
  scroll_t scroll;
  int32_t restart_x, restart_line;
  uint32_t advice[6];
  uint16_t changes;
} player_t;
_Static_assert(SFX_WINDOW_SIZE == CART_SFX_WINDOW_SIZE, "the driver's state");
_Static_assert(2 * MAP_CHANGES * sizeof(map_change_t) <=
                   CART_FRAMEBUFFER_OFFSET - CART_PLAYERS_OFFSET,
               "the players' map changes fit the window's scratch");
static player_t other;       /* the player waiting */
static player_t now;         /* the player playing, while they swap */
static int other_list;       /* which list holds the waiting player's */
static int player;           /* $428: 0 or 1 */
static bool in_game[2];      /* $A03, $A04: still in the game */
static bool finished[2];     /* $A05, $A06: has finished this level */
static uint8_t sample_pairs[256][2]; /* the driver's sample table as pairs */
static const uint8_t *sample_bytes;  /* the driver's sample 0 */
static uint32_t sample_pos, sample_len;
static bool sample_on;

#define FIRE_RATE_MAX 4
#define SPEED_UPS_MAX 2
#define DIVES_A_TOKEN 3
#define FLAME_PLAYING 0x2D0C2u /* the driver's flag while an effect plays */
#define NASHWAN_FRAMES 170    /* the dials' full times ($6C22, $40FE) */
#define DIAL_SPRITES 0x6B6Cu  /* the dials' sprites: ten pointers */
#define DIAL_DIVE_X 152
#define DIAL_DIVE_Y 8
#define DIAL_NASHWAN_X 152
#define DIAL_NASHWAN_Y 160
#define DIAL_RING_X 4
#define DIAL_RING_Y 160

static const x2level_header_t *ready_level(const char *folder, uint32_t level,
                                           x2_progress_fn progress,
                                           void *progress_ctx) {
  const x2_known_t *k = &x2_known[X2_OUT_LEVEL1 + level - 1];
  const x2level_header_t *h = store_level(level, k->crc32);
  if (h) return h;
  x2set_window_t *w = malloc(sizeof(x2set_window_t));
  if (!w) return NULL;
  char name[16];
  snprintf(name, sizeof(name), "LEVEL%lu.X2", (unsigned long)level);
  x2_source_t src = x2set_window(w, folder, name, k->size);
  uint32_t t0 = time_us_32();
  int e = store_write_level(&src, k->size, level, k->crc32, progress,
                            progress_ctx);
  DPRINTF("play: level %lu into the store: %d after %lu ms\n",
          (unsigned long)level, e,
          (unsigned long)((time_us_32() - t0) / 1000));
  free(w);
  return e == X2_OK ? store_level(level, k->crc32) : NULL;
}

/* ---- the shop ---- */

#define SAMPLE_COUNT 0x2F178u  /* the driver's sample 0: its length */
#define SAMPLE_DATA 0x2F182u
#define SAMPLE_TABLE 0x2E880u  /* its three volumes for each byte */
#define SAMPLE_RATE_HZ 10593u  /* Timer D: data 58, / 4 ($2E7D8) */

static const x2screens_header_t *ready_screens(const char *folder,
                                         x2_progress_fn progress,
                                         void *progress_ctx) {
  const x2_known_t *k = &x2_known[X2_OUT_GAME];
  const x2screens_header_t *h = store_screens(k->crc32);
  if (h) return h;
  x2set_window_t *w = malloc(sizeof(x2set_window_t));
  if (!w) return NULL;
  x2_source_t src = x2set_window(w, folder, "GAME.X2", k->size);
  int e = store_write_screens(&src, k->crc32, progress, progress_ctx);
  free(w);
  return e == X2_OK ? store_screens(k->crc32) : NULL;
}

/* ---- the shared graphics ---- */

static const x2global_header_t *ready_global(const char *folder,
                                             x2_progress_fn progress,
                                             void *progress_ctx) {
  const x2_known_t *k = &x2_known[X2_OUT_GAME];
  const x2global_header_t *h = store_global(k->crc32);
  if (h) return h;
  x2set_window_t *w = malloc(sizeof(x2set_window_t));
  if (!w) return NULL;
  x2_source_t src = x2set_window(w, folder, "GAME.X2", k->size);
  int e = store_write_global(&src, k->crc32, progress, progress_ctx);
  free(w);
  return e == X2_OK ? store_global(k->crc32) : NULL;
}

/* Pixels drawn in one colour where they are not transparent: a hit's
 * flash (the original's $1594: planes 0-2 set, plane 3 clear). */
static void blit_flash(const uint8_t *px, int w, int h, int x0, int y0) {
  for (int y = 0; y < h; y++) {
    int sy = y0 + y;
    if (sy < 0 || sy >= X2LEVEL_LINES) continue;
    uint8_t *row = &fb_chunked_buffer[sy * FB_CHUNKED_W];
    for (int x = 0; x < w; x++) {
      int sx = x0 + x;
      if (sx >= 0 && sx < FB_CHUNKED_W && px[y * w + x] != X2SPRITE_KEY) {
        row[sx] = ENEMIES_FLASH;
      }
    }
  }
}

/* A shared bitmap with its hotspot at (x, y), clipped to lines [0, lines). */
static void draw_bitmap(int id, int x, int y, int lines) {
  const x2g_bitmap_t *b = &gfx->bitmaps[id];
  struct FB_BITMAP bm = {b->width, b->height,
                         (const uint8_t *)gfx + b->offset};
  fb_blit_key_band(&bm, x - b->hot_x, y - b->hot_y, X2G_KEY, 0, lines);
}

static void draw_enemy(void *ctx, const x2_sprite_t *sp, const uint8_t *px,
                       int32_t x, int32_t y, bool flash);

/* A countdown dial ($6B94): one of ten sprites by the time left. */
static void draw_dial(int16_t left, int max, int x, int y) {
  if (left < 0) return;
  int k = left * 10 / (max + 1);
  if (k > 9) k = 9; /* the original reads past its table */
  const uint8_t *p = x2global_raw(gfx, DIAL_SPRITES + 4u * (uint32_t)k, 4);
  if (!p) return;
  const x2_sprite_t *sp = x2sprite_find(
      x2global_sprites(gfx), (int)gfx->sprite_count,
      (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]);
  if (sp) draw_enemy(NULL, sp, (const uint8_t *)gfx + sp->offset, x, y, false);
}

static void draw_lives_shield(int lives, int shield, int lives_x, int shield_x) {
  lives = lives < 0 ? 0 : lives > 9 ? 9 : lives;
  draw_bitmap(X2G_LIVES_DIGIT0 + lives, lives_x, LIVES_Y, FB_CHUNKED_H);
  shield = shield < 0 ? 0 : shield;
  fb_fill_rect(shield_x, SHIELD_Y, shield, SHIELD_LINES, SHIELD_COLOUR);
  fb_fill_rect(shield_x + shield, SHIELD_Y, SHIP_SHIELD_FULL - shield,
               SHIELD_LINES, 0);
}

/* The status bar: each player's score, lives and shield in their places
 * (one player: the second's score stays 0). */
static void draw_status_bar(void) {
  draw_bitmap(X2G_STATUS_BAR, 0, X2LEVEL_LINES, FB_CHUNKED_H);
  uint32_t v[2] = {score, 0};
  if (players == 2) {
    v[player] = score;
    v[player ^ 1] = other.score;
  }
  for (int p = 0; p < 2; p++) {
    for (int i = SCORE_DIGITS - 1; i >= 0; i--, v[p] /= 10) {
      draw_bitmap(X2G_SCORE_DIGIT0 + (int)(v[p] % 10), (p ? SCORE2_X : SCORE_X) + 8 * i,
                  X2LEVEL_LINES, FB_CHUNKED_H);
    }
  }
  int p = players == 2 ? player : 0;
  draw_lives_shield(ship.lives, ship.shield, p ? LIVES2_X : LIVES_X, p ? SHIELD2_X : SHIELD_X);
  if (players == 2) {
    draw_lives_shield(other.ship.lives, other.ship.shield, p ? LIVES_X : LIVES2_X,
                      p ? SHIELD_X : SHIELD2_X);
  }
}

static int ship_bitmap(void) {
  return ship.bank <= -4   ? X2G_SHIP_LEFT2
         : ship.bank <= -1 ? X2G_SHIP_LEFT1
         : ship.bank == 0  ? X2G_SHIP
         : ship.bank <= 3  ? X2G_SHIP_RIGHT1
                           : X2G_SHIP_RIGHT2;
}

/* The flames, the ship, its bullets: the original's lists, drawn in order. */
static void draw_ship(void) {
  if (ship.state == SHIP_FLYING && ship.dive_frame) { /* the dive */
    int id = X2G_JUMP0 + ship.dive_frame - 1;
    const x2g_bitmap_t *b = &gfx->bitmaps[id];
    if (ship.dive_clipped) {
      world_draw_under(&world, (uint32_t)scroll.line,
                       (const uint8_t *)gfx + b->offset, b->width, b->height,
                       ship.x - b->hot_x, ship.y - b->hot_y, X2G_KEY,
                       fb_chunked_buffer);
    } else {
      draw_bitmap(id, ship.x, ship.y, X2LEVEL_LINES);
    }
  } else if (ship.state == SHIP_FLYING) {
    int in = flame_in[ship.bank < 0 ? -ship.bank : ship.bank];
    int odd = (int)(ship.frames & 1u); /* each flame alternates two frames */
    if (ship.stick & SCROLL_STICK_UP) {
      draw_bitmap(X2G_FLAME3 + odd, ship.x - in, ship.y, X2LEVEL_LINES);
      draw_bitmap(X2G_FLAME2 + odd, ship.x + in, ship.y, X2LEVEL_LINES);
    }
    if (ship.stick & SCROLL_STICK_DOWN) {
      draw_bitmap(X2G_FLAME1 + odd, ship.x - in, ship.y, X2LEVEL_LINES);
      draw_bitmap(X2G_FLAME0 + odd, ship.x + in, ship.y, X2LEVEL_LINES);
    }
    int frame = ship_bitmap();
    if (ship_flash) {
      const x2g_bitmap_t *b = &gfx->bitmaps[frame];
      blit_flash((const uint8_t *)gfx + b->offset, b->width, b->height,
                 ship.x - b->hot_x, ship.y - b->hot_y);
    } else {
      draw_bitmap(frame, ship.x, ship.y, X2LEVEL_LINES);
    }
  } else if (ship.state == SHIP_EXPLODING &&
             ship.explosion < SHIP_EXPLOSION_FRAMES) {
    draw_bitmap(X2G_EXPLODE0 + ship.explosion, ship.x, ship.y, X2LEVEL_LINES);
  }
}

/* The boss's block of tiles (enemies_draw). */
static void draw_block(void *ctx, const uint16_t *words, int cols, int rows,
                       int32_t x, int32_t y, bool flash) {
  (void)ctx;
  world_draw_block(&world, words, cols, rows, x, y, flash, ENEMIES_FLASH,
                   fb_chunked_buffer);
}

/* An enemy, an explosion, cash, a token or a shot (enemies_draw). */
static void draw_enemy(void *ctx, const x2_sprite_t *sp, const uint8_t *px,
                       int32_t x, int32_t y, bool flash) {
  (void)ctx;
  if (flash) {
    blit_flash(px, sp->width, sp->height, x - sp->hot_x, y - sp->hot_y);
    return;
  }
  struct FB_BITMAP bm = {sp->width, sp->height, px};
  fb_blit_key_band(&bm, x - sp->hot_x, y - sp->hot_y, X2SPRITE_KEY, 0,
                   X2LEVEL_LINES);
}

/* An enemy behind the scenery ($1912): its pixels where the backdrop shows. */
static void draw_behind(void *ctx, const x2_sprite_t *sp, const uint8_t *px,
                        int32_t x, int32_t y, bool flash) {
  (void)ctx;
  (void)flash;
  int32_t x0 = x - sp->hot_x, y0 = y - sp->hot_y;
  for (int r = 0; r < sp->height; r++) {
    int32_t yy = y0 + r;
    if (yy < 0 || yy >= X2LEVEL_LINES) continue;
    for (int c = 0; c < sp->width; c++) {
      int32_t xx = x0 + c;
      uint8_t p = px[r * sp->width + c];
      if (p == X2SPRITE_KEY || xx < 0 || xx >= FB_CHUNKED_W) continue;
      if (world_solid(&world, (uint32_t)scroll.line, xx, yy)) continue;
      fb_chunked_buffer[yy * FB_CHUNKED_W + xx] = p;
    }
  }
}

/* An enemy's beam (enemies_set_strip): the colours on every line. */
static void draw_strip(void *ctx, int32_t x, int32_t y, int32_t height,
                       const uint8_t *colors, int width) {
  (void)ctx;
  for (int32_t yy = y < 0 ? 0 : y; yy < y + height && yy < X2LEVEL_LINES; yy++) {
    uint8_t *row = &fb_chunked_buffer[yy * FB_CHUNKED_W];
    for (int c = 0; c < width; c++) {
      int32_t xx = x + c;
      if (colors[c] && xx >= 0 && xx < FB_CHUNKED_W) row[xx] = colors[c];
    }
  }
}

/* A bullet's bitmap, the laser's beam, a drone's dot (weapons_draw_shots). */
static void draw_bullet(void *ctx, int id, int32_t x, int32_t y) {
  (void)ctx;
  draw_bitmap(id, x, y, X2LEVEL_LINES);
}

/* The beam: the 16-pixel row of a mask word and four plane words, on every
 * line from y up to y - length ($6CBA). */
static void draw_beam(void *ctx, const uint8_t *pattern, int32_t x,
                      int32_t y, int32_t length) {
  (void)ctx;
  uint16_t mask = (uint16_t)(pattern[0] << 8 | pattern[1]);
  uint16_t p[4];
  for (int k = 0; k < 4; k++) p[k] = (uint16_t)(pattern[4 + 4 * k] << 8 | pattern[5 + 4 * k]);
  for (int32_t line = y; line >= y - length && line >= 0; line--) {
    if (line >= X2LEVEL_LINES) continue;
    uint8_t *row = &fb_chunked_buffer[line * FB_CHUNKED_W];
    for (int b = 0; b < 16; b++) {
      int32_t sx = x + b;
      uint16_t m = (uint16_t)(0x8000u >> b);
      if (sx < 0 || sx >= FB_CHUNKED_W || (mask & m)) continue;
      row[sx] = (uint8_t)(((p[0] & m) ? 1 : 0) | ((p[1] & m) ? 2 : 0) |
                          ((p[2] & m) ? 4 : 0) | ((p[3] & m) ? 8 : 0));
    }
  }
}

static void draw_dot(void *ctx, int32_t x, int32_t y) { /* 3 x 2 */
  (void)ctx;
  for (int dy = 0; dy < 2; dy++) {
    if (y + dy < 0 || y + dy >= X2LEVEL_LINES) continue;
    for (int dx = 0; dx < 3; dx++) {
      if (x + dx < 0 || x + dx >= FB_CHUNKED_W) continue;
      fb_chunked_buffer[(y + dy) * FB_CHUNKED_W + x + dx] =
          dx == 1 ? WEAPONS_DOT_CENTRE : WEAPONS_DOT_COLOUR;
    }
  }
}

/* ---- the sound ---- */

static volatile uint16_t *ym_ring_words(void) {
  return (volatile uint16_t *)((uint8_t *)__rom_in_ram_start__ +
                               CART_YM_RING_OFFSET);
}

static void ym_mode(uint16_t mode) {
  *(volatile uint16_t *)((uint8_t *)__rom_in_ram_start__ +
                         CART_YM_MODE_OFFSET) = mode;
}

static void send(const sfx_write_t *w, int n) {
  volatile uint16_t *ring = ym_ring_words();
  for (int i = 0; i < n; i++) ym_ring_push(ring, w[i].reg, w[i].value);
}

static void sound_init(void) {
  sfx_init((const uint8_t *)gfx + gfx->driver_offset,
           (uint8_t *)__rom_in_ram_start__ + CART_SFX_WINDOW_OFFSET);
  ym_mode(CART_YM_MODE_REGISTERS);
}

/* Song 0 at `tempo` ($2CF42): 12 in play, 15 in the attract. */
static void sound_song(int tempo) {
  sfx_write_t w[SFX_WRITES_MAX];
  ym_mode(CART_YM_MODE_REGISTERS);
  send(w, sfx_song(SFX_SONG_LEVEL, tempo, w));
}

static void sound_vbl(void) {
  sfx_write_t w[SFX_WRITES_MAX];
  send(w, sfx_vbl(w));
}

static void sound_stop(void) {
  sfx_write_t w[SFX_WRITES_MAX];
  send(w, sfx_stop(w));
  ym_mode(CART_YM_MODE_SAMPLES);
}

/* The driver's sample 0, as the shop plays it when Crispin's window opens:
 * PCM on the DMA chip, the driver's three volumes as two on the YM. */
static void sample_fill(int8_t *buf, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    buf[i] = sample_pos < sample_len ? (int8_t)(127 - sample_bytes[sample_pos++]) : 0;
  }
}

static void sample_play(bool on) {
  const uint8_t *drv = (const uint8_t *)gfx + gfx->driver_offset;
  if (on && !sample_on) {
    const uint8_t *n = drv + SAMPLE_COUNT - X2G_DRIVER_START;
    sample_len = (uint32_t)n[0] << 24 | (uint32_t)n[1] << 16 | (uint32_t)n[2] << 8 | n[3];
    if (sample_len > X2G_DRIVER_SIZE - (SAMPLE_DATA - X2G_DRIVER_START)) return;
    sample_bytes = drv + SAMPLE_DATA - X2G_DRIVER_START;
    sample_pos = 0;
    x2tune_pairs(drv + SAMPLE_TABLE - X2G_DRIVER_START, sample_pairs);
    audio_set_pcm_callback(sample_fill, SAMPLE_RATE_HZ);
    audio_set_pcm_ym_pairs(sample_pairs);
    if (!audio_uses_dma()) ym_mode(CART_YM_MODE_SAMPLES);
    sample_on = true;
  } else if (!on && sample_on) {
    audio_set_fill_callback(NULL);
    ym_mode(CART_YM_MODE_REGISTERS);
    sample_on = false;
  }
}

static void hook_take(void *ctx, int token);
static void hook_stop(void *ctx) {
  (void)ctx;
  sfx_write_t w[SFX_WRITES_MAX];
  send(w, sfx_stop(w)); /* the chip stays the driver's: the shop's effects */
}
static void hook_sample(void *ctx, bool on) {
  (void)ctx;
  sample_play(on);
}
static void draw(void);
static void start_ready(bool level_start);
static void hook_redraw(void *ctx) {
  (void)ctx;
  draw(); /* the playfield back before the shop fades it in */
}

void play_sound_reset(void) {
  ym_ring_reset(ym_ring_words());
  ym_mode(CART_YM_MODE_SAMPLES);
}

/* ---- the game, an original frame at a time ---- */

static bool solid_at(void *ctx, int32_t x, int32_t y) {
  (void)ctx;
  return world_solid(&world, (uint32_t)scroll.line, x, y);
}

/* The ship's explosion on both channels, as the original asks it ($6548). */
static void explosion_sound(void) {
  sfx_request(15, SFX_CHANNEL_A);
  sfx_request(15, SFX_CHANNEL_B);
}

/* A token taken (the original's $5040 routines): the ship's own, the
 * weapons', the smart bomb. */
static void take_token(int token, enemies_events_t *ev) {
  switch (token) {
    case TOKEN_SPEEDUP:
      if (ship.speed_ups < SPEED_UPS_MAX) ship.speed_ups++;
      break;
    case TOKEN_AUTOFIRE:
      if (ship.fire_rate < FIRE_RATE_MAX) ship.fire_rate++;
      break;
    case TOKEN_HEALTH1:
    case TOKEN_HEALTH2:
      ship.shield += token == TOKEN_HEALTH1 ? 20 : 40;
      if (ship.shield > SHIP_SHIELD_FULL) ship.shield = SHIP_SHIELD_FULL;
      break;
    case TOKEN_DIVE:
      ship.dives += DIVES_A_TOKEN;
      break;
    case TOKEN_SMART_BOMB:
      enemies_bomb(enemies, ev);
      break;
    default:
      weapons_take(weapons, token);
      break;
  }
}

static void hook_take(void *ctx, int token) {
  (void)ctx;
  enemies_events_t ev = {0};
  take_token(token, &ev);
}

/* The weapons' view of the frame. */
static weapons_frame_t weapons_view(void) {
  weapons_frame_t wf = {0};
  wf.shot = ship.shot_request;
  wf.held = (stick & SHIP_STICK_FIRE) != 0;
  wf.stick = stick;
  wf.x = ship.x;
  wf.y = ship.y;
  wf.before_x = ship.history[1].x;
  wf.before_y = ship.history[1].y;
  wf.trail_x = ship.history[4].x;
  wf.trail_y = ship.history[4].y;
  const x2g_bitmap_t *b = &gfx->bitmaps[ship_bitmap()];
  wf.ship_centre_x = (int16_t)((2 * (ship.x - b->hot_x) + b->width) >> 1);
  wf.ship_centre_y = (int16_t)((2 * (ship.y - b->hot_y) + b->height - 1) >> 1);
  wf.scatter = (int16_t)ship.death_count;
  wf.diving = ship.dive != 0;
  wf.dying = ship.state != SHIP_FLYING;
  wf.flame_sound_done = sfx_peek(FLAME_PLAYING) == 0;
  return wf;
}

/* The frame's timers ($6BA8): Bitmap Shades (the level's palette back at
 * the end), the dive's dial, Super Nashwan's count and dial. */
static void timers(bool dive_timed) {
  if (shades && --shades == 0) palette_set(level_h->palette);
  dive_dial = dive_timed ? (int16_t)ship.dive_time : -1;
  nashwan_dial = -1;
  if (nashwan) {
    nashwan_dial = (int16_t)--nashwan;
    if (!nashwan) weapons_nashwan_end(weapons);
  }
}

static void game_frame(void) {
  enemies_events_t ev = {0};
  enemies_frame_start(enemies);
  int32_t jump = enemies_level_frame(enemies, scroll.line, scroll.step, ship.y,
                                     &scroll.back_limit);
  if (jump >= 0) scroll_jump(&scroll, jump); /* the world past a stretch */
  if (enemies->drift) { /* the world held: the backdrop and the stars go on */
    scroll_backdrop(&scroll, enemies->drift);
    stars_move(&stars, enemies->drift);
    enemies->drift = 0;
  }
  bool dive_timed = ship.dive_time != 0;
  int32_t step = ship_tick(&ship, stick, fire_pending, &scroll, &hit, solid_at,
                           NULL);
  fire_pending = false;
  ship.invulnerable = weapons_invulnerable(weapons);
  if (ship.died) weapons_death(weapons); /* $657E */
  /* The objects see the world as it stood this frame; the scroll moves
   * after them. Nothing touches a diving ship. */
  enemies_frame_t f = {scroll.line, scroll.last_step, scroll.back_limit,
                       ship.state == SHIP_FLYING && !ship.dive, ship.x,
                       ship.y, {0}, shades != 0, ship.invulnerable};
  if (!f.ship_flying || !enemies_box(enemies, x2global_source(ship_bitmap()),
                                     ship.x, ship.y, f.ship_box)) {
    f.ship_flying = false;
  }
  weapons_frame_t wf = weapons_view();
  enemies_ship_body(enemies, &f, &ev);
  enemies_update(enemies, &f, &ev);
  weapons_update(weapons, &wf, &ev);
  enemies_update_fx(enemies, &f, &ev);
  weapons_update_shots(weapons, &wf, &ev);
  enemies_spawn(enemies, &f, &ev);
  timers(dive_timed);
  for (int i = 0; i < ev.ntokens; i++) take_token(ev.tokens[i], &ev);
  score += (uint32_t)ev.score;
  cash += (uint32_t)ev.cash;
  if (ev.restart) { /* $3D86: the place and the weapons kept */
    restart_x = ev.restart_x;
    restart_line = ev.restart_line;
    cash_saved = cash;
    if (!nashwan) weapons_save(weapons); /* $3D9C */
  }
  if (ev.back_limit >= 0 && scroll.back_limit > ev.back_limit) {
    scroll.back_limit = ev.back_limit; /* the boss holds the world */
  }
  ship_flash = ev.ship_flash;
  if (ev.shield_damage && !cheat && !enemies_ship_safe(enemies)) {
    ship_hit(&ship, ev.shield_damage);
  }
  ship.fire_period = weapons_fire_period(weapons);
  /* The sounds, asked as the original asks them. */
  for (int i = 0; i < ev.nsounds; i++) {
    int ch = ev.sounds[i].channel;
    int fx = ev.sounds[i].effect;
    if ((fx == 2 || fx == 6) && ch == 2) { /* a bullet ($6990), a boss moving */
      sfx_request_if_free(fx, SFX_CHANNEL_A);
      continue;
    }
    sfx_request(ev.sounds[i].effect, ch == 2   ? SFX_CHANNEL_A
                                     : ch == 1 ? SFX_CHANNEL_B
                                               : SFX_CHANNEL_C);
  }
  if (ship.dive_sound) {
    sfx_request(19, SFX_CHANNEL_A);
    ship.dive_sound = false;
  }
  if (weapons->flame_stop || ev.stop_effect) {
    sfx_write_t w[SFX_WRITES_MAX];
    send(w, sfx_stop_effect(w));
    weapons->flame_stop = false;
  }
  if (ship.died) explosion_sound();
  /* What the level's code asks of the scroll ($CE8, $CCC, $CEA) and the
   * shop it asks for ($A6C: at the frame's end). */
  scroll.lowest = enemies->lowest;
  if (enemies->speed) {
    scroll.speed = enemies->speed;
    enemies->speed = 0;
  }
  if (scroll.back_limit < enemies->back_floor) scroll.back_limit = enemies->back_floor;
  if (enemies_shop_wanted(enemies)) shop_asked = true;
  scroll_advance(&scroll, step, stick);
  stars_move(&stars, scroll.last_step);
}

/* A death taken: a life lost, the shield and fire rate refilled ($7C50).
 * False at the game's end. */
static bool life_lost(void) {
  ship.lives--;
  ship.shield = SHIP_SHIELD_FULL;
  ship.fire_rate = 1;
  return ship.lives > 0;
}

static void restart(void) {
  cash = cash_saved; /* $75D8: the cash as the restart point found it */
  ship_place(&ship, restart_x, restart_line);
  ship.death_count = RESTART_DEATH_COUNT;
  scroll_restart(&scroll, restart_line);
  enemies_restart(enemies, restart_line);
  weapons_restore(weapons);
  if (nashwan) weapons_nashwan(weapons); /* $7654 */
  stars_init(&stars, time_us_32());
}

/* ---- two players ---- */

static const uint16_t *store_map(void) {
  return (const uint16_t *)((const uint8_t *)level_h + level_h->map_offset);
}

static map_change_t *changes(int list) {
  return (map_change_t *)((uint8_t *)__rom_in_ram_start__ + CART_PLAYERS_OFFSET) +
         list * MAP_CHANGES;
}

/* The player playing into a block (its map's changes into `list`), and a
 * block (its changes in `list`) into the player playing. */
static void keep(player_t *p, int list) {
  p->ship = ship;
  p->score = score;
  p->cash = cash;
  p->cash_saved = cash_saved;
  p->saved = weapons->saved;
  p->invulnerable = weapons->invulnerable;
  p->nashwan = nashwan;
  p->shades = shades;
  p->credits = credits;
  p->scroll = scroll;
  p->restart_x = restart_x;
  p->restart_line = restart_line;
  shop_advice_get(p->advice);
  const uint16_t *orig = store_map();
  map_change_t *c = changes(list);
  p->changes = 0;
  uint32_t words = level_h->map_rows * X2LEVEL_COLS;
  for (uint32_t i = 0; i < words && p->changes + 2 <= MAP_CHANGES; i++) {
    if (map[i] == orig[i]) continue;
    uint32_t n = 1;
    while (i + n < words && map[i + n] != orig[i + n] && map[i + n] == map[i]) n++;
    if (n >= 3) {
      c[p->changes].at = (uint16_t)(i | MAP_RUN);
      c[p->changes++].to = map[i];
      c[p->changes].at = (uint16_t)n;
      c[p->changes++].to = 0;
      i += n - 1;
      continue;
    }
    c[p->changes].at = (uint16_t)i;
    c[p->changes++].to = map[i];
  }
}

static void take(const player_t *p, int list) {
  ship = p->ship;
  score = p->score;
  cash = p->cash;
  cash_saved = p->cash_saved;
  weapons->saved = p->saved;
  weapons->invulnerable = p->invulnerable;
  nashwan = p->nashwan;
  shades = p->shades;
  credits = p->credits;
  scroll = p->scroll;
  restart_x = p->restart_x;
  restart_line = p->restart_line;
  shop_advice_set(p->advice);
  memcpy(map, store_map(), level_h->map_rows * X2LEVEL_COLS * sizeof(uint16_t));
  const map_change_t *c = changes(list);
  for (uint32_t k = 0; k < p->changes; k++) {
    if (!(c[k].at & MAP_RUN)) {
      map[c[k].at] = c[k].to;
      continue;
    }
    uint32_t at = c[k].at & ~MAP_RUN, n = c[k + 1].at;
    for (uint32_t w = 0; w < n; w++) map[at + w] = c[k].to;
    k++;
  }
}

/* The turn passes ($765A): the other player plays from their own block. */
static void swap_players(void) {
  keep(&now, other_list ^ 1);
  take(&other, other_list);
  other = now;
  other_list ^= 1;
  player ^= 1;
}

/* In two players the turn passes on a life lost while the other is in and
 * has not finished the level ($76B4). */
static bool turn_passes(void) {
  int o = player ^ 1;
  return players == 2 && in_game[o] && !finished[o];
}

/* A new level for both: the other's level start as the playing one's
 * ($778A runs for each player). */
static void level_start_other(void) {
  other.scroll = scroll;
  other.restart_x = restart_x;
  other.restart_line = restart_line;
  other.cash = other.cash_saved = 0;
  shop_advice_get(other.advice);
  other.changes = 0;
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
#define SCAN_K 0x25 /* destroys the ship: the death and the restart */
#define SCAN_C 0x2E /* cheat mode: hits leave the shield as it is */
#define SCAN_J 0x24 /* three dives */
#define SCAN_DOUBLE 0x1A   /* [ : the double shot (the shop's) */
#define SCAN_BIG_MINE 0x1B /* ] : the bigger mine (the shop's) */
/* 1-9, 0, -, =, Backspace: the weapon tokens 4-16 */
#define SCAN_TOKEN_FIRST 0x02
#define SCAN_TOKEN_LAST 0x0E
/* V cycles the pace: an original frame every 3 VBLs (the original's
 * 16.7 fps), every 2 (25 fps), every VBL (50 fps). */
static const uint32_t paces_q8[] = {SCROLL_VBLS_PER_FRAME_Q8, 512u, 256u};
static const char *const pace_names[] = {"", " 25", " 50"};
#define FLY_LINES 4      /* lines a frame while an arrow is held */
#define SHOP_LINE 2520   /* just before level 1's mid shop */

static bool frozen, show_timing, fly_up, fly_down;
static unsigned pace_index;

static bool fly_key(uint8_t scancode, bool press) {
  if (scancode == SCAN_UP) fly_up = press;
  else if (scancode == SCAN_DOWN) fly_down = press;
  else if (!press) return false;
  else if (scancode == SCAN_F) frozen = !frozen;
  else if (scancode == SCAN_D) show_timing = !show_timing;
  else if (scancode == SCAN_V) {
    pace_index = (pace_index + 1) % (sizeof(paces_q8) / sizeof(paces_q8[0]));
    scroll_pace_init(&pace, paces_q8[pace_index]);
  }
  else if (scancode == SCAN_K) {
    /* Between frames: the next tick clears `died`, so ask for the sound here. */
    ship_die(&ship);
    if (ship.died) {
      explosion_sound();
      weapons_death(weapons);
    }
  }
  else if (scancode == SCAN_C) cheat = !cheat;
  else if (scancode >= SCAN_TOKEN_FIRST && scancode <= SCAN_TOKEN_LAST) {
    enemies_events_t ev = {0};
    take_token(TOKEN_REAR + (scancode - SCAN_TOKEN_FIRST), &ev);
  }
  else if (scancode == SCAN_J) ship.dives += DIVES_A_TOKEN;
  else if (scancode == SCAN_DOUBLE) weapons_make(weapons, 192);
  else if (scancode == SCAN_BIG_MINE) weapons_make(weapons, 60);
  else if (scancode == SCAN_R) scroll_seek(&scroll, SCROLL_START_LINE);
  else if (scancode == SCAN_S) scroll_seek(&scroll, SHOP_LINE);
  else if (scancode == SCAN_E) { /* the next stage: the mid one, or the end */
    int32_t mid = enemies->mid_line; /* never past a mid stage not over */
    if (mid <= 0) {
      scroll_seek(&scroll, 0);
    } else if (scroll.line > mid) {
      scroll_seek(&scroll, mid);
    }
  }
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
  snprintf(line, sizeof(line), "LINE %4ld DRAW %4luUS C2P %4luUS%s%s",
           (long)scroll.line, (unsigned long)worldDrawUs,
           (unsigned long)fb_last_convert_us(), pace_names[pace_index],
           cheat ? " C" : "");
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
  stars_draw(&stars, fb_chunked_buffer);
  draw_ship();
  weapons_draw(weapons, draw_enemy, NULL);
  enemies_draw(enemies, scroll.line, draw_enemy, draw_behind, draw_block, NULL);
  weapons_draw_shots(weapons, draw_enemy, draw_bullet, draw_beam, draw_dot,
                     NULL);
  draw_dial(dive_dial, SHIP_DIVE_FRAMES, DIAL_DIVE_X, DIAL_DIVE_Y);
  draw_dial(nashwan_dial, NASHWAN_FRAMES, DIAL_NASHWAN_X, DIAL_NASHWAN_Y);
  draw_dial(weapons->ring_dial, WEAPONS_INVULNERABLE, DIAL_RING_X, DIAL_RING_Y);
  draw_status_bar();
  worldDrawUs = time_us_32() - t0;
  if (worldDrawUs > worldDrawMaxUs) worldDrawMaxUs = worldDrawUs;
  timing();
}

/* Level `level` made ready and begun; the ship's state is the caller's. */
static bool begin_level(const char *folder, uint32_t level,
                        x2_progress_fn progress, void *progress_ctx) {
  /* The enemies' 8 KB go back to the heap while the level is converted:
   * the conversion needs its work buffers. */
  if (weapons) weapon_save = weapons->saved;
  free(enemies);
  enemies = NULL;
  free(weapons);
  weapons = NULL;
  gfx = ready_global(folder, progress, progress_ctx);
  const x2level_header_t *h = ready_level(folder, level, progress,
                                          progress_ctx);
  if (!enemies) enemies = malloc(sizeof(enemies_t));
  if (!weapons) weapons = malloc(sizeof(weapons_t));
  if (!gfx || !h || !enemies || !weapons || world_init(&world, h, map) != 0) {
    return false;
  }
  level_number = level;
  level_h = h;
  screens_image = ready_screens(folder, progress, progress_ctx);
  shop_level_start(h);
  shop_asked = shopping = false;
  enemies_init(enemies, h, gfx, map, h->map_rows, loop_count, time_us_32());
  enemies_set_strip(enemies, draw_strip);
  weapons_init(weapons, gfx, enemies);
  if (new_game) {
    weapons_game_start(weapons);
    new_game = false;
  } else { /* $7E58: the weapons made again from the last save */
    weapons->saved = weapon_save;
    weapons_restore(weapons);
  }
  if (nashwan) weapons_nashwan(weapons); /* $7EB2 */
  dive_dial = nashwan_dial = -1;
  ship_flash = false;
  fb_chunked_clear(0);
  palette_set(h->palette);
  const x2g_bitmap_t *m = &gfx->bitmaps[X2G_SHIP_HIT];
  hit = (ship_hitmask_t){(const uint8_t *)gfx + m->offset, m->width,
                         m->height, m->hot_x, m->hot_y};
  scroll_start(&scroll);
  restart_line = SCROLL_START_LINE;
  restart_x = SHIP_START_X;
  ship_place(&ship, restart_x, restart_line);
  cash = cash_saved = 0; /* $7766: every level starts with none */
  stars_init(&stars, time_us_32());
  fire_pending = false;
  sound_init();
#if defined(_DEBUG) && (_DEBUG != 0)
  /* The pace V chose holds for the session: every level, every game. */
  scroll_pace_init(&pace, paces_q8[pace_index]);
#else
  scroll_pace_init(&pace, SCROLL_VBLS_PER_FRAME_Q8);
#endif
  last_vbls = fb_st_vbls();
  worldDrawMaxUs = 0;
  frozen = false;
  draw();
  return true;
}

bool play_start(const char *folder, uint32_t level, x2_progress_fn progress,
                void *progress_ctx) {
  ship_game_start(&ship);
  score = 0;
  nashwan = shades = 0; /* $7334 */
  cheat = false;
  loop_count = 1;
  new_game = true;
  if (!begin_level(folder, level, progress, progress_ctx)) return false;
  player = 0; /* $7406: player 1 first, player 2 the same */
  in_game[0] = true;
  in_game[1] = players == 2;
  finished[0] = finished[1] = false;
  if (players == 2) keep(&other, other_list);
  start_ready(true);
  return true;
}

bool play_next(const char *folder, x2_progress_fn progress,
               void *progress_ctx) {
  uint32_t next = level_number + 1;
  if (next > 5) { /* $7E78: round again */
    next = 1;
    loop_count++;
  }
  if (!begin_level(folder, next, progress, progress_ctx)) return false;
  finished[0] = finished[1] = false;
  if (players == 2) {
    level_start_other();
    if (in_game[player ^ 1]) { /* $7E58: the other player begins it */
      swap_players();
      restart();
    }
  }
  start_ready(true);
  return true;
}

uint32_t play_level(void) { return level_number; }

/* Into the shop ($7CF8): the mid one the level asked for, or its end one. */
static void enter_shop(bool end) {
  shop_game_t g = {screens_image, gfx, level_h, &ship, weapons, enemies, &cash,
                   level_number, scroll.line, player, players, &nashwan, &shades,
                   level_h->palette,
                   {NULL, hook_take, hook_stop, hook_sample, hook_redraw}};
  shop_asked = false;
  shop_is_end = end;
  shopping = shop_start(&g);
  if (!shopping) DPRINTF("shop: could not start\n");
}

/* Out of it: after a mid shop the level's song again ($7DB2). */
static void leave_shop(void) {
  shopping = false;
  if (nashwan) weapons_nashwan(weapons); /* $7DA0 */
  sample_play(false);
  if (!shop_is_end && music) sound_song(SFX_TEMPO_PLAY); /* $7DB2 */
  last_vbls = fb_st_vbls();
}

/* The level won ($7DCE): in two players the other plays it too, from their
 * own restart point, unless they have finished it or are out. */
static play_state_t level_won(void) {
  if (level_number == 5) { /* $7DD2: bare again, a credit more */
    memset(&weapons->saved, 0, sizeof(weapons->saved));
    weapons->saved.type[0] = 148; /* the forward shot */
    credits++;
  }
  if (turn_passes()) {
    finished[player] = true;
    enemies_level_unwon(enemies);
    swap_players();
    restart();
    start_ready(false);
    return PLAY_ON;
  }
  sound_stop();
  return PLAY_WON;
}

/* A VBL at a time in the shop, the driver ticking. */
static play_state_t shop_frame(bool fire) {
  uint32_t now = fb_st_vbls();
  uint32_t vbls = now - last_vbls;
  last_vbls = now;
  if (vbls > MAX_CATCH_UP_VBLS) vbls = MAX_CATCH_UP_VBLS;
  while (vbls--) {
    sound_vbl();
    if (sample_on && sample_pos >= sample_len) sample_play(false);
    if (!shop_vbl(stick, fire)) {
      leave_shop();
      if (shop_is_end) return level_won();
      return PLAY_ON;
    }
    fire = false;
  }
  return PLAY_ON;
}

/* ---- the screens around the levels ---- */

static void flow_song(void *ctx, int tempo) {
  (void)ctx;
  sound_song(tempo);
}
static void flow_stop(void *ctx) {
  (void)ctx;
  sfx_write_t w[SFX_WRITES_MAX];
  send(w, sfx_stop(w));
}
static void flow_fade(void *ctx) {
  (void)ctx;
  sfx_fade();
}
static void flow_effect(void *ctx, int effect) {
  (void)ctx;
  sfx_request(effect, SFX_CHANNEL_A);
}

/* The high scores kept in the app's settings: two values of five entries,
 * each its score's seven digits and its three initials. The original keeps
 * them only until the ST is switched off. */
static const char *const k_score_keys[2] = {ACONFIG_PARAM_SCORES1, ACONFIG_PARAM_SCORES2};

static void load_scores(void) {
  flow_scores_clear(&scores);
  for (int part = 0; part < 2; part++) {
    const SettingsConfigEntry *e = settings_find_entry(aconfig_getContext(), k_score_keys[part]);
    if (!e || strlen(e->value) != 50) continue;
    for (int k = 0; k < 5; k++) {
      const char *v = e->value + 10 * k;
      uint32_t value = 0;
      bool ok = true;
      for (int d = 0; d < 7; d++) {
        ok = ok && v[d] >= '0' && v[d] <= '9';
        value = value * 10u + (uint32_t)(v[d] - '0');
      }
      if (!ok) continue;
      int row = 5 * part + k;
      scores.score[row] = value;
      memcpy(&scores.text[row][4], v, 7);
      memcpy(&scores.text[row][12], v + 7, 3);
    }
  }
}

static void save_scores(void) {
  for (int part = 0; part < 2; part++) {
    char v[51];
    for (int k = 0; k < 5; k++) {
      memcpy(v + 10 * k, &scores.text[5 * part + k][4], 7);
      memcpy(v + 10 * k + 7, &scores.text[5 * part + k][12], 3);
    }
    v[50] = 0;
    settings_put_string(aconfig_getContext(), k_score_keys[part], v);
  }
  settings_save(aconfig_getContext(), true);
}

/* "GET READY PLAYER n" ($84C4): at a level's start a fade in follows. */
static void start_ready(bool level_start) {
  flow_get_ready(player);
  ready_fades = level_start;
  mode = M_READY;
}

/* CONTINUE while credits are left ($7C9A), else GAME OVER. */
static void continue_or_over(void) {
  if (--credits > 0) {
    flow_continue(credits);
    mode = M_CONTINUE;
  } else {
    flow_game_over();
    mode = M_OVER;
  }
}

/* The last life lost ($7C7C): the high scores first. */
static void game_over(void) {
  static const uint16_t k_black[PALETTE_ENTRIES];
  palette_set(k_black);
  if (flow_high_score(score)) {
    mode = M_SCORE;
    return;
  }
  continue_or_over();
}

/* A screen of flow.h over: what follows it. */
static play_state_t flow_over(void) {
  static const uint16_t k_black[PALETTE_ENTRIES];
  switch (mode) {
    case M_ATTRACT:
      flow_select(3, &music);
      mode = M_SELECT;
      return PLAY_ON;
    case M_SELECT: /* $79B8: a new game */
      players = flow_result();
      credits = 3;
      return PLAY_START;
    case M_READY:
      if (!ready_fades) { /* after a death: the level's palette at once */
        palette_set(level_h->palette);
        mode = M_PLAY;
        return PLAY_ON;
      }
      palette_set(k_black); /* $79F2: black, the song, the level faded in */
      screen_start(&fade, NULL, NULL, NULL, k_black);
      if (music) sound_song(SFX_TEMPO_PLAY);
      draw();
      mode = M_FADE;
      return PLAY_ON;
    case M_SCORE:
      save_scores();
      palette_set(k_black);
      continue_or_over();
      return PLAY_ON;
    case M_CONTINUE: /* $7CA0: three lives, the score from 0 */
      if (flow_result()) {
        ship.lives = 3;
        score = 0;
        if (turn_passes()) swap_players();
        restart();
        start_ready(false);
      } else {
        flow_game_over();
        mode = M_OVER;
      }
      return PLAY_ON;
    case M_OVER: /* the player out; the other goes on if in */
      in_game[player] = false;
      if (players == 2 && in_game[player ^ 1]) {
        swap_players();
        if (finished[player]) { /* $76D0: they had finished it */
          enemies_level_unwon(enemies);
          if (level_number == 5 && screens_image) { /* their end shop now */
            mode = M_PLAY;
            enter_shop(true);
            if (shopping) return PLAY_ON;
          }
          return level_won();
        }
        restart();
        start_ready(false);
        return PLAY_ON;
      }
      return PLAY_OVER;
    default:
      return PLAY_ON;
  }
}

/* A VBL at a time on a screen of flow.h or in the level's fade in. */
static play_state_t flow_frame(void) {
  ikbd_key_event_t k;
  while (ikbd_pop_key(&k)) {
  } /* the screens read the stick only */
  ikbd_joystick_t j;
  ikbd_read_joystick(1, &j);
  bool fire = (j.pressed & IKBD_JOY_FIRE) != 0;
  uint32_t now = fb_st_vbls();
  uint32_t vbls = now - last_vbls;
  last_vbls = now;
  if (vbls > MAX_CATCH_UP_VBLS) vbls = MAX_CATCH_UP_VBLS;
  while (vbls--) {
    sound_vbl();
    if (mode == M_FADE) {
      screen_tick(&fade);
      if (!screen_fade_in(&fade, level_h->palette)) continue;
      mode = M_PLAY;
      return PLAY_ON;
    }
    if (flow_vbl(j.state, fire)) {
      fire = false;
      continue;
    }
    fire = false;
    play_state_t st = flow_over();
    if (st != PLAY_ON || mode == M_PLAY) return st;
  }
  if (mode != M_FADE) flow_draw();
  return PLAY_ON;
}

bool play_attract(const char *folder, x2_progress_fn progress,
                  void *progress_ctx) {
  gfx = ready_global(folder, progress, progress_ctx);
  screens_image = ready_screens(folder, progress, progress_ctx);
  if (!gfx || !screens_image) return false;
  if (!flow_ready) {
    load_scores();
    flow_hooks_t h = {NULL, flow_song, flow_stop, flow_fade, flow_effect};
    flow_init(screens_image, gfx, time_us_32(), &scores, &h);
    flow_ready = true;
  }
  sound_init();
  flow_attract();
  mode = M_ATTRACT;
  paused = false;
  last_vbls = fb_st_vbls();
  return true;
}

int play_players(void) { return players; }

play_state_t play_frame(void) {
  if (mode != M_PLAY) return flow_frame();
  ikbd_key_event_t k;
  while (ikbd_pop_key(&k)) {
    if (shopping) continue; /* the shop reads the stick only */
    if (paused) { /* a key goes on ($7B3E) */
      if (k.is_press) paused = false;
      continue;
    }
    if (k.is_press && k.scancode == SCAN_ESC) {
      sound_stop();
      return PLAY_OVER;
    }
    if (k.is_press && k.scancode == SCAN_P) {
      paused = true;
      continue;
    }
    if (k.is_press && k.scancode == SCAN_SPACE) ship_dive_key(&ship);
    fly_key(k.scancode, k.is_press);
  }
  ikbd_joystick_t j;
  ikbd_read_joystick(1, &j);
  stick = j.state; /* the original's stick byte: the same bits */
  if (shopping) return shop_frame((j.pressed & IKBD_JOY_FIRE) != 0);
  if (paused && (j.pressed & IKBD_JOY_FIRE)) { /* fire goes on too */
    paused = false;
  } else if (j.pressed & IKBD_JOY_FIRE) {
    fire_pending = true;
  }

  uint32_t now = fb_st_vbls();
  uint32_t vbls = now - last_vbls;
  last_vbls = now;
  if (vbls > MAX_CATCH_UP_VBLS) vbls = MAX_CATCH_UP_VBLS;
  while (vbls--) {
    if (!frozen && !paused) {
      for (uint32_t n = scroll_pace(&pace); n; n--) {
        game_frame();
        if (ship.state == SHIP_DEAD) { /* $7C50 */
          if (life_lost()) {
            if (turn_passes()) swap_players();
            restart();
            start_ready(false);
          } else {
            game_over();
          }
          return PLAY_ON;
        }
        if (shop_asked) break;
      }
    }
    sound_vbl(); /* the driver ticks every VBL, as on the original */
    if (shop_asked) { /* $7CE6: at the frame's end */
      if (screens_image) {
        enter_shop(false);
        if (shopping) return PLAY_ON;
      }
      shop_asked = false;
    }
  }
  fly_frame();
  draw();
  if (enemies_level_won(enemies)) { /* the payout over: its end shop */
    /* $7D72: level 5's, with the ending, waits for the last of two
     * players to finish it. */
    bool other_to_play = players == 2 && in_game[player ^ 1] && !finished[player ^ 1];
    if (screens_image && !(level_number == 5 && other_to_play)) {
      enter_shop(true);
      if (shopping) return PLAY_ON;
    }
    return level_won();
  }
  return PLAY_ON;
}
