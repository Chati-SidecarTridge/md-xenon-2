/**
 * File: weapons.h
 * Description: The player's weapons, as the original runs them an original
 *              frame at a time: the seven slots, every weapon a token or
 *              the shop gives (forward, double and side shots, the rear
 *              shot, the flamer, the bomb, the homing missiles, the drone,
 *              the electro ball, the mines, the cannon, the laser and the
 *              missile launcher), their projectiles and the drone's dots,
 *              the power-ups, and what a restart point keeps. The game
 *              image's tables and sprites are read in the store's shared
 *              image (x2global_raw). Plain C, no SDK.
 *
 * Per original frame, after the enemies' update (the original's weapons
 * list $AEA comes after the enemies' $B4C): weapons_update (the weapons
 * fire and follow the ship), then weapons_update_shots (the projectiles
 * move and hit, as the effects list $BAE; the dots, $6A1C). Drawn after the
 * ship: weapons_draw (the weapons), then after the enemies
 * weapons_draw_shots (the projectiles and dots).
 */

#ifndef WEAPONS_H
#define WEAPONS_H

#include <stdbool.h>
#include <stdint.h>

#include "enemies.h"
#include "x2global.h"
#include "x2sprite.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEAPONS_POOL 64
#define WEAPONS_INVULNERABLE 170 /* token 16's frames ($52CA) */
#define WEAPONS_SLOTS 7
#define WEAPONS_DOTS 80
#define WEAPONS_DOT_COLOUR 5   /* the dots' ends */
#define WEAPONS_DOT_CENTRE 7   /* their middle */

/* The tokens (the original's $5040 table, by token number). */
enum {
  TOKEN_SPEEDUP, TOKEN_AUTOFIRE, TOKEN_HEALTH1, TOKEN_HEALTH2, TOKEN_REAR,
  TOKEN_SIDE, TOKEN_POWERUP, TOKEN_CANNON, TOKEN_DRONE, TOKEN_LASER,
  TOKEN_ELECTRO, TOKEN_MINE, TOKEN_LAUNCHER, TOKEN_HOMING, TOKEN_FLAMER,
  TOKEN_BOMB, TOKEN_INVULNERABLE, TOKEN_DIVE, TOKEN_SMART_BOMB, TOKEN_COUNT
};

typedef struct {
  uint16_t type;
  uint8_t role;
  uint8_t flags;
  int8_t next;
  int8_t slot;     /* a weapon's slot, -1 for a projectile */
  int8_t owner;    /* a projectile's weapon (the laser's beam, $86) */
  int16_t target;  /* a homing missile's enemy, -1 none */
  int32_t x, y;    /* 16.16 */
  int32_t dx, dy;  /* 16.16 a frame (bullets, flames) */
  uint32_t sprite, anim; /* a masked sprite and its animation */
  int16_t frames;
  int8_t bitmap;   /* a bullet: its x2g_id_t, -1 for a masked sprite */
  int16_t power, max; /* +66, +50 */
  int16_t v40, v94;   /* the role's counter, the weapon's timer */
  int16_t ox, oy;     /* a pod's place by the ship (+62, +64) */
  uint8_t dir;        /* a homing missile's direction, 0-7 */
  int16_t box[4];
} weapon_t;

typedef struct {
  int16_t x, y, dx, dy; /* x < 0: free */
} weapon_dot_t;

typedef struct {
  uint16_t type;
  int8_t obj;
} weapon_slot_t;

/* What a restart point or a level's end keeps ($CAA): the slots' types and
 * powers. */
typedef struct {
  uint16_t type[WEAPONS_SLOTS];
  int16_t power[WEAPONS_SLOTS];
} weapons_save_t;

