// units: rp/src/weapons.c rp/src/enemies.c rp/src/level1.c rp/src/level2.c rp/src/level3.c rp/src/level4.c rp/src/level5.c rp/src/rng.c rp/src/x2level.c rp/src/x2global.c rp/src/x2sprite.c
/* The player's weapons on a synthetic game image laid out like the
 * original's (the windows the weapons read: the pods' places, the drone's
 * dots, the homing missiles' speeds and animations, the laser's sprites and
 * patterns, the damages; animation lists of one 16 x 16 sprite) and an
 * empty level: the forward shot's bullets, side and rear shots excluding
 * each other, the power-up, the pods in slots 1-4 (a fifth lost), the
 * laser's beam growing then leaving, one bomb at a time, the homing volley,
 * the drone's dots by power, the electro ball's swing, the mines dropped
 * while steering, a restart point's save, a death and the weapons made
 * again, Super Nashwan's set made and the player's weapons back after it.
 * No game data. */
#include <string.h>

#include "test.h"
#include "weapons.h"

#define B X2LEVEL_BASE

static uint8_t level_img[0x2000];
static uint8_t global_img[0x10000];
static uint8_t game[0xA000];
static uint16_t map[20 * 20];
static enemies_t en;
static weapons_t wp;
static enemies_events_t ev;

static void gw(uint32_t a, uint16_t v) {
  game[a] = (uint8_t)(v >> 8);
  game[a + 1] = (uint8_t)v;
}
static void gl(uint32_t a, uint32_t v) {
  gw(a, (uint16_t)(v >> 16));
  gw(a + 2, (uint16_t)v);
}

/* Every list the weapons start: one sprite for one frame, looping. */
static void loop_list(uint32_t a) {
  gl(a, 0x27000);
  gw(a + 4, 1);
  gl(a + 6, 0);
  gl(a + 10, X2SPRITE_LOOP);
  gl(a + 14, a);
}

