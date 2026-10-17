/**
 * File: enemies.h
 * Description: Everything that moves in a level besides the ship, as the
 *              original moves it, an original frame at a time: the waves
 *              and scenery objects fired by the world line, the enemies on
 *              their paths, their fire, the hits, explosions, cash, capsules
 *              and tokens, level 1's wall objects (written into the map),
 *              wall crawlers and end boss with its turrets. The tables, paths and animation lists are read
 *              in the store from the level and the game image as the
 *              original holds them (x2level_raw, x2global_raw). Plain C, no
 *              SDK.
 *
 * Per original frame, in the original's order (the caller interleaves the
 * ship):
 *   enemies_frame_start   the wave groups' ids, every 4 frames;
 *   (the ship's update)   enemies_ship_body: the ship against the enemies;
 *   enemies_update        the enemies: paths, animation, fire;
 *   (the ship's bullets)  enemies_shoot: a bullet against the enemies;
 *   enemies_update_fx     explosions, cash, tokens, the enemies' shots;
 *   enemies_spawn         the records the world line has reached;
 *   enemies_draw          the enemies, then the rest, after the ship and its
 *                         bullets.
 * What a frame did for the ship and the score is added up in an
 * enemies_events_t.
 */

#ifndef ENEMIES_H
#define ENEMIES_H

#include <stdbool.h>
#include <stdint.h>

#include "x2global.h"
#include "x2level.h"
#include "x2sprite.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENEMIES_POOL 96
#define ENEMIES_GROUPS 8
#define ENEMIES_SOUNDS 8
#define ENEMIES_TOKENS 4
#define ENEMIES_FLASH 7 /* the colour a hit sprite is drawn in for a frame */
#define ENEMIES_BOSS_COLS 6
#define ENEMIES_BOSS_ROWS 7
#define ENEMIES_BOSS_X 112 /* the boss's block: 6 x 7 cells from here */

typedef struct {
  uint16_t type; /* the original's type; 0 a free slot */
  uint8_t role;  /* the update it runs */
  uint8_t hit;   /* what a hit does */
  uint8_t flags;
  int8_t next;   /* the list it is on */
  int8_t head;   /* a chained part: the chain's head */
  int8_t chain;  /* a chained part: the next part */
  int32_t x, y;  /* 16.16: screen x; screen y, or the world line (walls) */
  uint32_t sprite, anim; /* the sprite shown; the next animation entry */
  int16_t frames;        /* frames left on the sprite */
  uint32_t pc, start;    /* the path: the next opcode, the path's start */
  uint8_t dir;
  uint16_t frac;
  int32_t rate;
  int16_t acc, count, speed;
  int16_t hp;
  uint16_t worth, group;
  uint8_t fire, fire_acc;
  int16_t box[4];  /* x0, y0, x1, y1 on the screen, inclusive */
  int16_t v40, v42, v48; /* the role's counters (the original's +40...) */
  uint32_t tiles, wreck; /* a wall object's tile frames and wreck */
  int16_t cell;          /* a wall object's top-left map cell */
} enemy_t;

typedef struct {
  const x2level_header_t *level;
  const x2global_header_t *global;
  uint16_t *map; /* the level's map as it plays (20 words a row) */
  uint32_t map_rows;
  enemy_t pool[ENEMIES_POOL];
  int8_t free, enemies, fx; /* the lists' heads */
  int32_t wave_low, scenery_low; /* the lowest lines the tables saw */
  struct {
    uint16_t id, left;
  } groups[ENEMIES_GROUPS];
  uint16_t id_small, id_big, id_next;
  uint32_t frame;
  int32_t line; /* the world line of the frame being stepped */
  int16_t hp_small, hp_big;
  uint32_t rng[2];
  int16_t attacker[4]; /* the box of what hits ($460): a bullet, the ship */
  /* Level 1's end boss */
  int8_t boss;                /* its object, or -1 */
  bool boss_awake;            /* $DA1 */
  bool won;                   /* $A07: the boss destroyed */
  bool paid;                  /* $A6C: its payout taken or gone */
  uint8_t eye;                /* $D71 */
  int16_t payout;             /* $CA4: its cash still about */
  uint16_t boss_block[ENEMIES_BOSS_COLS * ENEMIES_BOSS_ROWS];
} enemies_t;

