// units: rp/src/ship.c rp/src/scroll.c rp/src/stars.c
/* The ship against the original's numbers, measured in Hatari on level 1
 * at speed 0: 3 pixels a frame across while held, the glide back by half
 * the bank (right 2, 2, 1, 1; left 3, 2, 2, 1, 1), 3 pixels up and down,
 * the top bound; a shot asked as fire is pressed, then every 8 frames held.
 * The scenery: a touch rewinds the ship; touching for 17 frames crushes it;
 * the explosion's 9 frames, then the death after its count. The dive: four
 * shrinking frames down, 136 frames under (no scenery test), four back up,
 * the rewind counter at -15. And the stars' speeds. No game data. */
#include <string.h>

#include "ship.h"
#include "stars.h"
#include "test.h"

static const uint8_t one = 1;
static const ship_hitmask_t dot = {&one, 1, 1, 0, 0}; /* the hotspot alone */
static int32_t wall_below = 1000;                      /* solid at y >= it */

static bool solid(void *ctx, int32_t x, int32_t y) {
  (void)ctx;
  (void)x;
  return y >= wall_below;
}

static ship_t s;
static scroll_t sc;

static void start(void) {
  ship_game_start(&s);
  ship_place(&s, SHIP_START_X, SCROLL_START_LINE);
  scroll_start(&sc);
  wall_below = 1000;
}

static int32_t frame(uint8_t stick, bool press) {
  int32_t step = ship_tick(&s, stick, press, &sc, &dot, solid, NULL);
  scroll_advance(&sc, step, stick);
  return step;
}

