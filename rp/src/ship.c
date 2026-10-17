/**
 * File: ship.c
 * Description: The Megablaster (ship.h), after the original's routines:
 *              the ship's update ($6734: the explosion $6744, the rewind
 *              $67F4-$687E, the control $661C, the history and the scenery
 *              test $6886-$68D4, the dive's frames $693A), the dive's key
 *              ($7B06), the timers ($6BA8: the death counter, the dive's
 *              count $6BE8, held fire $6C40), the scenery test ($387C). The
 *              weapons are weapons.c's.
 */

#include "ship.h"

#include <string.h>

#define FIRE_PERIOD 8

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
  s->dive = s->dive_dir = s->dive_time = s->dive_frame = 0;
  s->dive_clipped = false;
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

void ship_dive_key(ship_t *s) {
  if (s->state != SHIP_FLYING) return;
  if (s->dive == 0) {
    if (s->dives <= 0) return;
    s->dive_dir = 1;
    s->dive = 1;
    s->dives--;
    s->dive_time = SHIP_DIVE_FRAMES;
    s->dive_sound = true;
  } else {
    s->dive_time = 1;
  }
}

void ship_hit(ship_t *s, int32_t damage) {
  if (s->state != SHIP_FLYING || s->invulnerable) return;
  if (s->armour) damage /= 2;
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
  s->dive_frame = 0;
  s->dive_clipped = false;
  if (s->dive) { /* $693A: the frame of this step, then the next step */
    s->dive_frame = s->dive;
    s->dive_clipped = s->dive == SHIP_DIVE_DEEP && s->dive_dir == 0;
    s->dive += s->dive_dir;
    if (s->dive == 0) {
      s->dive_dir = 0;
      s->rewind = SHIP_SURFACED;
    } else if (s->dive == SHIP_DIVE_DEEP) {
      s->dive_dir = 0;
    }
    return step;
  }
  if (s->rewind >= 0 && ship_touches(hit, s->x, s->y, solid, ctx)) {
    s->rewind = SHIP_REWIND;
    s->bank = 0;
    int32_t limit = sc->line + sc->window;
    if (limit > sc->back_limit) sc->back_limit = limit;
  }
  return step;
}

/* The timers ($6BA8): the death counter and held fire. */
static void timers(ship_t *s) {
  if (s->state != SHIP_FLYING || s->dive) {
    if (s->death_count != SHIP_DEATH_COUNT) s->death_count++;
  } else if (s->death_count > 0) {
    s->death_count--;
  }
  if (s->dive_time && --s->dive_time == 0) { /* $6BE8: back up */
    s->dive_dir = -1;
    s->dive_sound = true;
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
  s->died = false;
  if (fire_pressed) s->fire_latch = true;
  int32_t step = update(s, sc, hit, solid, ctx);
  s->shot_request = s->fire_latch && s->state == SHIP_FLYING;
  s->fire_latch = false;
  timers(s);
  s->frames++;
  return step;
}
