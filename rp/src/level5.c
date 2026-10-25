/**
 * File: level5.c
 * Description: Level 5's own code (enemies_level.h), ported from the
 *              original's at $4F03C-$51830 and run on the level's tables in
 *              the store.
 *
 * - Its start ($517AA): the enemies' hit points twelve and fourteen times
 *   the round; the end boss's and the mid boss's numbers taken from the
 *   level's block ($4F071, $4F076).
 * - Its scenery ($4F3AA), kinds 1-9: three-part guns in the map (1,
 *   $50D10), wall guns (2, $5156C), turrets turning to the ship (3, $5126A;
 *   9 the same, once destroyed for good, $51250), eight-way turrets (4,
 *   $51000), snakes of ten on a path firing all round (7, $4F3E8), pods
 *   going up and down and launching homing creatures (8, $4F698); the mid
 *   boss (5, $50626) and the end boss (6, $4FDFA).
 * - The mid boss: a block of tiles moving up and down with its tracks,
 *   four turrets and an eye that takes the hits; lasers down the screen
 *   and homing creatures; destroyed, a payout brings the mid shop.
 * - The end boss: built into the map at the level's top (the world jumps
 *   back to line 416, the tables end, a restart point), its parts on it:
 *   turrets, laser launchers, bars that stop shots, jaws sending homing
 *   creatures; eighteen of them to destroy before its core can be hurt;
 *   with the world held at the top the backdrop and stars go on.
 */

#include <stddef.h>
#include <string.h>

#include "enemies_level.h"

/* Its roles and hits. */
enum { R_SNAKE = R_LEVEL, R_POD, R_HOMER, R_GUN3_END, R_GUN3_MID, R_GUN,
       R_TURRET, R_GUN8, R_MID_MAIN, R_AIMER, R_MID_EYE, R_TREAD, R_LASER,
       R_WRECK, R_END_MAIN, R_END_BAR, R_END_PART, R_END_CORE, R_LAUNCHER,
       R_END_HOMER };
enum { H_GUN3_LEFT = H_LEVEL, H_GUN3_RIGHT, H_TURRET, H_GUN8, H_MID_AIMER,
       H_MID_EYE, H_END_PART, H_END_CORE };

/* Its own variables (e->vars). */
enum { V_MID, V_END, V_PARTS, V_JUMP, V_STEP, V_ONCE0, V_ONCE1 };

/* The level's block of numbers ($4F03C). */
#define SNAKE_HP 0x4F042u
#define SNAKE_GAP 0x4F044u
#define SNAKE_SPEED 0x4F046u
#define POD_HP 0x4F048u
#define POD_RATE 0x4F04Bu
#define HOMER_HP 0x4F04Cu
#define HOMER_LIFE 0x4F04Eu
#define END_RATE 0x4F051u
#define END_HOMER_HP 0x4F052u
#define LAUNCHER_RATE 0x4F055u
#define AIMER_RATE 0x4F057u
#define AIMER_SPEED 0x4F058u
#define MID_RATE 0x4F05Bu
#define LASER_STEP 0x4F05Cu
#define GUN3_HP 0x4F05Eu
#define GUN8_HP 0x4F060u
#define TURRET_HP 0x4F062u
#define TURRET_RATE 0x4F065u
#define TURRET_SPEED 0x4F066u
#define GUN_HP 0x4F068u
#define GUN_RATE 0x4F06Bu
#define GUN_SPEED 0x4F06Cu
#define AIMER_SHOT 0x4F06Eu
#define END_HP 0x4F071u   /* the end boss's ($4FB64 at level start) */
#define GUN8_RATE 0x4F073u
#define GUN8_SPEED 0x4F074u
#define MID_EYE_HP 0x4F076u /* the mid boss's eye's ($505C4 at level start) */
#define SNAKE_RATE 0x4F079u
#define SNAKE_SHOT_SPEED 0x4F07Au
#define SNAKE_SHOT 0x4F07Cu
#define MID_RATE2 0x4F07Fu

/* The shots' sprites by index. The table at $719BC (pointers to these)
 * shares the planar tiles' last unit, which the image leaves out of the
 * raw copy; the level's two entries point here in order. */
#define SHOTS 0x719CCu
#define LEVEL_MAP 0x5D21Au    /* the level's map, as its code names it */
#define SNAKE_PATHS 0x4F3DCu  /* 3 paths */
#define SNAKE_ANIMS 0x4F4BAu  /* 8 animations, by direction */
#define SNAKE_SPRITE 0x563BEu
#define POD_DOWN 0x4F716u     /* the pods' animations, each side */
#define POD_DOWN_LEFT 0x4F73Au
#define POD_UP 0x4F75Eu
#define POD_UP_LEFT 0x4F782u
#define HOMER_ANIMS 0x4F9DEu  /* 8 animations, by direction */
#define HOMER_STEPS 0x4F9FEu  /* 8 dx, then 8 dy */
#define GUN3_PARTS 0x50CECu   /* 3 x (type, update, hit, cells on) */
#define GUN3_MIDDLE 0x50DF0u  /* its middle's cells, by step */
#define GUN3_LEFT_CELLS 0x50E54u
#define GUN3_RIGHT_CELLS 0x50E5Cu
#define GUN_RIGHT 0x516E4u    /* a wall gun's cell, by step */
#define GUN_LEFT 0x516D4u
#define GUN_WRECK_RIGHT 0x516F4u
#define GUN_WRECK_LEFT 0x516F6u
#define GUN_SHOT_RIGHT 0x516F8u
#define GUN_SHOT_LEFT 0x51710u
#define TURRET_CELLS 0x512DEu /* its 2 x 2 cells, by direction */
#define TURRET_AIM 0x5146Au   /* 8 x (dx, dy): where its shot leaves */
#define TURRET_SHOTS 0x5148Au /* its shot's sprites */
#define GUN8_CELLS 0x51062u   /* 2 x 2 cells, by step */
#define GUN8_SHOT 0x51178u
#define MID_PARTS 0x50536u    /* 10 x (type, update, draw, hit, sprite, dx, dy, hp) */
#define MID_BLOCK 0x50AF0u    /* 6 x 6 map words */
#define MID_FRAMES 0x50A84u   /* by step: rows 1-3, columns 2-3 */
#define MID_EYES 0x5070Au     /* the eye's sprites */
#define TREADS 0x507BAu       /* the tracks' sprites, each kind */
#define TREADS2 0x507CAu
#define AIMER_SPRITES 0x50850u
#define WRECK 0x5816Au
#define LASER_TOP 0x594D2u
#define LASER_BOTTOM 0x59514u
#define END_PARTS 0x4FB4Eu    /* 22 x (type, update, draw, hit, animation, dx, dy, hp, size) */
#define END_BLOCK 0x5183Au    /* 15 x 18 map words */
#define END_HOMER 0x5017Eu
#define END_HOMER_ANIMS 0x5023Eu
#define END_CORE 0x50476u     /* the core's sprites as it weakens */
#define JAWS 0x50142u
#define JAWS2 0x5011Au
#define LAUNCH_SPRITE 0x5B760u