typedef struct {
  const x2global_header_t *global;
  enemies_t *enemies;
  weapon_t pool[WEAPONS_POOL];
  int8_t free, weapons, shots; /* the lists' heads */
  weapon_slot_t slots[WEAPONS_SLOTS];       /* $C66 */
  weapons_save_t saved;                      /* $CAA */
  weapon_dot_t dots[WEAPONS_DOTS];           /* $3CF5C */
  int16_t mines;                             /* $43A */
  int16_t drop_x, drop_y;                    /* $4E8, $4EA */
  int16_t fire_period;                       /* $C62 */
  bool flaming;                              /* the flamer's sound runs */
  bool flame_stop;                           /* ask the driver to stop it */
  int16_t invulnerable;                      /* $CC8 */
  int16_t ship_x, ship_y; /* where a new weapon is made ($3923C, $39240) */
} weapons_t;

/* What the frame tells the weapons. */
typedef struct {
  bool shot;          /* the latched shot request ($A6A) */
  bool held;          /* the fire button held ($A00 bit 7) */
  uint8_t stick;      /* up 1, down 2, left 4, right 8 ($A00 bits 0-3) */
  int32_t x, y;       /* the ship */
  int32_t before_x, before_y; /* before this frame's move ($4E4) */
  int32_t trail_x, trail_y;   /* four frames ago ($46C) */
  int16_t ship_centre_x, ship_centre_y;
  int16_t scatter;    /* $E1C: 0 normal, 1-15 flung out, 16 hidden */
  bool diving;        /* $430: no weapon fires */
  bool dying;         /* $D9A: the projectiles vanish */
  bool flame_sound_done; /* the driver's flag $2D0C2 clear: effect ended */
} weapons_frame_t;

/* No weapons yet, the slots empty. */
void weapons_init(weapons_t *w, const x2global_header_t *global,
                  enemies_t *enemies);

/* A new game: the forward shot, saved ($7334). */
void weapons_game_start(weapons_t *w);

/* A weapon token taken (4, 5, 7-15; 6 the power-up; 16 invulnerability);
 * the others are the caller's. */
void weapons_take(weapons_t *w, int token);

/* A weapon made by its slot's type, as the shop buys it and a restart
 * remakes it ($508C): 148 forward, 192 double, 176 flamer, 152 side, 68
 * rear, 40 bomb, 72 homing, 44 drone, 76 electro, 56 / 60 mines, 52
 * cannon, 32 laser, 144 missile launcher. */
void weapons_make(weapons_t *w, uint16_t type);

/* Frames of invulnerability left ($CC8). */
int16_t weapons_invulnerable(const weapons_t *w);

/* A restart point passed ($3DA2): the slots and powers saved. */
void weapons_save(weapons_t *w);

/* The ship's death begins ($657E): the newest weapon is lost. */
void weapons_death(weapons_t *w);

/* After a death or at a level's start ($750C, $755E, $7E58): every weapon
 * and projectile freed, the saved ones made again with their powers. */
void weapons_restore(weapons_t *w);

/* The fire period the weapons set ($C62). */
int16_t weapons_fire_period(const weapons_t *w);

void weapons_update(weapons_t *w, const weapons_frame_t *f,
                    enemies_events_t *ev);
void weapons_update_shots(weapons_t *w, const weapons_frame_t *f,
                          enemies_events_t *ev);

/* Draw through the enemies' callback (masked sprites) and these: a bullet
 * bitmap; the laser's beam column; a dot. */
typedef void (*weapons_bitmap_fn)(void *ctx, int id, int32_t x, int32_t y);
typedef void (*weapons_beam_fn)(void *ctx, const uint8_t *pattern, int32_t x,
                                int32_t y_bottom, int32_t length);
typedef void (*weapons_dot_fn)(void *ctx, int32_t x, int32_t y);
void weapons_draw(weapons_t *w, enemies_draw_fn draw, void *ctx);
void weapons_draw_shots(weapons_t *w, enemies_draw_fn draw,
                        weapons_bitmap_fn bitmap, weapons_beam_fn beam,
                        weapons_dot_fn dot, void *ctx);

/* The weapon in a slot (its type, 0 none) and its power, for the record. */
uint16_t weapons_slot_type(const weapons_t *w, int slot);
int weapons_slot_power(const weapons_t *w, int slot);

#ifdef __cplusplus
}
#endif

#endif /* WEAPONS_H */
