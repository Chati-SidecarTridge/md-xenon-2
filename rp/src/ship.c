/**
 * File: ship.c
 * Description: The Megablaster (ship.h), after the original's routines:
 *              the ship's update ($6734: the explosion $6744, the rewind
 *              $67F4-$687E, the control $661C, the history and the scenery
 *              test $6886-$68D4), the gun ($5DC8, the spawn $6990), the
 *              bullets ($6086), the timers ($6BA8: the death counter, held
 *              fire $6C40), the scenery test ($387C).
 */

#include "ship.h"

#include <string.h>

#define FIRE_PERIOD 8
#define BULLET_DY (-9)
#define BULLET_ABOVE 6
#define BULLET_MAX_X 312u /* unsigned: past either side */
#define BULLET_MAX_Y 192u /* unsigned: past the top or the playfield */

/* The 68000's asr #1: division rounding down. */
static int32_t half_down(int32_t v) { return v >= 0 ? v / 2 : -((1 - v) / 2); }

void ship_game_start(ship_t *s) {
  memset(s, 0, sizeof(*s));
  s->shield = SHIP_SHIELD_FULL;
  s->lives = SHIP_LIVES;
  s->continues = SHIP_CONTINUES;
  s->fire_count = FIRE_PERIOD;
  s->fire_period = FIRE_PERIOD;
  s->fire_rate = 1;
}

void ship_place(ship_t *s, int32_t x, int32_t line) {
  s->state = SHIP_FLYING;
  s->x = x;
  s->y = SHIP_START_Y;
  s->bank = 0;
  s->stick = 0;
  s->fire_latch = false;
  s->rewind = 0;
  s->explosion = 0;
  memset(s->bullets, 0, sizeof(s->bullets));
  for (int i = 0; i < SHIP_HISTORY; i++) {
    s->history[i].line = line;
    s->history[i].x = s->x;
    s->history[i].y = s->y;
  }
}

void ship_die(ship_t *s) {
  if (s->state != SHIP_FLYING) return;
  s->died = true;
  s->state = SHIP_EXPLODING;
  s->explosion = 0;
  s->shield = 0;
}

void ship_hit(ship_t *s, int32_t damage) {
  if (s->state != SHIP_FLYING) return;
  int32_t before = s->shield;
  s->shield -= damage;
  if (before <= damage) ship_die(s);
}

bool ship_touches(const ship_hitmask_t *hit, int32_t x, int32_t y,
                  ship_solid_fn solid, void *ctx) {
  int32_t left = x - hit->hot_x, top = y - hit->hot_y;
  for (int32_t r = 0; r < hit->height; r++) {
    const uint8_t *row = hit->mask + r * hit->width;
    for (int32_t c = 0; c < hit->width; c++) {
      if (row[c] && solid(ctx, left + c, top + r)) return true;
    }
  }
  return false;
}

/* The control ($661C): the bank, then the step and the moves up and down,
 * then the move across. Returns the scroll's step. */
static int32_t control(ship_t *s, const scroll_t *sc) {
  uint8_t stick = s->stick;
  if (stick & SHIP_STICK_LEFT) {
    if (s->bank > 0) s->bank = 0;
    if (s->bank > -SHIP_BANK_MAX) s->bank--;
  } else if (stick & SHIP_STICK_RIGHT) {
    if (s->bank < 0) s->bank = 0;
    if (s->bank < SHIP_BANK_MAX) s->bank++;
  } else if (s->bank < 0) {
    s->bank++;
  } else if (s->bank > 0) {
    s->bank--;
  }

  int32_t step = sc->speed;
  int32_t climb = 3 + s->speed_ups;
  if (stick & SCROLL_STICK_UP) {
    if (s->y > SCROLL_SHIP_TOP) {
      s->y -= climb;
    } else {
      s->y = SCROLL_SHIP_TOP;
      step = 1;
    }
  } else if (stick & SCROLL_STICK_DOWN) {
    if (s->y < SCROLL_SHIP_BOTTOM) {
      s->y += climb;
    } else if (sc->line != sc->back_limit) {
      step = -1;
      s->y = SCROLL_SHIP_BOTTOM;
    }
  }

  int32_t d = s->bank;
  if (d > 0 && (stick & SHIP_STICK_RIGHT)) d = SHIP_BANK_MAX;
  if (d < 0 && (stick & SHIP_STICK_LEFT)) d = -SHIP_BANK_MAX;
  if (s->speed_ups == 0) d = half_down(d);
  if (s->speed_ups >= 2) d += half_down(d);
  s->x += d;
  if (s->x < SHIP_MIN_X) s->x = SHIP_MIN_X;
  if (s->x > SHIP_MAX_X) s->x = SHIP_MAX_X;
  return step;
}