#define T_HOMER 0xE4
#define T_END_HOMER 0xEC
#define T_SNAKE 0xDC
#define T_POD 0x100
#define T_GUN3_LEFT 0x128
#define T_GUN_RIGHT 0x108
#define T_GUN_LEFT 0x10C
#define T_TURRET 0xF4
#define T_GUN8 0xF8
#define T_AIMER 0x114
#define T_TREAD 0x118
#define T_LASER 0x110

/* The world lines the pods turn at, the mid boss's moves and lasers. */
#define POD_LOW 3632
#define POD_HIGH 3872
#define MID_X 112
#define MID_TOP 2256
#define MID_BOTTOM 2320
#define MID_BACK 2336
#define END_LINE 416
#define LASER_MAX 48

static const uint8_t k_beam[12] = {15, 14, 7, 7, 7, 7, 7, 7, 7, 7, 14, 15};

/* ---- shared ---- */

static void map_set(enemies_t *e, int32_t cell, uint16_t word) {
  if (cell >= 0 && (uint32_t)cell < e->map_rows * 20u) e->map[cell] = word;
}

static uint16_t map_get(const enemies_t *e, int32_t cell) {
  return cell >= 0 && (uint32_t)cell < e->map_rows * 20u ? e->map[cell] : 0;
}

/* Two words at `cell`, two more on the row below, from `words`. */
static void map_2x2(enemies_t *e, int32_t cell, uint32_t words) {
  map_set(e, cell, en_rw(e, words));
  map_set(e, cell + 1, en_rw(e, words + 2));
  map_set(e, cell + 20, en_rw(e, words + 4));
  map_set(e, cell + 21, en_rw(e, words + 6));
}

static bool gone(enemy_t *a, const enemies_frame_t *f) {
  if ((a->y >> 16) <= f->back_limit + 208) return false;
  en_kill(a);
  return true;
}

static void box_at(enemy_t *a, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  a->box[0] = (int16_t)x0;
  a->box[1] = (int16_t)y0;
  a->box[2] = (int16_t)x1;
  a->box[3] = (int16_t)y1;
}

/* An accumulator's carry ($9A48's way): a new one at random on it. */
static bool carry(enemies_t *e, uint8_t *acc, uint32_t rate) {
  unsigned sum = (unsigned)*acc + en_rb(e, rate);
  *acc = (uint8_t)sum;
  if (sum < 256) return false;
  *acc = (uint8_t)(en_rnd(e) & 63u);
  return true;
}

/* A scenery object as $3CCC makes it: on the enemy list, at the record's
 * place, the world line its y, its map cell. */
static enemy_t *in_map(enemies_t *e, uint32_t rec, uint16_t type, uint8_t role,
                       uint8_t hit) {
  int8_t i = en_alloc(e);
  if (i < 0) return NULL;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  int32_t x = en_rs(e, rec + 8) - 8, y = en_rs(e, rec + 10) - 8;
  a->type = type;
  a->role = role;
  a->hit = hit;
  a->flags = 0; /* drawn in the map */
  a->x = x * 65536;
  a->y = y * 65536;
  a->cell = (int16_t)((y >> 4) * 20 + (x >> 4));
  en_box_off(a);
  return a;
}

/* An object at the enemy list's end ($B48). */
static enemy_t *at_end(enemies_t *e) {
  int8_t i = en_alloc(e);
  if (i < 0) return NULL;
  en_append(&e->enemies, e, i);
  return &e->pool[i];
}

/* A boss's part where its parent is, its offset on ($506C0). */
static void follow(enemies_t *e, enemy_t *a) {
  if (a->head < 0) return;
  const enemy_t *p = &e->pool[a->head];
  a->x = p->x + (int32_t)a->tiles * 65536;
  a->y = p->y + (int32_t)a->wreck * 65536;
}

/* Dying as the original's parts do: an explosion at the sprite's middle. */
static void burst(enemies_t *e, enemy_t *a, bool big, enemies_events_t *ev) {
  en_explode(e, a, big, ev);
  en_kill(a);
}

/* An explosion at (x, y). */
static void boom(enemies_t *e, int32_t x, int32_t y, bool big,
                 enemies_events_t *ev) {
  enemy_t c;
  memset(&c, 0, sizeof(c));
  c.x = x * 65536;
  c.y = y * 65536;
  en_explode(e, &c, big, ev);
}

/* The ship hit for `damage`, or the object's end if it cannot be. */
static void into_ship(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                      int damage, bool explode, enemies_events_t *ev) {
  if (f->ship_invulnerable) {
    if (explode) {
      burst(e, a, false, ev);
      return;
    }
  } else {
    ev->shield_damage += damage;
    ev->ship_flash = true;
  }
  en_kill(a);
}

static bool point_in_ship(const enemies_frame_t *f, int32_t x, int32_t y) {
  return f->ship_flying && x >= f->ship_box[0] && x <= f->ship_box[2] &&
         y >= f->ship_box[1] && y <= f->ship_box[3];
}

/* Turned a step toward the ship on 8 directions ($4FA1E, $5025E). */
static int turn(int dir, int to, bool odd_rule, int32_t x) {
  int d = to - dir;
  if (d == 0) return dir;
  if (d < 0) d += 8;
  dir++;
  if (d >= 4 && (!odd_rule || d != 4 || !(x & 1))) dir -= 2;
  return dir & 7;
}

/* ---- the snakes (kind 7: $4F3E8, update $4F4DA) ---- */

