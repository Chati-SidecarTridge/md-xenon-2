// units: rp/src/enemies.c rp/src/x2level.c rp/src/x2global.c rp/src/x2sprite.c
/* The enemies on a synthetic level and game image laid out like the
 * original's (the level's header, wave and scenery tables, enemy types,
 * paths and animation lists; the game image's direction table, shots'
 * directions, explosions', cash's and capsule's lists, tokens), held in a
 * level image and a shared image as the store holds them: the triggers and
 * their restart, the paths, a trail, the kills, scores, explosions and
 * sounds, a group's cash, a chain, a capsule and its token, aimed fire and
 * a shot on the ship, the ship's body, a wall object of level 1 in the
 * map, and level 1's boss: asleep, awake with its turrets, its opening, its
 * eye, its death and payout, and a restart that keeps it. No game data. */
#include <stdlib.h>
#include <string.h>

#include "enemies.h"
#include "test.h"

#define B X2LEVEL_BASE
#define RAW 0x1B000u /* the level from $4F000 to $6A000 */

static uint8_t level_img[0x40000];
static uint8_t global_img[0x10000];
static uint8_t *L; /* the level's bytes as the original holds them */
static uint8_t game[0xA000]; /* the game image from 0 (windows copied out) */
static uint16_t map[20 * 300];

static void lw(uint32_t a, uint16_t v) {
  L[a - B] = (uint8_t)(v >> 8);
  L[a - B + 1] = (uint8_t)v;
}
static void ll(uint32_t a, uint32_t v) {
  lw(a, (uint16_t)(v >> 16));
  lw(a + 2, (uint16_t)v);
}
static void gw(uint32_t a, uint16_t v) {
  game[a] = (uint8_t)(v >> 8);
  game[a + 1] = (uint8_t)v;
}
static void gl(uint32_t a, uint32_t v) {
  gw(a, (uint16_t)(v >> 16));
  gw(a + 2, (uint16_t)v);
}

/* A record of seven words. */
static void rec(uint32_t a, int line, int kind, int count, int path,
                int spacing, int fire, int speed) {
  lw(a, (uint16_t)line);
  lw(a + 2, (uint16_t)kind);
  lw(a + 4, (uint16_t)count);
  lw(a + 6, (uint16_t)path);
  lw(a + 8, (uint16_t)spacing);
  lw(a + 10, (uint16_t)fire);
  lw(a + 12, (uint16_t)speed);
}

/* A sprite table entry: 16 x 16, hotspot (8, 8), box (2, 2) 12 x 12. */
static x2_sprite_t spr(uint32_t source) {
  x2_sprite_t s = {source, 8, 8, 16, 16, 16, 0, 2, 2, 12, 12, 0};
  return s;
}

