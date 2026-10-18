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
#define ENEMIES_HOLES 16 /* level 1's worm holes */
#define ENEMIES_WORMS 5  /* worms at once */
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
  int16_t count; /* $4E2: the enemies at the frame's start, a chain once */
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
  /* Level 1's worms */
  uint16_t holes[ENEMIES_HOLES];      /* $D3C: a hole's frames to animate */
  uint32_t worm_rng[ENEMIES_WORMS];   /* $D5C: each slot's own generator */
  bool worm_slot[ENEMIES_WORMS];      /* $D9C: a worm claimed it this frame */
  bool shop_asked;                    /* $DA2: the mid shop once a level */
} enemies_t;

/* What the caller tells a frame. */
typedef struct {
  int32_t line;   /* the playfield's top world line ($CD0) */
  int32_t step;   /* the lines the world moved this frame ($CD8) */
  int32_t back_limit; /* the highest line the world may go back to ($CEA) */
  bool ship_flying;
  int32_t ship_x, ship_y;
  int16_t ship_box[4]; /* x0, y0, x1, y1 */
  bool shades;         /* Bitmap Shades on ($C90): a box of 128 around it */
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

/* The level's own frame code, after enemies_frame_start and before
 * anything moves; `step` the world's step as the ship last chose it
 * ($CDA), `ship_y` its place on the screen. Level 1 ($4FA06): once, when
 * the world line and the ship's y come below the mid shop's line, the
 * shop is asked for and the world put at the line after it (returned; -1
 * otherwise); in the worms' stretch the world may go back to its lowest
 * line (raises *back_limit), every free worm slot spawns a worm from a
 * hole, the holes being opened or closed animate. */
int32_t enemies_level_frame(enemies_t *e, int32_t line, int32_t step,
                            int32_t ship_y, int32_t *back_limit);

/* The ship's box against the enemies (the first it meets is destroyed);
 * with Bitmap Shades a box of 128 around the ship, which takes nothing. */
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

/* The first enemy whose box meets `box` takes `damage` ($38C4 and its hit
 * routine): true if there was one. */
bool enemies_hit_first(enemies_t *e, const int16_t box[4], int damage,
                       enemies_events_t *ev);

/* Every enemy whose box meets `box` takes `damage` ($3908; it stops once
 * the boss's payout is about). */
void enemies_hit_all(enemies_t *e, const int16_t box[4], int damage,
                     enemies_events_t *ev);

/* A random enemy whose box is on the screen ($2C68), or -1. */
int enemies_pick_target(enemies_t *e);

/* The box of target `id` while it is still an enemy on the screen (the
 * homing missiles' test at $4252), false otherwise. */
bool enemies_target_box(const enemies_t *e, int id, int16_t box[4]);

/* `n` explosions at random in the w x h box from (x, y), big or small
 * ($4A98), and its sounds (effect 15 on the three channels). */
void enemies_burst(enemies_t *e, int32_t x, int32_t y, int w, int h, int n,
                   bool big, enemies_events_t *ev);

/* One explosion at (x, y), without its sound (a homing missile without a
 * target, $437E). */
void enemies_explosion(enemies_t *e, int32_t x, int32_t y, bool big);

/* The direction of 8 (0 up, clockwise) towards (dx, dy), as the original's
 * $39A4 finds it. */
int enemies_direction(int dx, int dy);

/* The game's random generator ($27FE), shared by everything that moves. */
uint32_t enemies_random(enemies_t *e);

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

/* The level not won after all ($7DCE: sf $A07): in two players the other
 * plays it too. */
void enemies_level_unwon(enemies_t *e);

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