static void spawn_snake(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  uint32_t path = en_rl(e, SNAKE_PATHS + 4u * en_rw(e, rec + 6));
  int16_t delay = 0;
  for (int k = 9; k >= 0; k--) {
    enemy_t *a = at_end(e);
    if (!a) return;
    a->type = T_SNAKE;
    a->role = R_SNAKE;
    a->hit = H_SINGLE;
    a->sprite = SNAKE_SPRITE;
    a->x = en_rs(e, path + 2) * 65536;
    a->y = (en_rs(e, path + 4) - f->line) * 65536;
    a->hp = en_rs(e, SNAKE_HP);
    a->start = path;
    a->pc = path + 6;
    a->count = delay;
    delay = (int16_t)(delay - en_rs(e, SNAKE_GAP));
    a->worth = 200;
    a->speed = en_rs(e, SNAKE_SPEED);
    a->fire_acc = (uint8_t)(k * 25);
    en_box_off(a);
  }
}

static void update_snake(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  if ((a->y >> 16) >= 400) {
    en_kill(a);
    return;
  }
  int was = ((a->dir + 16) >> 3) & 28;
  en_path_step(e, a);
  if (a->flags & F_DEAD) return;
  en_box(e, a);
  int now = ((a->dir + 16) >> 3) & 28;
  if (now != was) en_anim_start(e, a, en_rl(e, SNAKE_ANIMS + (uint32_t)now));
  unsigned sum = (unsigned)a->fire_acc + en_rb(e, SNAKE_RATE);
  a->fire_acc = (uint8_t)sum;
  if (sum >= 256) { /* all round */
    a->fire_acc = 0;
    uint32_t sprite = en_rl(e, SHOTS + 4u * en_rw(e, SNAKE_SHOT));
    for (int d = 7; d >= 0; d--) {
      en_shot(e, sprite, a->x >> 16, a->y >> 16, d, en_rw(e, SNAKE_SHOT_SPEED));
    }
  }
  en_box(e, a);
}

/* ---- the pods (kind 8: $4F698, update $4F7A6) and their creatures ---- */

static void spawn_homer(enemies_t *e, int32_t x, int32_t y, int dir) { /* $4F882 */
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  a->type = T_HOMER;
  a->role = R_HOMER;
  a->hit = H_SINGLE;
  a->x = x * 65536;
  a->y = y * 65536;
  a->v42 = (int16_t)dir;
  en_anim_start(e, a, en_rl(e, HOMER_ANIMS + 4u * (uint32_t)dir));
  a->hp = en_rs(e, HOMER_HP);
  a->worth = 100;
  a->v40 = (int16_t)(en_rnd(e) & 31u);
  en_box_off(a);
}

static void spawn_pod(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  bool left = en_rw(e, rec + 12) != 0;
  enemy_t *a = in_map(e, rec, left ? T_POD + 4 : T_POD, R_POD, H_SINGLE);
  if (!a) return;
  a->flags = F_DRAW | F_BIG;
  a->y -= f->line * 65536; /* on the screen */
  a->hp = en_rs(e, POD_HP);
  a->worth = 250;
  if (left) a->x += 16 * 65536;
  en_anim_start(e, a, left ? POD_DOWN_LEFT : POD_DOWN);
  a->v42 = 20;
}

static void update_pod(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  bool left = a->type != T_POD;
  a->y += f->step * 65536;
  if ((a->y >> 16) >= 400) {
    en_kill(a);
    return;
  }
  en_animate(e, a);
  int32_t y = a->y >> 16;
  if (a->v42 >= 0) {
    y++;
    if (f->line + y >= POD_HIGH) {
      a->v42 = -1;
      en_anim_start(e, a, left ? POD_UP_LEFT : POD_UP);
    }
  } else {
    y--;
    if (f->line + y <= POD_LOW) {
      a->v42 = 1;
      en_anim_start(e, a, left ? POD_DOWN_LEFT : POD_DOWN);
    }
  }
  a->y = y * 65536;
  if (carry(e, &a->fire_acc, POD_RATE)) {
    int32_t x = a->x >> 16;
    spawn_homer(e, left ? x - 32 : x + 32, y, left ? 6 : 2);
  }
  en_box(e, a);
}

/* $4FA1E: turning to the ship every 8 frames; after a while, an explosion.
 * Its test of the ship takes its step for its place, as the original's. */
static void update_homer(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                         enemies_events_t *ev) {
  en_animate(e, a);
  if (++a->v40 >= en_rs(e, HOMER_LIFE)) {
    burst(e, a, false, ev);
    return;
  }
  int32_t x = a->x >> 16, y = a->y >> 16;
  if ((a->v40 & 7) == 0) {
    int d = turn(a->v42, en_dir_to(f->ship_x - x, f->ship_y - y), true, x);
    if (d != a->v42) {
      a->v42 = (int16_t)d;
      en_anim_start(e, a, en_rl(e, HOMER_ANIMS + 4u * (uint32_t)d));
    }
  }
  int32_t dx = en_rs(e, HOMER_STEPS + 2u * (uint32_t)a->v42);
  int32_t dy = en_rs(e, HOMER_STEPS + 16u + 2u * (uint32_t)a->v42) + f->step;
  a->x = (x + dx) * 65536;
  a->y = (y + dy) * 65536;
  en_box(e, a);
  if (point_in_ship(f, dx, dy)) into_ship(e, a, f, 4, true, ev);
}

/* ---- the three-part guns (kind 1: $50D10) ---- */

static void spawn_gun3(enemies_t *e, uint32_t rec) {
  int32_t x = en_rs(e, rec + 8) - 8, y = en_rs(e, rec + 10) - 8;
  int32_t cell = (y >> 4) * 20 + (x >> 4);
  map_set(e, cell, 0x8BDD);
  map_set(e, cell + 1, 0);
  map_set(e, cell + 2, 0);
  map_set(e, cell + 3, 0x8BE9);
  int8_t first = -1;
  uint32_t part = GUN3_PARTS;
  for (int k = 0; k < 3; k++, part += 12) {
    enemy_t *a = at_end(e);
    if (!a) return;
    int8_t i = (int8_t)(a - e->pool);
    if (first < 0) first = i;
    a->type = en_rw(e, part);
    uint32_t upd = en_rl(e, part + 2), hit = en_rl(e, part + 6);
    a->role = upd == 0x50DAEu ? R_GUN3_MID : R_GUN3_END;
    a->hit = hit == 0x50E64u ? H_GUN3_LEFT : hit == 0x50F1Cu ? H_GUN3_RIGHT : H_NONE;
    a->flags = 0;
    a->x = x * 65536;
    a->y = y * 65536;
    a->hp = en_rs(e, GUN3_HP);
    a->cell = (int16_t)cell;
    a->head = first;
    en_box_off(a);
    int off = en_rs(e, part + 10);
    cell += off / 2;
    x += off * 8;
  }
}

