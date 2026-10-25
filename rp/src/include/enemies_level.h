/**
 * File: enemies_level.h
 * Description: What a level's own code shares with the game's object code
 *              (enemies.c): the objects' roles, hits and flags, the game's
 *              routines it calls, and the table of hooks a level fills in,
 *              one for each entry of the level's header the game calls
 *              ($4F00C, $4F018, $4F020, $4F024) and for the objects its own
 *              code makes. Internal to the enemies; plain C, no SDK.
 */

#ifndef ENEMIES_LEVEL_H
#define ENEMIES_LEVEL_H

#include "enemies.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Roles: the update an object runs. The game's first, then the level's own
 * from R_LEVEL (each level numbers its own). */
enum { R_PATH, R_FOLLOW, R_EXPLOSION, R_DRIFT, R_SHOT, R_LEVEL };
/* Hits: the game's, then the level's own from H_LEVEL. */
enum { H_NONE, H_SINGLE, H_CHAIN, H_CAPSULE, H_WALL, H_SHIELD, H_LEVEL };
/* Flags. */
#define F_DRAW 0x01
#define F_FLASH 0x02
#define F_BIG 0x04
#define F_CHAINED 0x08
#define F_DEAD 0x10
#define F_TO_FX 0x20 /* off to the other list after its update */
#define F_KEEP 0x40  /* outlives a restart (the original's +96) */
#define F_BEHIND 0x80 /* drawn behind the scenery ($1912) */

#define T_DEAD 4
#define T_EXPLOSION 12
#define T_SHOT 20
#define T_CASH_BIG 24
#define T_CASH 96
#define T_CAPSULE 100
#define T_BOSS_A 80 /* never destroyed by the ship's body or the bomb */
#define T_BOSS_B 84

#define BASE X2LEVEL_BASE
#define SINE 0x9C80u /* the game's sine table */
#define CASH_ANIM 0x2E9Eu /* the game's animation lists */
#define CASH_BIG_ANIM 0x2E3Eu
#define BOOM_ANIM 0x2EFEu
#define BOOM_BIG_ANIM 0x2F2Au

/* The original's memory, read only: the level from $4F000, the game's
 * windows below it; 0 where nothing is. */
const uint8_t *en_raw(const enemies_t *e, uint32_t addr, uint32_t len);
uint16_t en_rw(const enemies_t *e, uint32_t addr);
int16_t en_rs(const enemies_t *e, uint32_t addr);
uint32_t en_rl(const enemies_t *e, uint32_t addr);
uint8_t en_rb(const enemies_t *e, uint32_t addr);
const x2_sprite_t *en_sprite(const enemies_t *e, uint32_t source);

uint32_t en_rnd(enemies_t *e);         /* $27FE */
int en_rnd_mod(enemies_t *e, int n);   /* $28A4: below n */
int en_dir_to(int dx, int dy);         /* $39A4: of 8, 0 up, clockwise */

/* The lists: an object taken from the free list (-1 if none), put at a
 * list's head or end, marked dead, freed at once ($107C). */
int8_t en_alloc(enemies_t *e);
void en_push(int8_t *head, enemies_t *e, int8_t i);
void en_append(int8_t *head, enemies_t *e, int8_t i);
void en_kill(enemy_t *a);
void en_free_now(enemies_t *e, int8_t *head, int8_t i);

void en_sound(enemies_events_t *ev, int effect, int channel);
void en_anim_start(enemies_t *e, enemy_t *a, uint32_t list); /* $34C2 */
bool en_animate(enemies_t *e, enemy_t *a); /* false: its animation ended */
void en_remove_chain(enemies_t *e, enemy_t *a);
void en_box(const enemies_t *e, enemy_t *a); /* $3D58 */
void en_box_off(enemy_t *a);
bool en_overlap(const int16_t *a, const int16_t *b);
void en_path_step(enemies_t *e, enemy_t *a); /* $9ACA */

/* Made on the way: an explosion at `a`'s centre ($3A5E), an enemy's shot
 * ($39FE: direction 0-15), an enemy destroyed ($6250). */
void en_explode(enemies_t *e, const enemy_t *a, bool big, enemies_events_t *ev);
int8_t en_shot(enemies_t *e, uint32_t source, int32_t x, int32_t y, int dir,
               int speed);
void en_destroyed(enemies_t *e, enemy_t *a, enemies_events_t *ev);
/* The game's updates a level's objects run too: a path's ($9A40: the
 * animation, the step, the box, the fire), the fire alone ($9A48), an
 * enemy's shot ($4180). */
void en_update_path(enemies_t *e, enemy_t *a, const enemies_frame_t *f);
void en_fire(enemies_t *e, enemy_t *a, const enemies_frame_t *f);
void en_update_shot(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                    enemies_events_t *ev);
/* An object hit for `damage` by what e->attacker holds ($6156: through
 * its own hit), a boss's payout ($2996: `pairs` 50s and 100s). */
void en_hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev);
void en_payout(enemies_t *e, int pairs);
/* A strip of pixels through the game's drawing (enemies_set_strip). */
void en_strip(const enemies_t *e, void *ctx, int32_t x, int32_t y,
              int32_t height, const uint8_t *colors, int width);
uint16_t en_group_new(enemies_t *e); /* a group of one: its id */

/* A level's own code. Every hook may be NULL. */
typedef struct {
  /* +$20: the level begun (its objects made, its variables set). */
  void (*start)(enemies_t *e);
  /* +$18: a scenery record of kind 1 or more come into view. */
  void (*scenery)(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                  int kind, enemies_events_t *ev);
  /* +$24: every frame (enemies_level_frame). */
  int32_t (*frame)(enemies_t *e, int32_t line, int32_t step, int32_t ship_y,
                   int32_t *back_limit);
  /* An object of the level's own role (R_LEVEL on) on the enemy list or
   * the effects' list; `i` its index. */
  void (*update)(enemies_t *e, int8_t i, const enemies_frame_t *f,
                 enemies_events_t *ev);
  void (*update_fx)(enemies_t *e, int8_t i, const enemies_frame_t *f,
                    enemies_events_t *ev);
  void (*update_cells)(enemies_t *e, int8_t i, const enemies_frame_t *f,
                       enemies_events_t *ev);
  /* An object of the level's own hit (H_LEVEL on) hit. */
  void (*hit)(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev);
  /* An object of the level's own role drawn its own way: true if it was. */
  bool (*draw)(enemies_t *e, enemy_t *a, int32_t line, bool flash,
               enemies_draw_fn draw, enemies_block_fn block, void *ctx);
  /* +$0C: nothing on the enemy list hit, the box (on the screen, the world
   * line `line` at its top) tried on the level's own breakable cells; true
   * if one took the hit. */
  bool (*cells_hit)(enemies_t *e, const int16_t box[4], int32_t line,
                    enemies_events_t *ev);
} enemies_level_t;

extern const enemies_level_t enemies_level1;
extern const enemies_level_t enemies_level2;
extern const enemies_level_t enemies_level3;
extern const enemies_level_t enemies_level4;
extern const enemies_level_t enemies_level5;

#ifdef __cplusplus
}
#endif

#endif /* ENEMIES_LEVEL_H */