static void build(void) {
  x2level_header_t *lh = (x2level_header_t *)level_img;
  memset(level_img, 0, sizeof(level_img));
  lh->magic = X2LEVEL_MAGIC;
  lh->level = 2;
  lh->raw_offset = 256;
  lh->raw_size = 0x1000;
  uint8_t *L = level_img + 256;
  L[0x38] = 0x00; /* the wave and scenery tables: one end word each */
  L[0x39] = 0x04;
  L[0x3A] = 0xF8;
  L[0x3B] = 0x00;
  L[0x14] = 0x00;
  L[0x15] = 0x04;
  L[0x16] = 0xF8;
  L[0x17] = 0x00;
  L[0x800] = 0xFF;
  L[0x801] = 0xFF;
  lh->sprites_offset = 256 + 0x1000;

  memset(game, 0, sizeof(game));
  static const uint32_t lists[] = {0x302A, 0x2D0C, 0x2F86, 0x2D8A, 0x31F2, 0x30A2,
                                   0x30C0, 0x2FFA, 0x2FE2, 0x30DE, 0x307E, 0x2DDE,
                                   0x348C, 0x3108, 0x2DAE, 0x2D6C, 0x2EFE, 0x2F2A};
  for (unsigned i = 0; i < sizeof(lists) / 4; i++) loop_list(lists[i]);
  for (int d = 0; d < 8; d++) loop_list(0x3102 + 30u * (uint32_t)d);
  /* the pods' places, as the original lays them: slot 4 first */
  static const int16_t places[8] = {44, 24, -44, 24, 26, 16, -26, 16};
  for (int i = 0; i < 8; i++) gw(0x2BD0 + 2u * (uint32_t)i, (uint16_t)places[i]);
  /* the drone's dots: power 0 four, 1 eight, 2 twelve, at the drone */
  static const uint32_t dots[3] = {0x45D4, 0x45F6, 0x4638};
  static const int n_dots[3] = {4, 8, 12};
  for (int p = 0; p < 3; p++) {
    gl(0x45C8 + 4u * (uint32_t)p, dots[p]);
    gw(dots[p], (uint16_t)(n_dots[p] - 1));
    for (int k = 0; k < n_dots[p]; k++) {
      gw(dots[p] + 2 + 8u * (uint32_t)k + 4, (uint16_t)(k % 2 ? 6 : -6));
      gw(dots[p] + 2 + 8u * (uint32_t)k + 6, (uint16_t)-6);
    }
  }
  /* the homing missiles' speeds: up 5 ... */
  static const int16_t hdy[8] = {-5, -4, 0, 4, 5, 4, 0, -4};
  static const int16_t hdx[8] = {0, 4, 5, 4, 0, -4, -5, -4};
  for (int d = 0; d < 8; d++) {
    gw(0x4330 + 2u * (uint32_t)d, (uint16_t)hdy[d]);
    gw(0x4340 + 2u * (uint32_t)d, (uint16_t)hdx[d]);
  }
  /* the laser's tails and tips, and a beam pattern */
  for (int i = 0; i < 6; i++) gl(0x6CA2 + 4u * (uint32_t)i, 0x27000);
  /* the damages: homing 1, electro 4, shell 2, -, dot 1, bullet 1, flame 1,
   * laser 3, bomb 8, mine 4 */
  static const int16_t damages[10] = {1, 4, 2, 1, 1, 1, 1, 3, 8, 4};
  for (int i = 0; i < 10; i++) gw(0x7AEA + 2u * (uint32_t)i, (uint16_t)damages[i]);

  x2global_header_t *h = (x2global_header_t *)global_img;
  memset(global_img, 0, sizeof(global_img));
  static const uint32_t win[X2G_WINDOWS][2] = {
      {0xF6E, 0x1C},  {0x2CC0, 0x802}, {0x3F96, 0x8A}, {0x4140, 0x40},
      {0x5040, 0x2C0}, {0x9C80, 0x140}, {0x2BD0, 0x10}, {0x4310, 0x50},
      {0x45C8, 0xB8}, {0x6CA2, 0x18}, {0x6E22, 0x3C}, {0x7AEA, 0x14},
      {0x6B6C, 0x28}};
  uint32_t at = sizeof(*h);
  for (int w = 0; w < X2G_WINDOWS; w++) {
    h->windows[w].start = win[w][0];
    h->windows[w].size = win[w][1];
    h->windows[w].offset = at;
    memcpy(global_img + at, game + win[w][0], win[w][1]);
    at += (win[w][1] + 3) & ~3u;
  }
  h->magic = X2GLOBAL_MAGIC;
  h->sprites_offset = at;
  h->sprite_count = 1;
  x2_sprite_t *t = (x2_sprite_t *)(global_img + at);
  x2_sprite_t s = {0x27000, 8, 8, 16, 16, 16, 0, 0, 0, 16, 16, at + sizeof(x2_sprite_t)};
  t[0] = s;
}

static weapons_frame_t fr;

static void start(void) {
  build();
  enemies_init(&en, (const x2level_header_t *)level_img,
               (const x2global_header_t *)global_img, map, 20, 1, 77);
  weapons_init(&wp, (const x2global_header_t *)global_img, &en);
  weapons_game_start(&wp);
  memset(&fr, 0, sizeof(fr));
  fr.x = fr.before_x = fr.trail_x = 160;
  fr.y = fr.before_y = fr.trail_y = 150;
}

static void frame(void) {
  memset(&ev, 0, sizeof(ev));
  weapons_update(&wp, &fr, &ev);
  weapons_update_shots(&wp, &fr, &ev);
}

static int shots_of(int role_type) {
  int n = 0;
  for (int8_t i = wp.shots; i >= 0; i = wp.pool[i].next) {
    n += !(wp.pool[i].flags & 1) && wp.pool[i].type == role_type;
  }
  return n;
}

static const weapon_t *shot_n(int type, int n) {
  for (int8_t i = wp.shots; i >= 0; i = wp.pool[i].next) {
    if (!(wp.pool[i].flags & 1) && wp.pool[i].type == type && n-- == 0) return &wp.pool[i];
  }
  return NULL;
}

static int dots(void) {
  int n = 0;
  for (int d = 0; d < WEAPONS_DOTS; d++) n += wp.dots[d].x >= 0;
  return n;
}