static void update_gun3(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  int32_t x = a->x >> 16, y = (a->y >> 16) - f->line;
  if (a->role == R_GUN3_MID) { /* $50DAE */
    a->v40 = (int16_t)(a->v40 + 4 >= 16 ? 0 : a->v40 + 4);
    uint32_t w = GUN3_MIDDLE + (uint32_t)a->v40;
    map_set(e, a->cell, en_rw(e, w));
    map_set(e, a->cell + 1, en_rw(e, w + 2));
    box_at(a, x, y + 2, x + 32, y + 14);
    return;
  }
  a->v40 = (int16_t)((a->v40 + 1) & 3); /* $50E00 */
  uint32_t t = a->type == T_GUN3_LEFT ? GUN3_LEFT_CELLS : GUN3_RIGHT_CELLS;
  map_set(e, a->cell, en_rw(e, t + 2u * (uint32_t)a->v40));
  box_at(a, x + 3, y + 3, x + 13, y + 13);
}

/* An end destroyed ($50E64, $50F1C): the gun's cells to their wreck, its
 * three parts gone. */
static void hit_gun3(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) return;
  int32_t x = a->x >> 16, y = (a->y >> 16) - e->line;
  boom(e, x + 8, y + 8, false, ev);
  int32_t c = a->hit == H_GUN3_LEFT ? a->cell : a->cell - 3;
  map_set(e, c, 0x8881);
  map_set(e, c + 1, 0);
  map_set(e, c + 2, 0);
  map_set(e, c + 3, 0x8875);
  int8_t head = a->head;
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *o = &e->pool[i];
    if (o->head == head && (o->role == R_GUN3_END || o->role == R_GUN3_MID)) en_kill(o);
  }
  ev->score += 200;
}

/* ---- the wall guns (kind 2: $5156C, update $515DC) ---- */

static void spawn_gun(enemies_t *e, uint32_t rec) {
  bool left = en_rw(e, rec + 12) != 0;
  enemy_t *a = in_map(e, rec, left ? T_GUN_LEFT : T_GUN_RIGHT, R_GUN, H_WALL);
  if (!a) return;
  a->hp = en_rs(e, GUN_HP);
  a->cols = a->rows = 1;
  a->tiles = left ? GUN_LEFT : GUN_RIGHT;
  a->wreck = left ? GUN_WRECK_LEFT : GUN_WRECK_RIGHT;
  a->worth = 100;
  map_set(e, a->cell, en_rw(e, a->tiles));
}

static void update_gun(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (gone(a, f)) return;
  int32_t x = a->x >> 16, y = (a->y >> 16) - f->line;
  box_at(a, x + 2, y + 2, x + 14, y + 14);
  if (a->v40 == 0 && !carry(e, &a->fire_acc, GUN_RATE)) return;
  a->v40++;
  map_set(e, a->cell, en_rw(e, a->tiles + 2u * (uint32_t)a->v40));
  if (a->v40 == 3) {
    bool right = a->type == T_GUN_RIGHT;
    int8_t s = en_shot(e, 0, right ? x + 24 : x, y + 8, right ? 2 : 6,
                       en_rw(e, GUN_SPEED));
    if (s >= 0) en_anim_start(e, &e->pool[s], right ? GUN_SHOT_RIGHT : GUN_SHOT_LEFT);
    return;
  }
  if (a->v40 == 7) a->v40 = 0;
}

/* ---- the turrets (kinds 3 and 9: $5126A, update $5131E) ---- */

static void spawn_turret(enemies_t *e, uint32_t rec, int once) {
  enemy_t *a = in_map(e, rec, T_TURRET, R_TURRET, H_TURRET);
  if (!a) return;
  a->v42 = en_rs(e, rec + 12) & 7;
  a->hp = en_rs(e, TURRET_HP);
  a->v48 = (int16_t)once;
  int32_t c = a->cell;
  if (map_get(e, c - 1) == 0x8729) { /* its plinth */
    map_set(e, c - 1, 0x80ED);
    map_set(e, c + 19, 0x81AD);
  }
  if (map_get(e, c + 2) == 0x8735) {
    map_set(e, c + 2, 0x82CD);
    map_set(e, c + 22, 0x8391);
  }
  map_2x2(e, c, TURRET_CELLS + 8u * (uint32_t)a->v42);
}

static void update_turret(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (gone(a, f)) return;
  int32_t x = a->x >> 16, y = (a->y >> 16) - f->line;
  box_at(a, x + 4, y + 4, x + 28, y + 28);
  if (a->v40 != 8) {
    if (a->v40 != 0 || y >= -16) { /* on the screen: it turns once round */
      a->v40++;
      a->v42 = (int16_t)((a->v42 + 1) & 7);
    }
  } else {
    int d = en_dir_to(f->ship_x - x - 16, f->ship_y - y - 16) - a->v42;
    if (d != 0) a->v42 = (int16_t)(a->v42 + ((d & 7) < 4 ? 1 : -1));
    a->v42 &= 7;
    if (carry(e, &a->fire_acc, TURRET_RATE)) {
      uint32_t k = (uint32_t)a->v42 * 4u & 28u;
      uint32_t s = (k & 12u) + ((e->frame & 1u) ? 0u : 16u);
      en_shot(e, en_rl(e, TURRET_SHOTS + s), x + 16 + en_rs(e, TURRET_AIM + k),
              y + 16 + en_rs(e, TURRET_AIM + k + 2), a->v42, en_rw(e, TURRET_SPEED));
    }
  }
  map_2x2(e, a->cell, TURRET_CELLS + 8u * (uint32_t)a->v42);
}

