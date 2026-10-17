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
#include "scroll.h"
#include "sfx.h"
#include "ship.h"
#include "stars.h"
#include "store.h"
#include "world.h"
#include "x2game.h"
#include "x2set.h"
#include "ym_ring.h"

extern const struct FB_FONT font8x8; /* fb.c */

#define MAX_CATCH_UP_VBLS 10 /* after a stall, resume rather than race */
#define SCAN_ESC 0x01          /* in play, as in the original: to the title */
/* The status bar's places (the original's status bar routines). */
#define SCORE_X 24     /* player 1; 7 digits, 8 pixels apart */
#define SCORE2_X 240   /* player 2 */
#define SCORE_DIGITS 7
#define LIVES_X 136
#define LIVES_Y 193
#define SHIELD_X 91
#define SHIELD_Y 195
#define SHIELD_LINES 3
#define SHIELD_COLOUR 14
/* After a death: the original shows its "get ready" screen and waits for
 * fire; until that screen is made, a pause. */
#define RESTART_VBLS 50
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
static uint32_t restart_wait;
static enemies_t *enemies; /* on the heap: kept from one level to the next */
static uint32_t score, cash;
static bool ship_flash; /* hit this frame: drawn in the flash colour */
static bool cheat;      /* debug builds: hits leave the shield as it is */

/* The tokens the first weapon's game knows (the original's $5040 table);
 * the others are weapons, still to come. */
#define TOKEN_SPEED 0
#define TOKEN_FIRE_RATE 1
#define TOKEN_SHIELD 2
#define TOKEN_SHIELD_BIG 3
#define TOKEN_POWER 6
#define TOKEN_BOMB 18
#define FIRE_RATE_MAX 4
#define SPEED_UPS_MAX 2
#define POWER_MAX 2

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

