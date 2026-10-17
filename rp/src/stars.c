/**
 * File: stars.c
 * Description: The stars (stars.h).
 */

#include "stars.h"

#define MAX_STEP 4 /* the original's speed table covers steps -4..4 */

static uint32_t next(stars_t *s) {
  /* xorshift32: the start only has to look random */
  uint32_t x = s->random;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->random = x;
  return x;
}

void stars_init(stars_t *s, uint32_t seed) {
  s->random = seed ? seed : 0x2545F491u;
  for (int i = 0; i < STARS_COUNT; i++) {
    s->star[i].line = (uint16_t)(next(s) % STARS_LINES);
    s->star[i].x = (uint16_t)(next(s) % STARS_WIDTH);
    s->star[i].fraction = 0;
  }
}

void stars_move(stars_t *s, int32_t step) {
  if (step > MAX_STEP) step = MAX_STEP;
  if (step < -MAX_STEP) step = -MAX_STEP;
  for (int i = 0; i < STARS_COUNT; i++) {
    int32_t f = s->star[i].fraction + step * (STARS_FRACTION + 8 * i);
    int32_t line = s->star[i].line;
    while (f < 0) {
      f += STARS_FRACTION;
      line = line ? line - 1 : STARS_LINES - 1;
    }
    while (f >= STARS_FRACTION) {
      f -= STARS_FRACTION;
      line = line + 1 < STARS_LINES ? line + 1 : 0;
    }
    s->star[i].line = (uint16_t)line;
    s->star[i].fraction = (int16_t)f;
  }
}

void stars_draw(const stars_t *s, uint8_t *dst) {
  for (int i = 0; i < STARS_COUNT; i++) {
    uint8_t *p = dst + s->star[i].line * STARS_WIDTH + s->star[i].x;
    if (*p == 0) *p = (uint8_t)(4 + i / STARS_GROUP);
  }
}