static void hit_turret(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) return;
  if (a->v48 >= 0) e->vars[V_ONCE0 + a->v48] = 1; /* gone for good */
  boom(e, (a->x >> 16) + 16, (a->y >> 16) - e->line + 16, true, ev);
  int32_t c = a->cell;
  if (map_get(e, c - 1) == 0x80ED) {
    map_set(e, c - 1, 0x8729);
    map_set(e, c + 19, 0x87D5);
  }
  if (map_get(e, c + 2) == 0x82CD) {
    map_set(e, c + 2, 0x8735);
    map_set(e, c + 22, 0x87E1);
  }
  map_set(e, c, 0);
  map_set(e, c + 1, 0);
  map_set(e, c + 20, 0);
  map_set(e, c + 21, 0);
  ev->score += 400;
  en_kill(a);
}

/* ---- the eight-way turrets (kind 4: $51000, update $510A2) ---- */

static void spawn_gun8(enemies_t *e, uint32_t rec) {
  enemy_t *a = in_map(e, rec, T_GUN8, R_GUN8, H_GUN8);
  if (!a) return;
  a->hp = en_rs(e, GUN8_HP);
  int32_t c = a->cell;
  if (map_get(e, c - 1) == 0x8711) {
    map_set(e, c - 1, 0x00DD);
    map_set(e, c + 19, 0x019D);
  }
  if (map_get(e, c + 2) == 0x871D) {
    map_set(e, c + 2, 0x00DD);
    map_set(e, c + 22, 0x019D);
  }
  map_2x2(e, c, GUN8_CELLS);
}

static void update_gun8(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (gone(a, f)) return;
  int32_t x = a->x >> 16, y = (a->y >> 16) - f->line;
  box_at(a, x + 4, y + 4, x + 28, y + 28);
  a->v40 = (int16_t)((a->v40 + 1) & 15);
  if (carry(e, &a->fire_acc, GUN8_RATE)) {
    for (int d = 7; d >= 0; d--) {
      int8_t s = en_shot(e, 0, x + 16, y + 16, d, en_rw(e, GUN8_SPEED));
      if (s >= 0) en_anim_start(e, &e->pool[s], GUN8_SHOT);
    }
  }
  map_2x2(e, a->cell, GUN8_CELLS + 4u * (uint32_t)(a->v40 & 14));
}

static void hit_gun8(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) return;
  boom(e, (a->x >> 16) + 16, (a->y >> 16) - e->line + 16, true, ev);
  int32_t c = a->cell;
  if (map_get(e, c - 1) == 0x00DD) {
    map_set(e, c - 1, 0x8711);
    map_set(e, c + 19, 0x87BD);
  }
  if (map_get(e, c + 2) == 0x00DD) {
    map_set(e, c + 2, 0x871D);
    map_set(e, c + 22, 0x87C9);
  }
  map_set(e, c, 0);
  map_set(e, c + 1, 0);
  map_set(e, c + 20, 0);
  map_set(e, c + 21, 0);
  ev->score += 500;
  en_kill(a);
}

/* ---- the bosses' parts ---- */

/* The parts' routines by the original's addresses. */
static uint8_t part_role(uint32_t upd) {
  switch (upd) {
    case 0x508E0u: return R_MID_MAIN;
    case 0x507DAu: return R_AIMER;
    case 0x506DEu: return R_MID_EYE;
    case 0x5078Cu: return R_TREAD;
    case 0x4FF7Eu: return R_END_MAIN;
    case 0x4FF3Cu: return R_END_BAR;
    case 0x4FF62u: return R_END_PART;
    case 0x4FF6Eu: return R_END_CORE;
    case 0x5032Au: return R_LAUNCHER;
    default: return R_WRECK;
  }
}

static uint8_t part_hit(uint32_t hit) {
  switch (hit) {
    case 0x50870u: return H_MID_AIMER;
    case 0x50722u: return H_MID_EYE;
    case 0x503D2u: return H_END_PART;
    case 0x504A6u: return H_END_CORE;
    case 0xF3Eu: return H_SHIELD;
    default: return H_NONE;
  }
}

/* $507DA: at the ship, firing at it. */
static void update_aimer(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  follow(e, a);
  int32_t x = a->x >> 16, y = a->y >> 16;
  int d = en_dir_to(f->ship_x - x, f->ship_y - y);
  a->v42 = (int16_t)d;
  a->sprite = en_rl(e, AIMER_SPRITES + 4u * (uint32_t)d);
  if (carry(e, &a->fire_acc, AIMER_RATE)) {
    uint32_t sprite = en_rl(e, SHOTS + 4u * en_rw(e, AIMER_SHOT));
    en_shot(e, sprite, x + 8, y + 8, d, en_rw(e, AIMER_SPEED));
  }
  en_box(e, a);
}

/* A laser out of the mid boss (down) or a launcher (up) ($50B38). */
static void spawn_laser(enemies_t *e, int32_t x, int32_t y, int16_t step) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->fx, e, i);
  a->type = T_LASER;
  a->role = R_LASER;
  a->x = x * 65536;
  a->y = y * 65536;
  a->v40 = 0;
  a->v42 = step;
}

/* Growing to its length where it starts, then on: into the ship (6 of its
 * shield) or off the screen. */
static void update_laser(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                         enemies_events_t *ev) {
  int32_t x = a->x >> 16, y = a->y >> 16;
  int step = en_rs(e, LASER_STEP);
  if (a->v40 != LASER_MAX) {
    a->v40 = (int16_t)(a->v40 + step > LASER_MAX ? LASER_MAX : a->v40 + step);
    y += f->step;
    if (a->v42 < 0) y -= step;
  } else {
    y += a->v42;
    if (a->v42 >= 0 ? y >= 192 : y + a->v40 < 0) {
      en_kill(a);
      return;
    }
  }
  a->y = y * 65536;
  box_at(a, x, y, x + 11, y + a->v40);
  if (f->ship_flying && en_overlap(a->box, f->ship_box)) into_ship(e, a, f, 6, false, ev);
}

/* $50C08: the beam, its ends. */
static bool draw_laser(enemies_t *e, enemy_t *a, enemies_draw_fn draw, void *ctx) {
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (a->v42 >= 0 || a->v40 == LASER_MAX) {
    const x2_sprite_t *s = en_sprite(e, LASER_TOP);
    if (s) draw(ctx, s, (const uint8_t *)e->level + s->offset, x, y, false);
  }
  if (a->v42 < 0 || a->v40 == LASER_MAX) {
    const x2_sprite_t *s = en_sprite(e, LASER_BOTTOM);
    if (s) draw(ctx, s, (const uint8_t *)e->level + s->offset, x, y + a->v40 - 1, false);
  }
  en_strip(e, ctx, x, y, a->v40, k_beam, 12);
  return true;
}