static void draw_status_bar(void) {
  draw_bitmap(X2G_STATUS_BAR, 0, X2LEVEL_LINES, FB_CHUNKED_H);
  uint32_t v = score;
  for (int i = SCORE_DIGITS - 1; i >= 0; i--, v /= 10) {
    draw_bitmap(X2G_SCORE_DIGIT0 + (int)(v % 10), SCORE_X + 8 * i,
                X2LEVEL_LINES, FB_CHUNKED_H);
    draw_bitmap(X2G_SCORE_DIGIT0, SCORE2_X + 8 * i, X2LEVEL_LINES, FB_CHUNKED_H);
  }
  int lives = ship.lives < 0 ? 0 : ship.lives > 9 ? 9 : ship.lives;
  draw_bitmap(X2G_LIVES_DIGIT0 + lives, LIVES_X, LIVES_Y, FB_CHUNKED_H);
  int shield = ship.shield < 0 ? 0 : ship.shield;
  fb_fill_rect(SHIELD_X, SHIELD_Y, shield, SHIELD_LINES, SHIELD_COLOUR);
  fb_fill_rect(SHIELD_X + shield, SHIELD_Y, SHIP_SHIELD_FULL - shield,
               SHIELD_LINES, 0);
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
  if (ship.state == SHIP_FLYING) {
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
  for (int i = 0; i < SHIP_BULLETS; i++) {
    const ship_bullet_t *b = &ship.bullets[i];
    if (b->alive) draw_bitmap(X2G_BULLET0 + b->power, b->x, b->y, X2LEVEL_LINES);
  }
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

static void sound_start(void) {
  sfx_write_t w[SFX_WRITES_MAX];
  sfx_init((const uint8_t *)gfx + gfx->driver_offset,
           (uint8_t *)__rom_in_ram_start__ + CART_SFX_WINDOW_OFFSET);
  ym_mode(CART_YM_MODE_REGISTERS);
  send(w, sfx_song(SFX_SONG_LEVEL, SFX_TEMPO_PLAY, w));
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

/* A token taken: the shield, the speed, the fire rate, the gun's power,
 * the bomb (the original's $5040 routines); the weapons' still to come. */
static void take_token(int token, enemies_events_t *ev) {
  switch (token) {
    case TOKEN_SPEED:
      if (ship.speed_ups < SPEED_UPS_MAX) ship.speed_ups++;
      break;
    case TOKEN_FIRE_RATE:
      if (ship.fire_rate < FIRE_RATE_MAX) ship.fire_rate++;
      break;
    case TOKEN_SHIELD:
    case TOKEN_SHIELD_BIG:
      ship.shield += token == TOKEN_SHIELD ? 20 : 40;
      if (ship.shield > SHIP_SHIELD_FULL) ship.shield = SHIP_SHIELD_FULL;
      break;
    case TOKEN_POWER:
      if (ship.power < POWER_MAX) ship.power++;
      break;
    case TOKEN_BOMB:
      enemies_bomb(enemies, ev);
      break;
    default:
      break;
  }
}

static void game_frame(void) {
  enemies_events_t ev = {0};
  enemies_frame_start(enemies);
  int32_t step = ship_tick(&ship, stick, fire_pending, &scroll, &hit, solid_at,
                           NULL);
  fire_pending = false;
  /* The objects see the world as it stood this frame; the scroll moves
   * after them. */
  enemies_frame_t f = {scroll.line, scroll.last_step, scroll.back_limit,
                       ship.state == SHIP_FLYING, ship.x, ship.y, {0}};
  if (!f.ship_flying || !enemies_box(enemies, x2global_source(ship_bitmap()),
                                     ship.x, ship.y, f.ship_box)) {
    f.ship_flying = false;
  }
  enemies_ship_body(enemies, &f, &ev);
  enemies_update(enemies, &f, &ev);
  for (int i = 0; i < SHIP_BULLETS; i++) {
    ship_bullet_t *b = &ship.bullets[i];
    if (b->alive && enemies_shoot(enemies, b->x, b->y, b->power + 1, &ev)) {
      b->alive = false;
    }
  }
  enemies_update_fx(enemies, &f, &ev);
  enemies_spawn(enemies, &f, &ev);
  for (int i = 0; i < ev.ntokens; i++) take_token(ev.tokens[i], &ev);
  score += (uint32_t)ev.score;
  cash += (uint32_t)ev.cash;
  if (ev.restart) {
    restart_x = ev.restart_x;
    restart_line = ev.restart_line;
  }
  ship_flash = ev.ship_flash;
  if (ev.shield_damage && !cheat) ship_hit(&ship, ev.shield_damage);
  /* Its sounds, asked as the original asks them: the gun ($6990) if
   * nothing waits on channel 2, the explosion, the objects' effects. */
  if (ship.shot) sfx_request_if_free(2, SFX_CHANNEL_A);
  for (int i = 0; i < ev.nsounds; i++) {
    sfx_request(ev.sounds[i].effect, ev.sounds[i].channel == 2 ? SFX_CHANNEL_A
                                                               : SFX_CHANNEL_B);
  }
  if (ship.died) explosion_sound();
  scroll_advance(&scroll, step, stick);
  stars_move(&stars, scroll.last_step);
}

/* A death taken: a life lost, the shield and fire rate refilled ($7C50).
 * False at the game's end. */
static bool life_lost(void) {
  ship.lives--;
  ship.shield = SHIP_SHIELD_FULL;
  ship.fire_rate = 1;
  if (ship.lives <= 0) return false;
  restart_wait = RESTART_VBLS;
  return true;
}

static void restart(void) {
  ship_place(&ship, restart_x, restart_line);
  ship.death_count = RESTART_DEATH_COUNT;
  scroll_restart(&scroll, restart_line);
  enemies_restart(enemies, restart_line);
  stars_init(&stars, time_us_32());
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
/* V cycles the pace: an original frame every 3 VBLs (the original's
 * 16.7 fps), every 2 (25 fps), every VBL (50 fps). */
static const uint32_t paces_q8[] = {SCROLL_VBLS_PER_FRAME_Q8, 512u, 256u};
static const char *const pace_names[] = {"", " 25", " 50"};
#define FLY_LINES 4      /* lines a frame while an arrow is held */
#define SHOP_LINE 0x9C0  /* where level 1's code puts the world at its shop */

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
    if (ship.died) explosion_sound();
  }
  else if (scancode == SCAN_C) cheat = !cheat;
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
  enemies_draw(enemies, draw_enemy, NULL);
  draw_status_bar();
  worldDrawUs = time_us_32() - t0;
  if (worldDrawUs > worldDrawMaxUs) worldDrawMaxUs = worldDrawUs;
  timing();
}

bool play_start(const char *folder, uint32_t level, x2_progress_fn progress,
                void *progress_ctx) {
  gfx = ready_global(folder, progress, progress_ctx);
  const x2level_header_t *h = ready_level(folder, level, progress,
                                          progress_ctx);
  if (!enemies) enemies = malloc(sizeof(enemies_t));
  if (!gfx || !h || !enemies || world_init(&world, h, map) != 0) return false;
  enemies_init(enemies, h, gfx, map, h->map_rows, 1, time_us_32());
  score = cash = 0;
  ship_flash = false;
  cheat = false;
  fb_chunked_clear(0);
  palette_set(h->palette);
  const x2g_bitmap_t *m = &gfx->bitmaps[X2G_SHIP_HIT];
  hit = (ship_hitmask_t){(const uint8_t *)gfx + m->offset, m->width,
                         m->height, m->hot_x, m->hot_y};
  scroll_start(&scroll);
  restart_line = SCROLL_START_LINE;
  restart_x = SHIP_START_X;
  ship_game_start(&ship);
  ship_place(&ship, restart_x, restart_line);
  stars_init(&stars, time_us_32());
  fire_pending = false;
  restart_wait = 0;
  sound_start();
  scroll_pace_init(&pace, SCROLL_VBLS_PER_FRAME_Q8);
#if defined(_DEBUG) && (_DEBUG != 0)
  pace_index = 0;
#endif
  last_vbls = fb_st_vbls();
  worldDrawMaxUs = 0;
  frozen = false;
  draw();
  return true;
}

bool play_frame(void) {
  ikbd_key_event_t k;
  while (ikbd_pop_key(&k)) {
    if (k.is_press && k.scancode == SCAN_ESC) {
      sound_stop();
      return false;
    }
    fly_key(k.scancode, k.is_press);
  }
  ikbd_joystick_t j;
  ikbd_read_joystick(1, &j);
  stick = j.state; /* the original's stick byte: the same bits */
  if (j.pressed & IKBD_JOY_FIRE) fire_pending = true;

  uint32_t now = fb_st_vbls();
  uint32_t vbls = now - last_vbls;
  last_vbls = now;
  if (vbls > MAX_CATCH_UP_VBLS) vbls = MAX_CATCH_UP_VBLS;
  while (vbls--) {
    if (frozen) {
    } else if (restart_wait) {
      if (--restart_wait == 0) restart();
    } else {
      for (uint32_t n = scroll_pace(&pace); n; n--) {
        game_frame();
        if (ship.state == SHIP_DEAD) {
          if (!life_lost()) {
            sound_stop();
            return false; /* the game is over */
          }
          break;
        }
      }
    }
    sound_vbl(); /* the driver ticks every VBL, as on the original */
  }
  fly_frame();
  draw();
  return true;
}