/* What the caller tells a frame. */
typedef struct {
  int32_t line;   /* the playfield's top world line ($CD0) */
  int32_t step;   /* the lines the world moved this frame ($CD8) */
  int32_t back_limit; /* the highest line the world may go back to ($CEA) */
  bool ship_flying;
  int32_t ship_x, ship_y;
  int16_t ship_box[4]; /* x0, y0, x1, y1 */
} enemies_frame_t;

/* What a frame did. */
typedef struct {
  int32_t score, cash, shield_damage;
  bool ship_flash;
  int ntokens;
  uint8_t tokens[ENEMIES_TOKENS]; /* tokens taken, by index */
  bool restart;                   /* a restart point passed */
  int32_t restart_x, restart_line;
  int32_t back_limit; /* the most the world may go back to now, or -1 */
  int nsounds;
  struct {
    uint8_t effect, channel; /* the original's: 2 ($414), 1 ($418), 0 */
  } sounds[ENEMIES_SOUNDS];
} enemies_events_t;

/* The level and the shared image in the store, the map in RAM; `loop` the
 * times the game went round (1 the first), `seed` the random generator's.
 * Empty lists, the tables from the level's start. */
void enemies_init(enemies_t *e, const x2level_header_t *level,
                  const x2global_header_t *global, uint16_t *map,
                  uint32_t map_rows, int loop, uint32_t seed);

/* After a death, play resumes at world line `line`: empty lists; the
 * waves from just below it fire again, the scenery objects on the screen
 * at once. */
void enemies_restart(enemies_t *e, int32_t line);

void enemies_frame_start(enemies_t *e);

/* The ship's box against the enemies (the first it meets is destroyed). */
void enemies_ship_body(enemies_t *e, const enemies_frame_t *f,
                       enemies_events_t *ev);

void enemies_update(enemies_t *e, const enemies_frame_t *f,
                    enemies_events_t *ev);

/* A player's bullet at (x, y) of `damage` against the enemies: true if it
 * hit one (the bullet is spent). */
bool enemies_shoot(enemies_t *e, int32_t x, int32_t y, int damage,
                   enemies_events_t *ev);

void enemies_update_fx(enemies_t *e, const enemies_frame_t *f,
                       enemies_events_t *ev);

void enemies_spawn(enemies_t *e, const enemies_frame_t *f,
                   enemies_events_t *ev);

/* The bomb: every enemy destroyed as by a hit of 127. */
void enemies_bomb(enemies_t *e, enemies_events_t *ev);

/* Draw every visible object, in the original's order, with the world line
 * `line` at the screen's top, through `draw`: the sprite, its pixels, its
 * hotspot's position, and whether it is drawn in ENEMIES_FLASH this frame;
 * the boss's tile block through `block`. */
typedef void (*enemies_draw_fn)(void *ctx, const x2_sprite_t *s,
                                const uint8_t *pixels, int32_t x, int32_t y,
                                bool flash);
/* A block of map words (the boss), its top-left corner at (x, y). */
typedef void (*enemies_block_fn)(void *ctx, const uint16_t *words, int cols,
                                 int rows, int32_t x, int32_t y, bool flash);
void enemies_draw(enemies_t *e, int32_t line, enemies_draw_fn draw,
                  enemies_block_fn block, void *ctx);

/* The boss destroyed ($A07), and its payout over ($A6C): the level is won. */
bool enemies_level_won(const enemies_t *e);

/* While the boss's payout is about, the ship takes no damage ($65A4). */
bool enemies_ship_safe(const enemies_t *e);

/* The collision box of the sprite the original keeps at `source` at
 * (x, y), as the original's $3D58 makes it: false if no such sprite. */
bool enemies_box(const enemies_t *e, uint32_t source, int32_t x, int32_t y,
                 int16_t box[4]);

/* Live objects, for the record. */
int enemies_count(const enemies_t *e);

#ifdef __cplusplus
}
#endif

#endif /* ENEMIES_H */
