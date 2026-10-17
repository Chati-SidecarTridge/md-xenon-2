/**
 * File: ship.h
 * Description: The Megablaster, as the original moves it: stepped once per
 *              original frame (scroll_pace decides how many a VBL), so its
 *              speeds are the original's per frame and every position is
 *              one the original could show. Plain C, no SDK.
 *
 * Per original frame, in the original's order:
 *   the fire button's press since the last frame latches a shot;
 *   the ship's update: the explosion while it dies; otherwise the rewind
 *     after touching the scenery, or the control (bank, glide, moves, the
 *     scroll's step), its position recorded, then the scenery test;
 *   the latched shot becomes this frame's shot request (`shot_request`,
 *     the original's $A6A: the weapons fire on it, weapons.h);
 *   the dive: Space starts it while dives are left ($7B06), the ship goes
 *     under the scenery for 136 frames (four shrinking frames down and up,
 *     $694C; no scenery test, no collisions), and surfaces with the rewind
 *     counter at -15 ($6978);
 *   the timers: held fire latches a shot every `fire_period` / `fire_rate`
 *     frames; the death counter, which also counts while diving.
 * The caller then applies the returned step to the scroll (scroll_advance).
 */

#ifndef SHIP_H
#define SHIP_H

#include <stdbool.h>
#include <stdint.h>

#include "scroll.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHIP_START_X 160
#define SHIP_START_Y 176
#define SHIP_MIN_X 14
#define SHIP_MAX_X 304
#define SHIP_BANK_MAX 6
#define SHIP_SHIELD_FULL 39
#define SHIP_LIVES 3
#define SHIP_CONTINUES 3
#define SHIP_HISTORY 17      /* positions kept for the scenery's rewind */
#define SHIP_REWIND 1        /* frames rewound at the first touch ($41E) */
#define SHIP_CRUSH (-17)     /* still touching after this: crushed */
#define SHIP_DIVE_FRAMES 136 /* $434 */
#define SHIP_DIVE_DEEP 4      /* $430 under the scenery */
#define SHIP_SURFACED (-15)   /* the rewind counter on surfacing */
#define SHIP_EXPLOSION_FRAMES 9
#define SHIP_DEATH_COUNT 16  /* frames before a death takes a life */
#define SHIP_STICK_LEFT 0x04
#define SHIP_STICK_RIGHT 0x08
#define SHIP_STICK_FIRE 0x80

typedef struct {
  int32_t line, x, y; /* world line, ship position */
} ship_place_t;

typedef enum {
  SHIP_FLYING,
  SHIP_EXPLODING, /* the explosion runs, the scroll stands */
  SHIP_DEAD       /* the death is complete: the level takes a life */
} ship_state_t;

/* Whether the playfield's pixel (x, y) shows solid scenery. */
typedef bool (*ship_solid_fn)(void *ctx, int32_t x, int32_t y);

/* The ship's shape against the scenery: 1 where it collides, row by row,
 * its top-left at (x - hot_x, y - hot_y). */
typedef struct {
  const uint8_t *mask;
  int32_t width, height, hot_x, hot_y;
} ship_hitmask_t;

typedef struct {
  ship_state_t state;
  int32_t x, y;      /* the sprite's hotspot */
  int32_t bank;      /* -6..6 ($CDE) */
  int32_t speed_ups; /* 0-2 ($C9E) */
  uint8_t stick;     /* the stick this frame, SCROLL_STICK_* / SHIP_STICK_* */
  /* Firing */
  bool fire_latch;     /* a shot asked for ($A01) */
  int32_t fire_count;  /* $C60 */
  int32_t fire_period; /* $C62 */
  int32_t fire_rate;   /* $C64 */
  bool shot_request;   /* this frame's shot ($A6A) */
  /* The scenery's rewind */
  ship_place_t history[SHIP_HISTORY]; /* [0] the newest */
  int32_t rewind;                     /* $42E */
  /* Damage and death */
  int32_t shield, lives, continues;
  int32_t explosion;   /* the explosion's frame, 0-8; 9 when it has ended */
  int32_t death_count; /* $E1C: up while dying or diving, down after */
  /* The dive */
  int32_t dives;       /* $CA0: dives left */
  int32_t dive;        /* $430: 0 flying, 1-3 shrinking, 4 under */
  int32_t dive_dir;    /* $432 */
  int32_t dive_time;   /* $434 */
  int32_t dive_frame;  /* the shrinking frame drawn this frame (1-4), 0 */
  bool dive_clipped;   /* drawn under the scenery ($1912) */
  /* Protection */
  int32_t invulnerable; /* $CC8: frames */
  bool armour;          /* $D99: damage halved */
  uint32_t frames;     /* original frames flown, for the flames' animation */
  /* What this frame did, for its sounds: the death begun, a dive's start or
   * end. */
  bool died, dive_sound;
} ship_t;

/* A new game: lives, shield, continues, the gun. */
void ship_game_start(ship_t *s);

/* The ship placed at (x, SHIP_START_Y) on world line `line` (a level's
 * start or a restart point): no bullets, no bank, its history full of that
 * place. Lives and shield are kept. */
void ship_place(ship_t *s, int32_t x, int32_t line);

/* One original frame (see the top of this file). `fire_pressed`: the fire
 * button went down since the last frame. `sc` is the scroll as drawn this
 * frame; the rewind may raise its back limit. Returns the scroll's step. */
int32_t ship_tick(ship_t *s, uint8_t stick, bool fire_pressed, scroll_t *sc,
                  const ship_hitmask_t *hit, ship_solid_fn solid, void *ctx);

/* Start the ship's death (the original's $6536): the explosion, then
 * SHIP_DEAD. */
void ship_die(ship_t *s);

/* Space: a dive begins if dives are left, or one under way ends ($7B06). */
void ship_dive_key(ship_t *s);

/* Damage to the shield (the original's $65A4): nothing while invulnerable,
 * half with the armour; at or below 0 the ship dies. */
void ship_hit(ship_t *s, int32_t damage);

/* Whether the ship's shape at (x, y) touches solid scenery. */
bool ship_touches(const ship_hitmask_t *hit, int32_t x, int32_t y,
                  ship_solid_fn solid, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHIP_H */