/* ---- the mid boss (kind 5: $50626) ---- */

static void spawn_mid(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  if (e->vars[V_MID]) return;
  e->vars[V_MID] = 1;
  int32_t y = en_rs(e, rec + 10) - 8 - f->line;
  int8_t first = -1;
  for (int k = 0; k < 10; k++) {
    uint32_t p = MID_PARTS + 24u * (uint32_t)k;
    enemy_t *a = at_end(e);
    if (!a) return;
    int8_t i = (int8_t)(a - e->pool);
    if (first < 0) first = i;
    a->type = en_rw(e, p);
    a->role = part_role(en_rl(e, p + 2));
    a->hit = part_hit(en_rl(e, p + 10));
    a->flags = F_KEEP | (en_rl(e, p + 6) == 0xE24u ? F_DRAW : 0);
    a->sprite = en_rl(e, p + 14);
    a->x = MID_X * 65536;
    a->y = y * 65536;
    a->tiles = (uint32_t)(int32_t)en_rs(e, p + 18);
    a->wreck = (uint32_t)(int32_t)en_rs(e, p + 20);
    a->v42 = 4;
    a->hp = k == 5 ? en_rs(e, MID_EYE_HP) : en_rs(e, p + 22);
    a->head = first;
    en_box_off(a);
  }
  if (first < 0) return;
  /* Its block's two rates start where the allocator leaves them: an object
   * never used before holds the pool's fill, $C2FF, so the first homing
   * creatures leave at once. */
  e->pool[first].flags |= F_DRAW;
  e->pool[first].fire_acc = 0xC2;
  e->pool[first].v48 = 0xFF;
}

/* $508E0: up and down at random between two lines; lasers down the screen,
 * homing creatures to each side. */
static void update_mid(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                       enemies_events_t *ev) {
  e->back_floor = MID_BACK;
  int32_t y = (a->y >> 16) + f->step;
  if (a->v42 > 0) {
    en_sound(ev, 21, 2);
    y++;
    a->v42--;
  } else if (a->v42 < 0) {
    en_sound(ev, 21, 2);
    y--;
    a->v42++;
  } else {
    uint32_t r = en_rnd(e);
    if ((uint8_t)r < 15u) {
      if (r & 1u) {
        int n = (uint16_t)(MID_BOTTOM - y - f->line);
        if (n) a->v42 = (int16_t)(en_rnd_mod(e, n) + 1);
      } else {
        int n = (uint16_t)(y + f->line - MID_TOP);
        if (n) a->v42 = (int16_t)-(en_rnd_mod(e, n) + 1);
      }
    }
  }
  a->y = y * 65536;
  if (a->v40 != 0 || carry(e, &a->fire_acc, MID_RATE)) {
    if (++a->v40 == 9) {
      a->v40 = 0;
    } else if (a->v40 == 3) {
      en_sound(ev, 7, 2);
      spawn_laser(e, 150, y + 48, en_rs(e, LASER_STEP));
    }
  }
  uint8_t acc2 = (uint8_t)a->v48;
  if (carry(e, &acc2, MID_RATE2)) {
    spawn_homer(e, 110, y + 48, 6);
    spawn_homer(e, 210, y + 48, 2);
  }
  a->v48 = acc2;
}

/* $50A52: its block, rows 1-3 of columns 2-3 by its step. */
static bool draw_mid(enemies_t *e, enemy_t *a, bool flash, enemies_block_fn block,
                     void *ctx) {
  for (int k = 0; k < 36; k++) {
    e->boss_block[(k / 6) * ENEMIES_BOSS_COLS + k % 6] = en_rw(e, MID_BLOCK + 2u * (uint32_t)k);
  }
  uint32_t fr = MID_FRAMES + 12u * (uint32_t)a->v40;
  for (int r = 0; r < 3; r++) {
    e->boss_block[(1 + r) * ENEMIES_BOSS_COLS + 2] = en_rw(e, fr + 4u * (uint32_t)r);
    e->boss_block[(1 + r) * ENEMIES_BOSS_COLS + 3] = en_rw(e, fr + 4u * (uint32_t)r + 2);
  }
  block(ctx, e->boss_block, 6, 6, MID_X, a->y >> 16, flash);
  return true;
}

/* $506DE: its eye's sprite by a phase its wounds speed up. */
static void update_eye(enemies_t *e, enemy_t *a) {
  follow(e, a);
  int32_t v = 256 - a->hp + a->v40;
  if (v >= 1536) v -= 1536;
  a->v40 = (int16_t)v;
  a->sprite = en_rl(e, MID_EYES + (((uint32_t)v >> 6) & ~3u));
  en_box(e, a);
}

/* $5078C: its tracks turning with the world. */
static void update_tread(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  follow(e, a);
  uint32_t t = a->type == T_TREAD ? TREADS : TREADS2;
  a->sprite = en_rl(e, t + 4u * (uint32_t)(((a->y >> 16) + f->line) & 3));
}

/* $50870: a turret's last hit leaves its wreck. */
static void to_wreck(enemies_t *e, enemy_t *a, enemies_events_t *ev) {
  a->role = R_WRECK;
  a->hit = H_NONE;
  en_box_off(a);
  ev->score += 200;
  boom(e, (a->x >> 16) + 8, (a->y >> 16) + 8, false, ev);
}

/* $50722: the eye destroyed, the mid boss with it. */
static void mid_destroyed(enemies_t *e, enemies_events_t *ev) {
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) en_kill(&e->pool[i]);
  enemies_burst(e, MID_X, MID_TOP - e->line, 96, 160, 20, true, ev);
  en_payout(e, 5);
  e->mid_line = 0;
  for (int r = 0; r < 10; r++) { /* its place in the map cleared */
    for (int c = 0; c < 8; c++) map_set(e, 128 * 20 + 10 + r * 20 + c, 0);
  }
  ev->score += 1500;
}

/* ---- the end boss (kind 6: $4FDFA) ---- */