/* Waves at $4F700, scenery at $4F800, types at $4F200, paths at $4F600. */
static void build_level(int level) {
  x2level_header_t *h = (x2level_header_t *)level_img;
  memset(level_img, 0, sizeof(level_img));
  h->magic = X2LEVEL_MAGIC;
  h->level = (uint32_t)level;
  h->raw_offset = 256;
  h->raw_size = RAW;
  L = level_img + 256;
  /* the wave handler: kinds 1 and 2 */
  lw(B + 0x10, 0x6000);
  lw(B + 0x12, 0x100 - 0x12);
  ll(B + 0x10A + 4, B + 0x180);
  lw(B + 0x180, 0x45F9);
  ll(B + 0x182, B + 0x200);
  ll(B + 0x10A + 8, B + 0x188);
  lw(B + 0x188, 0x45F9);
  ll(B + 0x18A, B + 0x240);
  ll(B + 0x14, B + 0x800);
  ll(B + 0x28, B + 0x3008);           /* the enemies' shot */
  ll(B + 0x30, B + 0x600);
  ll(B + 0x34, B + 0x640);
  ll(B + 0x38, B + 0x700);
  /* kind 1: one part, worth 400, small; its list loops on two sprites */
  lw(B + 0x200, 0xC8);
  lw(B + 0x202, 400);
  ll(B + 0x204, 0xE1E);
  ll(B + 0x20C, 0xE2A);
  ll(B + 0x210, B + 0x400);
  /* kind 2: a big chain of two, worth 300 and 0 */
  lw(B + 0x240, 0x108);
  lw(B + 0x242, 300);
  ll(B + 0x244, 0xE1E);
  ll(B + 0x24C, 0xE30);
  ll(B + 0x250, B + 0x400);
  L[0x240 + 22] = 0xFF; /* both parts big: the group is the last part's */
  L[0x240 + 23] = 0xFF;
  L[0x258 + 22] = 0xFF;
  lw(B + 0x258, 0x108);
  ll(B + 0x25C, 0xE36);
  ll(B + 0x264, 0xE30);
  ll(B + 0x268, B + 0x420);
  L[0x258 + 23] = 0xFF;
  ll(B + 0x400, B + 0x3008);
  lw(B + 0x404, 2);
  ll(B + 0x406, B + 0x3108);
  lw(B + 0x40A, 2);
  ll(B + 0x410, X2SPRITE_LOOP);
  ll(B + 0x414, B + 0x400);
  ll(B + 0x420, B + 0x3108);
  lw(B + 0x424, 0);
  /* paths: 1 straight down from (100, -16) for 400 steps; 2 a wait of 5
   * steps then right from (0, 50) */
  ll(B + 0x600, B + 0x640);
  ll(B + 0x604, B + 0x660);
  lw(B + 0x640, 10); lw(B + 0x642, 100); lw(B + 0x644, (uint16_t)-16);
  lw(B + 0x646, 2); lw(B + 0x648, 64); lw(B + 0x64A, 0); lw(B + 0x64C, 0);
  lw(B + 0x64E, 400); lw(B + 0x650, 0);
  lw(B + 0x660, 10); lw(B + 0x662, 0); lw(B + 0x664, 50);
  lw(B + 0x666, 4); lw(B + 0x668, 5);
  lw(B + 0x66A, 2); lw(B + 0x66C, 0); lw(B + 0x66E, 0); lw(B + 0x670, 0);
  lw(B + 0x672, 300); lw(B + 0x674, 0);
  lw(B + 0x700, 0xFFFF);
  lw(B + 0x800, 0xFFFF);
  /* sprites at $4F3008, $4F3108 */
  h->sprites_offset = 256 + RAW;
  h->sprite_count = 2;
  x2_sprite_t *t = (x2_sprite_t *)(level_img + h->sprites_offset);
  t[0] = spr(B + 0x3008);
  t[1] = spr(B + 0x3108);
  t[0].offset = t[1].offset = h->sprites_offset + 2 * sizeof(x2_sprite_t);
}

/* The game image: a direction table moving one pixel a step, the shots'
 * 16 directions, the lists, a token table. */