int main(void) {
  /* Across: 3 a frame held, the bank up to 6, the glide back. */
  start();
  int32_t x = s.x;
  for (int i = 1; i <= 8; i++) {
    frame(SHIP_STICK_RIGHT, false);
    CHECK_EQ(s.x - x, 3);
    CHECK_EQ(s.bank, i < 6 ? i : 6);
    x = s.x;
  }
  static const int right_glide[] = {2, 2, 1, 1, 0, 0};
  for (int i = 0; i < 6; i++) {
    frame(0, false);
    CHECK_EQ(s.x - x, right_glide[i]);
    x = s.x;
  }
  for (int i = 0; i < 8; i++) frame(SHIP_STICK_LEFT, false);
  x = s.x;
  static const int left_glide[] = {-3, -2, -2, -1, -1, 0};
  for (int i = 0; i < 6; i++) {
    frame(0, false);
    CHECK_EQ(s.x - x, left_glide[i]);
    x = s.x;
  }
  for (int i = 0; i < 200; i++) frame(SHIP_STICK_LEFT, false);
  CHECK_EQ(s.x, SHIP_MIN_X);

  /* Up 3 a frame to the top; held there, the world moves on. */
  start();
  frame(SCROLL_STICK_UP, false);
  CHECK_EQ(s.y, SHIP_START_Y - 3);
  for (int i = 0; i < 100; i++) frame(SCROLL_STICK_UP, false);
  CHECK_EQ(s.y, SCROLL_SHIP_TOP);
  for (int i = 0; i < 100; i++) frame(SCROLL_STICK_DOWN, false);
  CHECK_EQ(s.y, SCROLL_SHIP_BOTTOM);

  /* Fire: a press asks a shot at once, held fire every 8 frames. */
  start();
  frame(SHIP_STICK_FIRE, true);
  CHECK(s.shot_request);
  int shots = 1, last = 0;
  for (int f = 1; f <= 40; f++) {
    frame(SHIP_STICK_FIRE, false);
    if (s.shot_request) {
      CHECK_EQ(f - last, 8);
      last = f;
      shots++;
    }
  }
  CHECK_EQ(shots, 6);
  frame(0, false);
  CHECK(!s.shot_request);

  /* The dive: none without dives; with one, frames 1-4 down, under for
   * 136 frames with no scenery test (a wall all over), then 3, 2, 1 and
   * the surface with the rewind counter at -15: two steps back, then
   * crushed. */
  start();
  ship_dive_key(&s);
  CHECK_EQ(s.dive, 0);
  s.dives = 1;
  ship_dive_key(&s);
  CHECK_EQ(s.dive, 1);
  CHECK_EQ(s.dives, 0);
  CHECK(s.dive_sound);
  static const int down[] = {1, 2, 3, 4, 4};
  for (int i = 0; i < 5; i++) {
    frame(0, false);
    CHECK_EQ(s.dive_frame, down[i]);
  }
  CHECK(s.dive_clipped);
  wall_below = 0;
  for (int i = 0; i < 128; i++) frame(0, false);
  CHECK(s.state == SHIP_FLYING);
  CHECK_EQ(s.rewind, 0);
  CHECK_EQ(s.death_count, SHIP_DEATH_COUNT); /* the weapons flung away */
  int up = 0;
  while (s.dive && up++ < 20) frame(0, false);
  CHECK_EQ(s.rewind, SHIP_SURFACED);
  for (int i = 0; i < 3; i++) frame(0, false);
  CHECK(s.state == SHIP_EXPLODING);
  wall_below = 1000;
  /* Space while under brings it up at once */
  start();
  s.dives = 1;
  ship_dive_key(&s);
  for (int i = 0; i < 6; i++) frame(0, false);
  ship_dive_key(&s);
  frame(0, false);
  CHECK_EQ(s.dive_dir, -1);

  /* The scenery: a wall just above the ship as it climbs. One touch, one
   * frame back along its path; free again, it flies on. */
  start();
  for (int i = 0; i < 3; i++) frame(0, false);
  wall_below = 0;
  int32_t y = s.y;
  frame(SCROLL_STICK_UP, false); /* climbs into the wall */
  CHECK_EQ(s.rewind, SHIP_REWIND);
  wall_below = 1000;
  frame(SCROLL_STICK_UP, false); /* rewinds one place */
  CHECK_EQ(s.y, y);
  CHECK_EQ(s.rewind, -1);
  frame(SCROLL_STICK_UP, false); /* free: flies again */
  CHECK_EQ(s.rewind, 0);
  CHECK_EQ(s.y, y - 3);

  /* Crushed: touching from the first frame. The touch, 16 more frames
   * back, the death at the 19th frame; the explosion's 9 frames; the
   * death counted to 16, then the life is lost. */
  start();
  wall_below = 0;
  int f = 0;
  while (s.state == SHIP_FLYING && f < 100) {
    frame(0, false);
    f++;
  }
  CHECK_EQ(f, 19);
  CHECK_EQ(s.state, SHIP_EXPLODING);
  CHECK_EQ(s.explosion, 0);
  CHECK_EQ(s.shield, 0);
  for (int i = 1; i <= 8; i++) {
    CHECK_EQ(frame(0, false), 0); /* the world stands */
    CHECK_EQ(s.explosion, i);
  }
  int more = 0;
  while (s.state == SHIP_EXPLODING && more < 100) {
    frame(0, false);
    more++;
  }
  CHECK_EQ(s.explosion, SHIP_EXPLOSION_FRAMES);
  CHECK_EQ(s.state, SHIP_DEAD);
  CHECK_EQ(f + 8 + more, 35);

  /* Stars: star i moves (384 + 8 i) / 384 lines a step. */
  stars_t st;
  stars_init(&st, 1);
  uint16_t l0 = st.star[0].line, l47 = st.star[47].line;
  for (int i = 0; i < 384; i++) stars_move(&st, 1);
  CHECK_EQ(st.star[0].line, (l0 + 384) % 192);
  CHECK_EQ(st.star[47].line, (l47 + 384 + 8 * 47) % 192);
  uint8_t field[192 * 320];
  memset(field, 0, sizeof(field));
  field[st.star[3].line * 320 + st.star[3].x] = 9; /* scenery there */
  stars_draw(&st, field);
  CHECK_EQ(field[st.star[3].line * 320 + st.star[3].x], 9);
  CHECK_EQ(field[st.star[40].line * 320 + st.star[40].x], 7);
  TEST_END();
}
