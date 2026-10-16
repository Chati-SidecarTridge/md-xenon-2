// units: rp/src/scroll.c
/* The scroll model against the original: the positions of level 1 at the
 * frames captured in Hatari, the steady speed of a line a frame with the
 * backdrop at half, the stop at the level's end, the 16-line reverse
 * window, the doubling after 35 frames off the set speed, and the 50 Hz
 * pacing. */
#include "scroll.h"
#include "test.h"

/* Frame f is drawn after f - 1 ticks. */
static void at_frame(scroll_t *s, uint32_t f) {
  scroll_start(s);
  for (uint32_t i = 1; i < f; i++) scroll_tick(s, 0, 100);
}

int main(void) {
  scroll_t s;

  /* Captured in the original (screen dumps at the scenery drawer): frame
   * 120 shows map offset 11200 with fine scroll 9 (line 4489) and the
   * backdrop pointer 132 lines into the backdrop. */
  at_frame(&s, 120);
  CHECK_EQ(s.line, 280 * 16 + 9);
  CHECK_EQ(s.backdrop, 132);
  CHECK_EQ(s.frame, 120);

  /* A line a frame, the backdrop half as fast on odd frames. */
  scroll_start(&s);
  for (uint32_t f = 1; f < 4000; f++) {
    CHECK_EQ(s.line, 4608 - (int32_t)(f - 1));
    CHECK_EQ(s.backdrop, (192 * 100 - (f + 1) / 2) % 192);
    scroll_tick(&s, 0, 100);
  }

  /* The level's end: the scroll stops at the lowest line; the backdrop
   * stops with it. */
  at_frame(&s, 4700);
  CHECK_EQ(s.line, 0);
  uint32_t bd = s.backdrop;
  scroll_tick(&s, 0, 100);
  scroll_tick(&s, 0, 100);
  CHECK_EQ(s.line, 0);
  CHECK_EQ(s.backdrop, bd);

  /* Back: no room at the start (the world goes on forward); then at most
   * 16 lines behind the furthest point, and held there the stick lets the
   * world go forward again with the limit at its heels: no room left. */
  scroll_start(&s);
  scroll_tick(&s, SCROLL_STICK_DOWN, SCROLL_SHIP_BOTTOM);
  CHECK_EQ(s.line, 4607);
  for (int i = 0; i < 40; i++) scroll_tick(&s, 0, 100);
  int32_t far = s.line;
  for (int i = 0; i < 16; i++) {
    scroll_tick(&s, SCROLL_STICK_DOWN, SCROLL_SHIP_BOTTOM);
  }
  CHECK_EQ(s.line, far + 16);
  scroll_tick(&s, SCROLL_STICK_DOWN, SCROLL_SHIP_BOTTOM);
  CHECK_EQ(s.line, far + 15);
  CHECK_EQ(s.back_limit, s.line);
  /* The ship not at the bottom does not move the world back. */
  scroll_start(&s);
  for (int i = 0; i < 40; i++) scroll_tick(&s, 0, 100);
  far = s.line;
  scroll_tick(&s, SCROLL_STICK_DOWN, 100);
  CHECK_EQ(s.line, far - 1);

  /* A stopped level (speed 0): the ship pushed against the top moves the
   * world a line a frame, two after 35 frames. */
  scroll_start(&s);
  s.speed = 0;
  scroll_tick(&s, 0, 100);
  far = s.line;
  for (int i = 0; i < 34; i++) scroll_tick(&s, SCROLL_STICK_UP, 16);
  CHECK_EQ(s.line, far - 34);
  scroll_tick(&s, SCROLL_STICK_UP, 16);
  CHECK_EQ(s.line, far - 36);
  scroll_tick(&s, 0, 100);
  CHECK_EQ(s.line, far - 36);
  CHECK_EQ(s.off_speed, 0);

  /* Seeking gives the state reached by scrolling there from the start. */
  static const int32_t lines[] = {4608, 4607, 4600, 4592, 3000, 2496, 17, 1};
  for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
    scroll_t a, b;
    at_frame(&a, (uint32_t)(4608 - lines[i]) + 1u);
    scroll_seek(&b, lines[i]);
    CHECK_EQ(b.line, a.line);
    CHECK_EQ(b.backdrop, a.backdrop);
    CHECK_EQ(b.frame, a.frame);
    CHECK_EQ(b.back_limit, a.back_limit);
  }

  /* Pacing: three VBLs a frame, then a factor of 2.5. */
  scroll_pace_t p;
  scroll_pace_init(&p, SCROLL_VBLS_PER_FRAME_Q8);
  uint32_t frames = 0;
  for (int v = 0; v < 300; v++) {
    uint32_t n = scroll_pace(&p);
    CHECK(n <= 1);
    frames += n;
  }
  CHECK_EQ(frames, 100);
  scroll_pace_init(&p, 640);
  frames = 0;
  for (int v = 0; v < 500; v++) frames += scroll_pace(&p);
  CHECK_EQ(frames, 200);
  TEST_END();
}