static void spawn_end(enemies_t *e, uint32_t rec, enemies_events_t *ev) {
  (void)rec;
  if (e->vars[V_END]) return;
  e->vars[V_END] = 1;
  ev->restart = true; /* $3D9C: the place to come back to */
  ev->restart_x = 160;
  ev->restart_line = END_LINE;
  e->tables_closed = true;
  for (int c = 0; c < 39 * 20; c++) map_set(e, c, 0); /* its block in the map */
  for (int r = 0; r < 18; r++) {
    for (int c = 0; c < 15; c++) {
      map_set(e, (6 + r) * 20 + 3 + c, en_rw(e, END_BLOCK + 2u * (uint32_t)(r * 15 + c)));
    }
  }
  e->vars[V_JUMP] = END_LINE;
  int8_t first = -1;
  for (int k = 0; k < 22; k++) {
    uint32_t p = END_PARTS + 24u * (uint32_t)k;
    enemy_t *a = at_end(e);
    if (!a) return;
    int8_t i = (int8_t)(a - e->pool);
    if (first < 0) first = i;
    a->type = en_rw(e, p);
    a->role = part_role(en_rl(e, p + 2));
    a->hit = part_hit(en_rl(e, p + 10));
    a->flags = F_KEEP | (en_rl(e, p + 6) == 0xE24u ? F_DRAW : 0);
    en_anim_start(e, a, en_rl(e, p + 14));
    a->x = 48 * 65536;
    a->y = -320 * 65536;
    a->tiles = (uint32_t)(int32_t)en_rs(e, p + 18);
    a->wreck = (uint32_t)(int32_t)en_rs(e, p + 20);
    a->v42 = 4;
    a->hp = k == 0 ? en_rb(e, END_HP) : en_rb(e, p + 22);
    a->cols = en_rb(e, p + 23); /* a bar's height */
    if (a->cols) a->flags |= F_BIG;
    a->head = first;
    en_box_off(a);
  }
  if (first >= 0) e->pool[first].flags |= F_DRAW; /* its jaws */
  e->vars[V_PARTS] = 18;
}

/* $4FF7E: held at line 416, the backdrop going on at the top; its jaws
 * open now and then at either end, each opening a homing creature. */
static void update_end(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                       enemies_events_t *ev) {
  ev->back_limit = END_LINE;
  e->back_floor = END_LINE;
  if (f->line == 0) e->drift = e->vars[V_STEP];
  a->y += f->step * 65536;
  if (a->v40 == 0) {
    if (carry(e, &a->fire_acc, END_RATE)) {
      en_sound(ev, 30, 1);
      a->v40 = (e->frame & 1u) ? 2 : -2;
    }
    return;
  }
  a->v40 = (int16_t)(a->v40 + (a->v40 > 0 ? 2 : -2));
  int v = a->v40 < 0 ? -a->v40 : a->v40;
  if (v == 20) {
    en_sound(ev, 31, 1);
  } else if (v == 40) {
    ev->stop_effect = true;
    enemy_t *h = at_end(e);
    if (!h) return;
    h->type = T_END_HOMER;
    h->role = R_END_HOMER;
    h->hit = H_SINGLE;
    h->x = ((a->x >> 16) + 119) * 65536;
    h->y = ((a->y >> 16) + (a->v40 > 0 ? 101 : 245)) * 65536;
    en_anim_start(e, h, END_HOMER);
    h->hp = en_rs(e, END_HOMER_HP);
    h->worth = 150;
    h->v40 = 1;
    en_box_off(h);
  } else if (v == 60) {
    a->v40 = 0;
  }
}

/* $500DA: the jaws, by how far they are open. */
static bool draw_end(enemies_t *e, enemy_t *a, enemies_draw_fn draw, void *ctx) {
  if (a->v40 == 0) return true;
  int d = (a->v40 < 0 ? -a->v40 : a->v40) & ~3;
  int32_t x = (a->x >> 16) + 105, y = (a->y >> 16) + (a->v40 < 0 ? 229 : 85);
  const x2_sprite_t *s = en_sprite(e, en_rl(e, JAWS + (uint32_t)d));
  if (s) draw(ctx, s, (const uint8_t *)e->level + s->offset, x, y, false);
  if (d >= 20 && d < 40) {
    s = en_sprite(e, en_rl(e, JAWS2 + (uint32_t)d));
    if (s) draw(ctx, s, (const uint8_t *)e->level + s->offset, x + 14, y + 16, false);
  }
  return true;
}

/* $5025E: at the ship as the pods' creatures, faster once the boss is
 * bare; into the ship, 4 of its shield. */
static void update_end_homer(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                             enemies_events_t *ev) {
  en_animate(e, a);
  int32_t x = a->x >> 16, y = a->y >> 16;
  if ((++a->v40 & 7) == 0) {
    int d = turn(a->v42, en_dir_to(f->ship_x - x, f->ship_y - y), false, x);
    if (d != a->v42) {
      a->v42 = (int16_t)d;
      en_anim_start(e, a, en_rl(e, END_HOMER_ANIMS + 4u * (uint32_t)d));
    }
  }
  int32_t dx = en_rs(e, HOMER_STEPS + 2u * (uint32_t)a->v42);
  int32_t dy = en_rs(e, HOMER_STEPS + 16u + 2u * (uint32_t)a->v42);
  if (e->vars[V_PARTS] == 0) {
    dx *= 2;
    dy *= 2;
  }
  a->x = (x + dx) * 65536;
  a->y = (y + dy) * 65536;
  en_box(e, a);
  if (f->ship_flying && en_overlap(a->box, f->ship_box)) into_ship(e, a, f, 4, false, ev);
}

/* $5032A: a laser up when its animation comes to the shot. */
static void update_launcher(enemies_t *e, enemy_t *a) {
  en_animate(e, a);
  follow(e, a);
  if (a->sprite == LAUNCH_SPRITE && a->frames == 1) {
    spawn_laser(e, (a->x >> 16) - 2, (a->y >> 16) - 24, (int16_t)-en_rs(e, LASER_STEP));
  }
  if (a->frames == 0 && a->y >= 0 && carry(e, &a->fire_acc, LAUNCHER_RATE)) {
    a->frames = 1;
  }
  en_box(e, a);
}

/* $503D2: one of the eighteen; a turret leaves its wreck. */
static void hit_end_part(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) return;
  e->vars[V_PARTS]--;
  ev->score += 200;
  en_explode(e, a, a->flags & F_BIG, ev);
  if (a->type == T_AIMER) {
    a->role = R_WRECK;
    a->hit = H_NONE;
    en_box_off(a);
    return;
  }
  en_kill(a);
}