int main(void) {
  /* The forward shot: a bullet from 6 above, 9 up a frame (the first step
   * in the frame it is made), effect 2; gone past the top. */
  start();
  CHECK_EQ(weapons_slot_type(&wp, 0), 148);
  CHECK_EQ(weapons_fire_period(&wp), 8);
  fr.shot = true;
  frame();
  fr.shot = false;
  CHECK_EQ(shots_of(16), 1);
  CHECK_EQ(shot_n(16, 0)->y >> 16, 150 - 6 - 9);
  CHECK_EQ(ev.nsounds, 1);
  CHECK_EQ(ev.sounds[0].effect, 2);
  for (int i = 0; i < 15; i++) frame(); /* at y 0 */
  CHECK_EQ(shots_of(16), 1);
  CHECK_EQ(shot_n(16, 0)->y >> 16, 0);
  frame();
  CHECK_EQ(shots_of(16), 0);

  /* The side shot (slot 6): two bullets across; the rear shot (slot 5)
   * takes the side shot's place; taken again it powers up (to 2). */
  weapons_take(&wp, TOKEN_SIDE);
  CHECK_EQ(weapons_slot_type(&wp, 6), 152);
  fr.shot = true;
  frame();
  fr.shot = false;
  CHECK_EQ(shots_of(16), 3);
  weapons_take(&wp, TOKEN_REAR);
  CHECK_EQ(weapons_slot_type(&wp, 6), 0);
  CHECK_EQ(weapons_slot_type(&wp, 5), 68);
  weapons_take(&wp, TOKEN_REAR);
  weapons_take(&wp, TOKEN_REAR);
  weapons_take(&wp, TOKEN_REAR);
  CHECK_EQ(weapons_slot_power(&wp, 5), 2);
  /* the power-up: the weakest below its max (the forward shot at 0) */
  weapons_take(&wp, TOKEN_POWERUP);
  CHECK_EQ(weapons_slot_power(&wp, 0), 1);

  /* Pods: slots 1-4 in turn, by the ship at their places; a fifth lost. */
  weapons_take(&wp, TOKEN_LASER);
  weapons_take(&wp, TOKEN_CANNON);
  weapons_take(&wp, TOKEN_LAUNCHER);
  weapons_take(&wp, TOKEN_LASER);
  weapons_take(&wp, TOKEN_CANNON);
  CHECK_EQ(weapons_slot_type(&wp, 1), 32);
  CHECK_EQ(weapons_slot_type(&wp, 2), 52);
  CHECK_EQ(weapons_slot_type(&wp, 3), 144);
  CHECK_EQ(weapons_slot_type(&wp, 4), 32);
  frame();
  const weapon_t *laser = &wp.pool[wp.slots[1].obj];
  CHECK_EQ(laser->x >> 16, 160 - 26);
  CHECK_EQ(laser->y >> 16, 150 + 16);
  CHECK_EQ(wp.pool[wp.slots[4].obj].x >> 16, 160 + 44);
  /* the laser's beam: 0, 32, 64 long from the pod, then away 32 a frame */
  for (int i = 0; i < 8; i++) frame(); /* the lasers' timers run out */
  fr.shot = true;
  frame();
  fr.shot = false;
  CHECK_EQ(shots_of(0x24), 2);
  const weapon_t *beam = shot_n(0x24, 1);
  CHECK_EQ(beam->v40, 0);
  frame();
  CHECK_EQ(beam->v40, 32);
  frame();
  CHECK_EQ(beam->v40, 64);
  int32_t y = beam->y >> 16;
  frame();
  CHECK_EQ(beam->v40, 65);
  CHECK_EQ(beam->y >> 16, y - 32);

  /* The bomb (slot 6): one in flight at a time; it climbs 6 a frame. */
  start();
  weapons_take(&wp, TOKEN_BOMB);
  CHECK_EQ(weapons_fire_period(&wp), 12);
  fr.shot = true;
  frame();
  CHECK_EQ(shots_of(40), 1);
  int32_t by = shot_n(40, 0)->y >> 16;
  for (int i = 0; i < 20; i++) frame();
  CHECK_EQ(shots_of(40), 1);
  CHECK(shot_n(40, 0)->y >> 16 < by);

  /* Homing missiles: a volley of four, effect 4, no other while it flies. */
  start();
  weapons_take(&wp, TOKEN_HOMING);
  CHECK_EQ(weapons_slot_type(&wp, 5), 72);
  CHECK_EQ(weapons_fire_period(&wp), 30);
  fr.shot = true;
  frame();
  CHECK_EQ(shots_of(72), 4);
  CHECK_EQ(ev.sounds[ev.nsounds - 1].effect, 4);
  frame();
  CHECK_EQ(shots_of(72), 4);

  /* The drone: under the ship's place four frames ago; held fire, dots
   * every 6 frames: 4, then 8 at power 1. */
  start();
  weapons_take(&wp, TOKEN_DRONE);
  fr.held = true;
  frame();
  CHECK_EQ(dots(), 4);
  CHECK_EQ(wp.pool[wp.slots[5].obj].y >> 16, 150 + 25);
  for (int i = 0; i < 5; i++) frame();
  CHECK_EQ(dots(), 4); /* the first four still out */
  weapons_take(&wp, TOKEN_DRONE);
  frame();
  CHECK_EQ(dots(), 4 + 8);
  fr.held = false;

  /* The electro ball: held, it moves 4 times the ship's move. */
  start();
  weapons_take(&wp, TOKEN_ELECTRO);
  frame();
  const weapon_t *ball = &wp.pool[wp.slots[5].obj];
  int32_t bx = ball->x >> 16;
  fr.held = true;
  frame(); /* launched */
  fr.before_x = 160;
  fr.x = 163;
  frame();
  CHECK_EQ(ball->x >> 16, bx + 12);
  fr.held = false;
  fr.x = 160;

  /* Mines: the layer climbs 12 a frame to the top, then steered; a mine
   * at once and every 32 pixels; released, they go off in turn. */
  start();
  weapons_take(&wp, TOKEN_MINE);
  fr.held = true;
  for (int i = 0; i < 16; i++) frame();
  fr.stick = 8; /* right */
  for (int i = 0; i < 9; i++) frame();
  CHECK_EQ(wp.mines, 3); /* at once, then 32 and 64 further */
  fr.stick = 0;
  fr.held = false;
  for (int i = 0; i < 20; i++) frame();
  CHECK_EQ(wp.mines, 0);

  /* A restart point saves; a death loses the newest; the restore makes the
   * saved ones again with their powers. */
  start();
  weapons_take(&wp, TOKEN_REAR);
  weapons_take(&wp, TOKEN_REAR);
  weapons_save(&wp);
  weapons_take(&wp, TOKEN_LASER);
  weapons_death(&wp);
  CHECK_EQ(weapons_slot_type(&wp, 1), 0);
  weapons_take(&wp, TOKEN_CANNON);
  weapons_restore(&wp);
  CHECK_EQ(weapons_slot_type(&wp, 0), 148);
  CHECK_EQ(weapons_slot_type(&wp, 5), 68);
  CHECK_EQ(weapons_slot_power(&wp, 5), 1);
  CHECK_EQ(weapons_slot_type(&wp, 1), 0);

  /* Super Nashwan: the player's weapons set aside, the full set made (the
   * double shot, two cannons, two lasers, the drone and the side shot, all
   * but the cannons at power 2); at its end its set is freed and the
   * player's are back, nothing lost from the pool. A death's restore ends
   * it first. */
  start();
  weapons_take(&wp, TOKEN_REAR);
  int free_before = 0;
  for (int8_t i = wp.free; i >= 0; i = wp.pool[i].next) free_before++;
  weapons_nashwan(&wp);
  static const uint16_t set[WEAPONS_SLOTS] = {192, 52, 52, 32, 32, 44, 152};
  static const int powers[WEAPONS_SLOTS] = {2, 0, 0, 2, 2, 2, 2};
  for (int k = 0; k < WEAPONS_SLOTS; k++) {
    CHECK_EQ(weapons_slot_type(&wp, k), set[k]);
    CHECK_EQ(weapons_slot_power(&wp, k), powers[k]);
  }
  for (int i = 0; i < 5; i++) frame();
  weapons_nashwan_end(&wp);
  frame(); /* the cannons' tops leave with them */
  frame();
  CHECK_EQ(weapons_slot_type(&wp, 0), 148);
  CHECK_EQ(weapons_slot_type(&wp, 5), 68);
  CHECK_EQ(weapons_slot_type(&wp, 1), 0);
  int free_after = 0;
  for (int8_t i = wp.free; i >= 0; i = wp.pool[i].next) free_after++;
  CHECK_EQ(free_after, free_before);
  weapons_save(&wp);
  weapons_nashwan(&wp);
  weapons_restore(&wp);
  CHECK(!wp.nashwan);
  CHECK_EQ(weapons_slot_type(&wp, 5), 68);
  CHECK_EQ(weapons_slot_type(&wp, 3), 0);
  TEST_END();
}