/* One step back along the recorded positions ($681C-$687E): the world
 * moves so the ship's old place fits the playfield. Returns the step. */
static int32_t rewind_step(ship_t *s, const scroll_t *sc) {
  s->rewind--;
  int32_t step = sc->speed;
  const ship_place_t *h = &s->history[1];
  int32_t y = h->line - sc->line + h->y;
  s->x = h->x;
  if (y > SCROLL_SHIP_BOTTOM) {
    step -= y - SCROLL_SHIP_BOTTOM;
    y = SCROLL_SHIP_BOTTOM;
  }
  if (y < SCROLL_SHIP_TOP) {
    step -= y - SCROLL_SHIP_TOP;
    y = SCROLL_SHIP_TOP;
  }
  s->y = y;
  memmove(&s->history[0], &s->history[1],
          (SHIP_HISTORY - 1) * sizeof(s->history[0]));
  if (s->rewind == 0) s->rewind = -1;
  return step;
}

/* The ship's update ($6734). Returns the scroll's step. */
static int32_t update(ship_t *s, scroll_t *sc, const ship_hitmask_t *hit,
                      ship_solid_fn solid, void *ctx) {
  if (s->state != SHIP_FLYING) {
    if (s->explosion < SHIP_EXPLOSION_FRAMES - 1) {
      s->explosion++;
    } else {
      s->explosion = SHIP_EXPLOSION_FRAMES; /* ended: nothing drawn */
      if (s->death_count >= SHIP_DEATH_COUNT) s->state = SHIP_DEAD;
    }
    return 0;
  }
  if (s->rewind != 0) {
    bool rewinding = true;
    if (s->rewind < 0) {
      if (!ship_touches(hit, s->x, s->y, solid, ctx)) {
        s->rewind = 0;
        rewinding = false;
      } else if (s->rewind == SHIP_CRUSH) {
        ship_die(s);
        return sc->step; /* the original leaves its step as it was */
      }
    }
    if (rewinding) return rewind_step(s, sc);
  }

  int32_t step = control(s, sc);
  memmove(&s->history[1], &s->history[0],
          (SHIP_HISTORY - 1) * sizeof(s->history[0]));
  s->history[0].line = sc->line;
  s->history[0].x = s->x;
  s->history[0].y = s->y;
  if (ship_touches(hit, s->x, s->y, solid, ctx)) {
    s->rewind = SHIP_REWIND;
    s->bank = 0;
    int32_t limit = sc->line + sc->window;
    if (limit > sc->back_limit) sc->back_limit = limit;
  }
  return step;
}

/* The gun ($5DC8) and the bullets ($6086). */
static void weapons(ship_t *s, bool shot) {
  if (shot && s->state == SHIP_FLYING) {
    s->shot = true;
    memmove(&s->bullets[1], &s->bullets[0],
            (SHIP_BULLETS - 1) * sizeof(s->bullets[0]));
    s->bullets[0] = (ship_bullet_t){s->x, s->y - BULLET_ABOVE, 0, BULLET_DY,
                                    s->power, true};
  }
  for (int i = 0; i < SHIP_BULLETS; i++) {
    ship_bullet_t *b = &s->bullets[i];
    if (!b->alive) continue;
    if (s->state != SHIP_FLYING) {
      b->alive = false;
      continue;
    }
    b->x += b->dx;
    b->y += b->dy;
    if ((uint32_t)b->x >= BULLET_MAX_X || (uint32_t)b->y >= BULLET_MAX_Y) {
      b->alive = false;
    }
  }
}

/* The timers ($6BA8): the death counter and held fire. */
static void timers(ship_t *s) {
  if (s->state != SHIP_FLYING) {
    if (s->death_count != SHIP_DEATH_COUNT) s->death_count++;
  } else if (s->death_count > 0) {
    s->death_count--;
  }
  if (s->stick & SHIP_STICK_FIRE) {
    s->fire_count -= s->fire_rate;
    if (s->fire_count <= 0) {
      s->fire_count += s->fire_period;
      s->fire_latch = true;
    }
  } else {
    s->fire_count = s->fire_period;
  }
}

int32_t ship_tick(ship_t *s, uint8_t stick, bool fire_pressed, scroll_t *sc,
                  const ship_hitmask_t *hit, ship_solid_fn solid, void *ctx) {
  s->stick = stick;
  s->shot = s->died = false;
  if (fire_pressed) s->fire_latch = true;
  int32_t step = update(s, sc, hit, solid, ctx);
  bool shot = s->fire_latch;
  s->fire_latch = false;
  weapons(s, shot);
  timers(s);
  s->frames++;
  return step;
}