/* $504A6: its core, wounding the boss itself; destroyed, the level won. */
static void hit_end_core(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  a->flags |= F_FLASH;
  if (a->head < 0) return;
  enemy_t *main = &e->pool[a->head];
  uint16_t before = (uint16_t)main->hp;
  main->hp = (int16_t)(main->hp - damage);
  if (before > (uint16_t)damage) {
    uint8_t whole = en_rb(e, END_HP);
    uint32_t k = whole ? (uint32_t)(uint16_t)main->hp * 11u / whole : 0;
    a->sprite = en_rl(e, END_CORE + 4u * k);
    return;
  }
  for (int c = 12 * 20; c < 12 * 20 + 360; c++) map_set(e, c, 0);
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) en_kill(&e->pool[i]);
  en_payout(e, 10);
  enemies_burst(e, 0, 0, 320, 192, 40, true, ev);
  e->won = true;
  for (int c = 0; c < 39 * 20; c++) map_set(e, c, 0);
}

/* ---- the hooks ---- */

/* $517AA. */
static void start(enemies_t *e) {
  int loop = e->hp_small; /* enemies_init's: the round */
  e->hp_small = (int16_t)(loop * 12);
  e->hp_big = (int16_t)(loop * 14);
  e->lowest = 0;
  e->mid_line = 2448; /* its mid boss's record */
  memset(e->vars, 0, sizeof(e->vars));
}

/* The original's has none; the end boss's jump of the world. */
static int32_t frame(enemies_t *e, int32_t line, int32_t step, int32_t ship_y,
                     int32_t *back_limit) {
  (void)line;
  (void)ship_y;
  e->vars[V_STEP] = (int16_t)step;
  int32_t jump = e->vars[V_JUMP] ? e->vars[V_JUMP] : -1;
  if (jump >= 0) *back_limit = jump;
  e->vars[V_JUMP] = 0;
  return jump;
}

/* $4F3AA. */
static void scenery(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                    int kind, enemies_events_t *ev) {
  switch (kind) {
    case 1: spawn_gun3(e, rec); break;
    case 2: spawn_gun(e, rec); break;
    case 3: spawn_turret(e, rec, -1); break;
    case 4: spawn_gun8(e, rec); break;
    case 5: spawn_mid(e, f, rec); break;
    case 6: spawn_end(e, rec, ev); break;
    case 7: spawn_snake(e, f, rec); break;
    case 8: spawn_pod(e, f, rec); break;
    case 9: { /* $51250: once destroyed, for good */
      int k = en_rw(e, rec + 6);
      if (k >= 0 && k <= 1 && !e->vars[V_ONCE0 + k]) spawn_turret(e, rec, k);
      break;
    }
    default: break;
  }
}

static void update(enemies_t *e, int8_t i, const enemies_frame_t *f,
                   enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  switch (a->role) {
    case R_SNAKE: update_snake(e, a, f); break;
    case R_POD: update_pod(e, a, f); break;
    case R_HOMER: update_homer(e, a, f, ev); break;
    case R_GUN3_END:
    case R_GUN3_MID: update_gun3(e, a, f); break;
    case R_GUN: update_gun(e, a, f); break;
    case R_TURRET: update_turret(e, a, f); break;
    case R_GUN8: update_gun8(e, a, f); break;
    case R_MID_MAIN: update_mid(e, a, f, ev); break;
    case R_AIMER: update_aimer(e, a, f); break;
    case R_MID_EYE: update_eye(e, a); break;
    case R_TREAD: update_tread(e, a, f); break;
    case R_WRECK: /* $508C6 */
      follow(e, a);
      a->sprite = WRECK;
      break;
    case R_END_MAIN: update_end(e, a, f, ev); break;
    case R_END_BAR: /* $4FF3C: a bar across the boss, stopping shots */
      follow(e, a);
      box_at(a, a->x >> 16, a->y >> 16, (a->x >> 16) + a->hp, (a->y >> 16) + a->cols);
      break;
    case R_END_PART: /* $4FF62 */
      en_animate(e, a);
      follow(e, a);
      en_box(e, a);
      break;
    case R_END_CORE: /* $4FF6E: hurt only once the boss is bare */
      follow(e, a);
      if (e->vars[V_PARTS] == 0) en_box(e, a);
      break;
    case R_LAUNCHER: update_launcher(e, a); break;
    case R_END_HOMER: update_end_homer(e, a, f, ev); break;
    default: break;
  }
}

static void update_fx(enemies_t *e, int8_t i, const enemies_frame_t *f,
                      enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  if (a->role == R_LASER) update_laser(e, a, f, ev);
}

static void hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  switch (a->hit) {
    case H_GUN3_LEFT:
    case H_GUN3_RIGHT: hit_gun3(e, a, damage, ev); break;
    case H_TURRET: hit_turret(e, a, damage, ev); break;
    case H_GUN8: hit_gun8(e, a, damage, ev); break;
    case H_MID_AIMER: {
      a->flags |= F_FLASH;
      uint16_t before = (uint16_t)a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if (before <= (uint16_t)damage) to_wreck(e, a, ev);
      break;
    }
    case H_MID_EYE: {
      a->flags |= F_FLASH;
      if (a->head >= 0) e->pool[a->head].flags |= F_FLASH;
      uint16_t before = (uint16_t)a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if (before <= (uint16_t)damage) mid_destroyed(e, ev);
      break;
    }
    case H_END_PART: hit_end_part(e, a, damage, ev); break;
    case H_END_CORE: hit_end_core(e, a, damage, ev); break;
    default: break;
  }
}

static bool draw(enemies_t *e, enemy_t *a, int32_t line, bool flash,
                 enemies_draw_fn fn, enemies_block_fn block, void *ctx) {
  (void)line;
  switch (a->role) {
    case R_MID_MAIN: return draw_mid(e, a, flash, block, ctx);
    case R_END_MAIN: return draw_end(e, a, fn, ctx);
    case R_LASER: return draw_laser(e, a, fn, ctx);
    default: return false;
  }
}

const enemies_level_t enemies_level5 = {start, scenery, frame, update,
                                         update_fx, NULL, hit, draw, NULL};
