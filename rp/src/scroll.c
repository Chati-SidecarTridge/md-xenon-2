/**
 * File: scroll.c
 * Description: The original's scroll (scroll.h): its level loop's update of
 *              the world line and the backdrop pointer, and the ship
 *              control's choice of step, as one original frame.
 */

#include "scroll.h"

#define BACKDROP_LINES 192

/* The 68000's asr: division rounding down. */
static int32_t half_down(int32_t v) { return v >= 0 ? v / 2 : -((1 - v) / 2); }

/* The backdrop moves half as fast as the map: half the last step, and the
 * odd half on odd frames. */
static void backdrop_step(scroll_t *s) {
  int32_t d = half_down(s->last_step) +
              (int32_t)(s->frame & 1u & (uint32_t)s->last_step);
  int32_t b = ((int32_t)s->backdrop - d) % BACKDROP_LINES;
  s->backdrop = (uint32_t)(b < 0 ? b + BACKDROP_LINES : b);
}

static void begin_frame(scroll_t *s) {
  s->frame++;
  backdrop_step(s);
}

void scroll_start(scroll_t *s) {
  s->line = SCROLL_START_LINE;
  s->speed = 1;
  s->step = 0;
  s->last_step = 1;
  s->off_speed = 0;
  s->lowest = 0;
  s->back_limit = SCROLL_START_LINE;
  s->window = SCROLL_WINDOW;
  s->frame = 0;
  s->backdrop = 0;
  begin_frame(s);
}

int32_t scroll_ship_step(const scroll_t *s, uint8_t stick, int32_t ship_y) {
  if (stick & SCROLL_STICK_UP) {
    if (ship_y <= SCROLL_SHIP_TOP) return 1;
  } else if (stick & SCROLL_STICK_DOWN) {
    if (ship_y >= SCROLL_SHIP_BOTTOM && s->line != s->back_limit) return -1;
  }
  return s->speed;
}

void scroll_tick(scroll_t *s, uint8_t stick, int32_t ship_y) {
  scroll_advance(s, scroll_ship_step(s, stick, ship_y), stick);
}

void scroll_advance(scroll_t *s, int32_t step, uint8_t stick) {
  s->step = step;
  int32_t old = s->line;
  int32_t d = s->step;
  if (d == s->speed) {
    s->off_speed = 0;
  } else if (++s->off_speed >= SCROLL_DOUBLE_AFTER) {
    d *= 2;
    s->step = d;
  }
  int32_t line = s->line - d;
  if (line < s->lowest) {
    d += line - s->lowest;
    line = s->lowest;
  }
  if (line > s->back_limit) {
    d += line - s->back_limit;
    line = s->back_limit;
  }
  s->last_step = d;
  s->line = line;
  int32_t limit = line;
  if (!(old == s->back_limit && (stick & SCROLL_STICK_DOWN))) {
    limit += s->window;
  }
  if (limit <= s->back_limit) s->back_limit = limit;

  begin_frame(s);
}

void scroll_jump(scroll_t *s, int32_t line) {
  s->line = line;
  s->back_limit = line;
}

void scroll_seek(scroll_t *s, int32_t line) {
  scroll_start(s);
  if (line < 0) line = 0;
  if (line > SCROLL_START_LINE) line = SCROLL_START_LINE;
  /* After n frames forward: frame n + 1, the backdrop moved on every odd
   * frame, the back limit a window behind (never past the start). */
  uint32_t frame = (uint32_t)(SCROLL_START_LINE - line) + 1u;
  s->line = line;
  s->frame = frame;
  s->backdrop = (BACKDROP_LINES - (frame + 1u) / 2u % BACKDROP_LINES) %
                BACKDROP_LINES;
  s->back_limit = line + s->window < SCROLL_START_LINE ? line + s->window
                                                       : SCROLL_START_LINE;
}

void scroll_restart(scroll_t *s, int32_t line) {
  s->line = line;
  s->back_limit = line;
}

void scroll_pace_init(scroll_pace_t *p, uint32_t vbls_per_frame_q8) {
  p->phase_q8 = 0;
  p->vbls_per_frame_q8 = vbls_per_frame_q8 ? vbls_per_frame_q8 : 1;
}

uint32_t scroll_pace(scroll_pace_t *p) {
  uint32_t frames = 0;
  p->phase_q8 += 256u;
  while (p->phase_q8 >= p->vbls_per_frame_q8) {
    p->phase_q8 -= p->vbls_per_frame_q8;
    frames++;
  }
  return frames;
}