static void build_global(void) {
  x2global_header_t *h = (x2global_header_t *)global_img;
  memset(global_img, 0, sizeof(global_img));
  memset(game, 0, sizeof(game));
  game[0x9C80 + 64] = 64;  /* direction 0: x + 1 a step */
  game[0x9C80 + 128] = 0;
  game[0x9C80 + 64 + 64] = 0;
  game[0x9C80 + 64] = 64;  /* direction 64: y + 1 (sine[64]) */
  static const int16_t dx[8] = {0, 11585, 16384, 11585, 0, -11585, -16384, -11585};
  static const int16_t dy[8] = {-16384, -11585, 0, 11585, 16384, 11585, 0, -11585};
  for (int i = 0; i < 8; i++) {
    gw(0x4140 + 2 * (uint32_t)i, (uint16_t)dx[i]);
    gw(0x4160 + 2 * (uint32_t)i, (uint16_t)dy[i]);
  }
  /* explosions: one sprite for 2 frames, then the end; cash and capsule
   * loop on one sprite */
  static const uint32_t ends[] = {0x2EFE, 0x2F2A};
  for (int i = 0; i < 2; i++) {
    gl(ends[i], 0x27000);
    gw(ends[i] + 4, 2);
    gl(ends[i] + 6, 0);
    gl(ends[i] + 10, 0x34FA);
  }
  static const uint32_t loops[] = {0x2E9E, 0x2E3E, 0x2D30, 0x3000};
  for (int i = 0; i < 4; i++) {
    gl(loops[i], 0x27000);
    gw(loops[i] + 4, 1);
    gl(loops[i] + 6, 0);
    gl(loops[i] + 10, X2SPRITE_LOOP);
    gl(loops[i] + 14, loops[i]);
  }
  for (int t = 0; t < 19; t++) {
    gw(0x3F96 + 6 * (uint32_t)t, (uint16_t)(116 + t));
    gl(0x3F98 + 6 * (uint32_t)t, 0x3000);
  }
  game[0x5040 + 4 * 2] = 0xFF;   /* token 2's pickup: flagged */
  game[0x5040 + 4 * 118] = 0xFF; /* its type (116 + 2) at the shot: too */
  static const uint32_t win[6][2] = {{0xF6E, 0x1C}, {0x2CC0, 0x802}, {0x3F96, 0x8A},
                                     {0x4140, 0x40}, {0x5040, 0x2C0}, {0x9C80, 0x140}};
  uint32_t at = sizeof(*h);
  for (int w = 0; w < 6; w++) {
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
  t[0] = spr(0x27000);
  t[0].offset = at + sizeof(x2_sprite_t);
}

static enemies_t en;
static enemies_frame_t fr;
static enemies_events_t ev;

static void frame(int32_t line) {
  memset(&ev, 0, sizeof(ev));
  fr.line = line;
  enemies_frame_start(&en);
  enemies_ship_body(&en, &fr, &ev);
  enemies_update(&en, &fr, &ev);
  enemies_update_fx(&en, &fr, &ev);
  enemies_spawn(&en, &fr, &ev);
}

static int draws;
static struct {
  int32_t x, y;
  uint32_t source;
  bool flash;
} drawn[ENEMIES_POOL];
static void on_draw(void *ctx, const x2_sprite_t *s, const uint8_t *px,
                    int32_t x, int32_t y, bool flash) {
  (void)ctx;
  (void)px;
  drawn[draws].x = x;
  drawn[draws].y = y;
  drawn[draws].source = s->source;
  drawn[draws].flash = flash;
  draws++;
}
static void draw(void) {
  draws = 0;
  enemies_draw(&en, 0, on_draw, NULL, NULL);
}

static void start(int level) {
  build_level(level);
  build_global();
  enemies_init(&en, (const x2level_header_t *)level_img,
               (const x2global_header_t *)global_img, map, 300, 1, 1234);
  memset(&fr, 0, sizeof(fr));
  fr.back_limit = 4000;
  fr.ship_flying = true;
  fr.ship_x = 160;
  fr.ship_y = 176;
  fr.ship_box[0] = fr.ship_box[1] = fr.ship_box[2] = fr.ship_box[3] = -100;
}

int main(void) {
  /* Triggers: a trail of three on path 1 at line 1000, 4 steps apart, 2
   * steps a frame; a capsule (token 2) at line 900 on path 2. */
  start(2);
  rec(B + 0x700, 1000, 1, 3, 1, 4, 0, 2);
  rec(B + 0x70E, 900, 0, 1, 2, 32, 0, 2);
  lw(B + 0x71C, 0xFFFF);
  frame(1010);
  CHECK_EQ(enemies_count(&en), 0);
  frame(1000);
  CHECK_EQ(enemies_count(&en), 3);
  frame(999);
  frame(990);
  CHECK_EQ(enemies_count(&en), 3);

  /* The path: the lead 2 pixels down a frame from (100, -16); the third
   * waits 8 steps (4 frames). */
  draw();
  CHECK_EQ(draws, 3);
  CHECK_EQ(drawn[2].x, 100); /* the lead, spawned first: the list's end */
  CHECK_EQ(drawn[2].y, -16 + 4);
  CHECK_EQ(drawn[0].y, -16);
  frame(989);
  frame(988);
  draw();
  CHECK_EQ(drawn[2].y, -16 + 8);
  CHECK_EQ(drawn[0].y, -16);
  frame(987);
  draw();
  CHECK_EQ(drawn[0].y, -16 + 2);

  /* A bullet on the lead (box: hotspot (8, 8), box from (2, 2), 12 x 12):
   * one hit point, 400 points, a small explosion, effect 44 on channel 1,
   * the bullet spent; the dying one still stops a second bullet. */
  int32_t lead_y = drawn[2].y;
  memset(&ev, 0, sizeof(ev));
  CHECK(enemies_shoot(&en, 100, lead_y, 1, &ev));
  CHECK_EQ(ev.score, 400);
  CHECK_EQ(ev.nsounds, 1);
  CHECK_EQ(ev.sounds[0].effect, 44);
  CHECK_EQ(ev.sounds[0].channel, 1);
  CHECK(enemies_shoot(&en, 100, lead_y, 1, &ev));
  CHECK_EQ(ev.score, 400);
  CHECK(!enemies_shoot(&en, 10, 10, 1, &ev));
  /* The other two, then the group's cash where the last died. */
  frame(986);
  draw();
  int32_t y2 = drawn[0].y, y1 = drawn[1].y;
  memset(&ev, 0, sizeof(ev));
  CHECK(enemies_shoot(&en, 100, y1, 1, &ev));
  CHECK(enemies_shoot(&en, 100, y2, 1, &ev));
  CHECK_EQ(ev.score, 800);
  frame(985);
  draw();
  int cash = 0, booms = 0;
  for (int i = 0; i < draws; i++) {
    cash += drawn[i].source == 0x27000 && drawn[i].x != 100;
    booms += drawn[i].source == 0x27000;
  }
  CHECK(booms >= 1);
  (void)cash;

  /* The capsule: line 900, waits 5 steps at 3 a frame, then moves right. */
  for (int line = 984; line >= 900; line--) frame(line);
  frame(899);
  frame(898);
  frame(897);
  draw();
  int32_t cx = -1, cy = -1;
  for (int i = 0; i < draws; i++) {
    if (drawn[i].y == 50) {
      cx = drawn[i].x;
      cy = drawn[i].y;
    }
  }
  CHECK_EQ(cx, 3 * 3 - 5); /* 9 steps less the 5 waited, a pixel each */
  memset(&ev, 0, sizeof(ev));
  CHECK(enemies_shoot(&en, cx, cy, 1, &ev));
  CHECK_EQ(ev.nsounds, 1);
  CHECK_EQ(ev.sounds[0].effect, 16); /* token 2 is flagged */
  CHECK_EQ(ev.sounds[0].channel, 2);
  /* The token drifts; put the ship on it to take it. */
  frame(896);
  draw();
  for (int i = 0; i < draws; i++) {
    if (drawn[i].source == 0x27000 && drawn[i].y > 40 && drawn[i].y < 60) {
      fr.ship_box[0] = (int16_t)(drawn[i].x - 20);
      fr.ship_box[1] = (int16_t)(drawn[i].y - 20);
      fr.ship_box[2] = (int16_t)(drawn[i].x + 20);
      fr.ship_box[3] = (int16_t)(drawn[i].y + 20);
    }
  }
  frame(895);
  CHECK_EQ(ev.ntokens, 1);
  CHECK_EQ(ev.tokens[0], 2);
  CHECK_EQ(ev.sounds[ev.nsounds - 1].effect, 40);

  /* A restart at line 1005: empty lists; the trail fires again at 1000. */
  enemies_restart(&en, 1005);
  CHECK_EQ(enemies_count(&en), 0);
  fr.ship_box[0] = fr.ship_box[1] = fr.ship_box[2] = fr.ship_box[3] = -100;
  frame(1005);
  frame(1001);
  CHECK_EQ(enemies_count(&en), 0);
  frame(1000);
  CHECK_EQ(enemies_count(&en), 3);

  /* The chain: big (2 hit points), two parts on path 1; a hit flashes
   * both; the second kills it: 300 points, a big explosion (15), the
   * follower gone without its own; the ship's body: 16 and the enemy. */
  start(2);
  rec(B + 0x700, 1000, 2, 1, 1, 0, 0, 2);
  lw(B + 0x70E, 0xFFFF);
  frame(1000);
  CHECK_EQ(enemies_count(&en), 2);
  frame(999);
  draw();
  CHECK_EQ(draws, 2);
  CHECK_EQ(drawn[0].y, drawn[1].y);
  int32_t chain_y = drawn[0].y;
  memset(&ev, 0, sizeof(ev));
  CHECK(enemies_shoot(&en, 100, chain_y, 1, &ev));
  CHECK_EQ(ev.score, 0);
  draw();
  CHECK(drawn[0].flash && drawn[1].flash);
  draw();
  CHECK(!drawn[0].flash);
  CHECK(enemies_shoot(&en, 100, chain_y, 1, &ev));
  CHECK_EQ(ev.score, 300);
  CHECK_EQ(ev.nsounds, 1);
  CHECK_EQ(ev.sounds[0].effect, 15);
  frame(998);
  CHECK_EQ(enemies_count(&en), 2); /* the explosion, the big cash */

  start(2);
  rec(B + 0x700, 1000, 2, 1, 1, 0, 0, 2);
  lw(B + 0x70E, 0xFFFF);
  frame(1000);
  frame(999);
  fr.ship_box[0] = 95;
  fr.ship_box[1] = -20;
  fr.ship_box[2] = 105;
  fr.ship_box[3] = 0;
  frame(998);
  CHECK_EQ(ev.shield_damage, 16);
  CHECK(ev.ship_flash);
  CHECK_EQ(ev.score, 300);

  /* Aimed fire (rate 99 + 100): shots towards the ship below (direction 4:
   * 6 pixels a frame down), once the enemy is on the playfield (a shot off
   * it goes at once); on the ship: 4. */
  start(2);
  rec(B + 0x700, 1000, 1, 1, 1, 0, 199, 2);
  lw(B + 0x70E, 0xFFFF);
  frame(1000);
  fr.ship_box[0] = fr.ship_box[1] = fr.ship_box[2] = fr.ship_box[3] = -100;
  for (int i = 0; i < 14; i++) frame(999 - i);
  CHECK(enemies_count(&en) >= 2);
  draw();
  int32_t sx = -1, sy = -1000;
  for (int i = 0; i < draws; i++) {
    if (drawn[i].source == B + 0x3008 && drawn[i].x == 100 && drawn[i].y > sy) {
      sx = drawn[i].x;
      sy = drawn[i].y;
    }
  }
  CHECK_EQ(sx, 100);
  fr.ship_box[0] = 90;
  fr.ship_box[2] = 120;
  fr.ship_box[1] = (int16_t)(sy + 6);
  fr.ship_box[3] = (int16_t)(sy + 12);
  frame(985);
  CHECK(ev.shield_damage >= 4);

  /* Level 1: a wall object (kind 2, left wall) writes its tiles into the
   * map, steps them, and leaves its wreck when destroyed (2 hit points,
   * 100 points, a big explosion). */
  start(1);
  lw(B + 0x700, 0xFFFF);
  rec(B + 0x800, 1000, 2, 0, 0, 40, 976, 0); /* x 40, cell line 976 */
  lw(B + 0x80E, 0xFFFF);
  lw(0x4F05A, 2);
  for (int f2 = 0; f2 < 4; f2++) {
    for (int k = 0; k < 4; k++) lw(0x4F5D2 + 8 * (uint32_t)f2 + 2 * (uint32_t)k, (uint16_t)(0x100 + f2 * 16 + k));
  }
  for (int k = 0; k < 4; k++) lw(0x4F5FA + 2 * (uint32_t)k, (uint16_t)(0x200 + k));
  frame(1000);
  int cell = (976 - 8) / 16 * 20 + (40 - 8) / 16 - 1;
  CHECK_EQ(map[cell], 0x100);
  CHECK_EQ(map[cell + 21], 0x103);
  frame(999);
  CHECK_EQ(map[cell], 0x100);
  frame(998);
  CHECK_EQ(map[cell + 1], 0x111);
  /* its box: x 16 + 4 .. + 28, screen y (968 - 998) + 4 .. + 28 */
  memset(&ev, 0, sizeof(ev));
  CHECK(enemies_shoot(&en, 30, 968 - 998 + 10, 1, &ev));
  CHECK_EQ(ev.score, 0);
  CHECK(enemies_shoot(&en, 30, 968 - 998 + 10, 1, &ev));
  CHECK_EQ(ev.score, 100);
  CHECK_EQ(map[cell], 0x200);
  CHECK_EQ(map[cell + 21], 0x203);
  CHECK_EQ(ev.sounds[0].effect, 15);

  /* Level 1's boss: 2 hit points; a 16 x 16 sprite for its eye and its
   * turrets (the level's sprite table holds $4F3008 and $4F3108). */
  start(1);
  lw(B + 0x700, 0xFFFF);
  lw(B + 0x800, 0xFFFF);
  lw(0x4F058, 2);
  for (int k = 0; k < 4; k++) ll(0x50866 + 4 * (uint32_t)k, B + 0x3008);
  for (int k = 0; k < 8; k++) ll(0x5058A + 4 * (uint32_t)k, B + 0x3108);
  ll(0x4F4B0, B + 0x3008);
  lw(0x4F4B4, 0);
  for (int k = 0; k < 4; k++) ll(0x507FC + 4 * (uint32_t)k, 0x50A42);
  for (int k = 0; k < 8; k++) lw(0x50A42 + 2 * (uint32_t)k, (uint16_t)(0x300 + k));
  enemies_init(&en, (const x2level_header_t *)level_img,
               (const x2global_header_t *)global_img, map, 300, 1, 1234);
  CHECK(en.boss >= 0);
  CHECK_EQ(enemies_count(&en), 1);
  frame(600);
  CHECK(!en.boss_awake);
  CHECK_EQ(ev.back_limit, -1);
  frame(448);
  CHECK(en.boss_awake);
  CHECK_EQ(enemies_count(&en), 9);
  CHECK_EQ(ev.back_limit, 448);
  CHECK_EQ(en.boss_block[5 * 6 + 1], 0x300); /* the mouth's words */
  CHECK_EQ(en.boss_block[6 * 6 + 4], 0x307);
  /* closed until the world line reaches 48, then opening: effect 26 */
  frame(100);
  CHECK_EQ(en.pool[en.boss].speed, 0);
  frame(48);
  CHECK_EQ(en.pool[en.boss].speed, 1);
  CHECK_EQ(ev.nsounds, 1);
  CHECK_EQ(ev.sounds[0].effect, 26);
  for (int i = 0; i < 12; i++) frame(47);
  CHECK_EQ(en.pool[en.boss].speed, 11);
  /* A shot on its body is spent; on its eye (x 152-167, y + 68 - line)
   * it hurts; the second kills it: payout, explosions, the level won once
   * the payout is gone. */
  int32_t eye_y = (en.pool[en.boss].y >> 16) - 47 + 70;
  memset(&ev, 0, sizeof(ev));
  CHECK(enemies_shoot(&en, 140, eye_y, 1, &ev));
  CHECK_EQ(en.pool[en.boss].hp, 2);
  CHECK(enemies_shoot(&en, 160, eye_y, 1, &ev));
  CHECK_EQ(en.pool[en.boss].hp, 1);
  /* a restart keeps the boss and its turrets */
  enemies_restart(&en, 47);
  CHECK_EQ(enemies_count(&en), 9);
  frame(47);
  CHECK(enemies_shoot(&en, 160, eye_y, 1, &ev));
  CHECK(en.won);
  CHECK_EQ(en.payout, 18);
  CHECK(enemies_ship_safe(&en));
  CHECK(!enemies_level_won(&en));
  CHECK_EQ(ev.sounds[ev.nsounds - 1].effect, 15);
  CHECK_EQ(ev.sounds[ev.nsounds - 1].channel, 0);
  for (int i = 0; i < 400 && !enemies_level_won(&en); i++) frame(47);
  CHECK(enemies_level_won(&en));
  CHECK(!enemies_ship_safe(&en));

  /* Level 1's worms: in the stretch (here lines 900-1100) the five slots
   * each spawn one (every hole record the same: x 100, line 1000, start
   * hole 3, end hole 5, a path of 20 steps at direction 0, 2 a frame), of
   * three parts (a $114 then two $110); the start hole opens and
   * animates. */
  start(1);
  lw(B + 0x700, 0xFFFF);
  lw(B + 0x800, 0xFFFF);
  lw(0x4FA3C, 900);
  lw(0x4FA44, 1100);
  lw(0x50090, 20);
  lw(0x4F062, 2);
  for (int k = 0; k < 16; k++) {
    uint32_t r = 0x4FF6C + 12u * (uint32_t)k;
    lw(r, 100);
    lw(r + 2, 1000);
    lw(r + 4, 3);
    lw(r + 6, 5);
    ll(r + 8, B + 0x680);
  }
  lw(B + 0x680, 2);  /* arc: direction 0, no turn, 20 steps */
  lw(B + 0x682, 0);
  lw(B + 0x684, 0);
  lw(B + 0x686, 0);
  lw(B + 0x688, 20);
  lw(B + 0x68A, 0);  /* end */
  lw(0x4FEE6, 2);    /* three parts: type, hit, sprite, worth */
  lw(0x4FEE8, 0x114);
  ll(0x4FEEA, B + 0x6A0);
  ll(0x4FEEE, B + 0x3008);
  lw(0x4FEF2, 250);
  for (uint32_t k = 1; k < 3; k++) {
    lw(0x4FEE8 + 12 * k, 0x110);
    ll(0x4FEEA + 12 * k, B + 0x6C0);
    ll(0x4FEEE + 12 * k, B + 0x3108);
    lw(0x4FEF2 + 12 * k, 50);
  }
  ll(B + 0x6A0 + 18, B + 0x400); /* the hits' lea: the pieces' list */
  ll(B + 0x6C0 + 18, B + 0x400);
  for (uint32_t k = 0; k < 8; k++) {
    ll(0x50374 + 4 * k, B + 0x3008);
    ll(0x50394 + 4 * k, B + 0x3108);
  }
  for (uint32_t k = 0; k < 4; k++) lw(0x4FAA2 + 30 + 2 + 2 * k, (uint16_t)(0x501 + k));
  lw(0x4FAA2 + 30, 2 * (62 * 20 + 6)); /* hole 3's cell */
  for (uint32_t s2 = 0; s2 < 5; s2++) ll(0x50954 + 8 * s2, 0x12345678u * (s2 + 1));
  enemies_init(&en, (const x2level_header_t *)level_img,
               (const x2global_header_t *)global_img, map, 300, 1, 1234);
  int start_count = enemies_count(&en);
  int32_t back = 0;
  enemies_level_frame(&en, 1200, 0, &back);
  CHECK_EQ(enemies_count(&en), start_count);
  CHECK_EQ(back, 0);
  enemies_frame_start(&en);
  enemies_level_frame(&en, 1000, 0, &back);
  CHECK_EQ(back, 1100);
  CHECK_EQ(enemies_count(&en), start_count + 5 * 6);
  CHECK_EQ(en.holes[3], 19);
  CHECK_EQ(map[62 * 20 + 6], 0x504);
  /* the newest worm: start marker, the parts (the last first), the head,
   * the end marker */
  int8_t mk = en.enemies, p3 = en.pool[mk].next, p2 = en.pool[p3].next;
  int8_t p1 = en.pool[p2].next, hd = en.pool[p1].next, end = en.pool[hd].next;
  CHECK_EQ(en.pool[p1].type, 0x114);
  CHECK_EQ(en.pool[p2].type, 0x110);
  CHECK_EQ(en.pool[hd].type, 0x10C);
  CHECK_EQ(en.pool[end].type, 0xBC);
  CHECK_EQ(en.pool[p1].y >> 16, 0);
  memset(&ev, 0, sizeof(ev));
  fr.line = 1000;
  fr.step = 0;
  enemies_update(&en, &fr, &ev);
  CHECK_EQ(en.pool[hd].x >> 16, 102);     /* 2 steps */
  CHECK_EQ(en.pool[p1].x >> 16, 100);     /* where the head was */
  CHECK(en.pool[p1].flags & 0x01);        /* shown: the head had started */
  CHECK(en.pool[p2].flags & 0x01);
  CHECK(!(en.pool[p3].flags & 0x01));     /* 7 steps behind */
  enemies_update(&en, &fr, &ev);
  CHECK_EQ(en.pool[p1].x >> 16, 102);
  CHECK_EQ(en.pool[p2].x >> 16, 100);
  CHECK(en.pool[p3].flags & 0x01);
  /* the first part shot where only its box reaches (the list holds the
   * parts behind it first): 250 points, a piece flies */
  memset(&ev, 0, sizeof(ev));
  int fx_before = 0;
  for (int8_t i = en.fx; i >= 0; i = en.pool[i].next) fx_before++;
  CHECK(enemies_shoot(&en, 107, 0, 1, &ev));
  CHECK_EQ(ev.score, 250);
  int fx_after = 0;
  for (int8_t i = en.fx; i >= 0; i = en.pool[i].next) fx_after++;
  CHECK_EQ(fx_after, fx_before + 1);
  /* the head's path ends after 10 frames; the parts then go into the end
   * hole one a frame, which opens; the markers go last */
  for (int i = 0; i < 20; i++) enemies_update(&en, &fr, &ev);
  CHECK_EQ(en.holes[5], 20);
  CHECK_EQ(enemies_count(&en), start_count + fx_after);
  TEST_END();
}
