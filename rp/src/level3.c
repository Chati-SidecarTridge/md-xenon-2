/**
 * File: level3.c
 * Description: Level 3's own code (enemies_level.h), ported from the
 *              original's at $4F03C-$51300 and run on the level's tables in
 *              the store.
 *
 * - Its waves ($510FA): kind 3 a snake (eight parts on one path, $4F244,
 *   $4F24E, $4F258: the path's step and fire, the sprite by direction),
 *   kind 4 an enemy whose animation is chosen by the screen edge it came
 *   in by ($4F2D8); kind 3's big enemies' hit points its own ($51130).
 * - Its scenery ($4F8D0): wall lizards (kind 1, $500EA: eight parts that
 *   lunge out of the wall level with the ship), walkers (kind 2, $50308:
 *   to the screen's middle, a bolt at each stop), the mid boss (kind 3),
 *   small and big wall objects in the map (kinds 4 and 5, $50420, $503A6:
 *   a cell opening and firing; the big one a mouth, then its top a burst
 *   of eight), turrets on tracks (kind 6, $4F8F6: along the map's cells,
 *   turning at their ends, aiming at the ship); their numbers in the
 *   level's block at $4F03C.
 * - Its mid boss ($4FFEC): seventeen parts on one path, a body firing at
 *   the ship, two eyes that only take hits, two arms swinging chains of
 *   segments and a claw; the world held at line 2800 ($CE8) while it
 *   lives; its eyes destroyed, the whole of it destroyed and a payout that
 *   brings the mid shop. While it is out the original's drawer leaves the
 *   map's tiles out ($D9B): the map is empty there, so the playfield is
 *   drawn as always.
 * - Its end boss (from its frame code at line 208, $4F510): a worm of
 *   eleven parts on one of the level's scripted paths, hit only at its head
 *   (the shared hit points $D7C), its holes shown over the playfield while
 *   it is out ($51160); destroyed, twenty explosions, a payout, the level
 *   won.
 */

#include <string.h>

#include "enemies_level.h"

/* Its roles and hits. */
enum { R_SNAKE_HEAD = R_LEVEL, R_SNAKE_BODY, R_SNAKE_TAIL, R_EDGE, R_TURRET,
       R_LIZARD_HEAD, R_LIZARD_BODY, R_LIZARD_TAIL, R_MARK, R_MARK_END,
       R_WALKER, R_BOLT, R_WALL_SMALL, R_WALL_BIG, R_WALL_TOP, R_BODY,
       R_FOLLOW_PART, R_EYE, R_ARM, R_SEGMENT, R_CLAW, R_WORM_HEAD,
       R_WORM_NECK, R_WORM_BODY, R_WORM_TAIL, R_HOLES };
enum { H_BIG_WALL = H_LEVEL, H_WALL_TOP, H_EYE, H_WORM };

/* Its variables (e->vars). */
enum { V_BODY_SEEN, /* $E0E: the mid boss's body updated this frame */
       V_DEAD,      /* $E0A: the end boss destroyed */
       V_TAIL,      /* $E0C: its tail out this frame */
       V_TAIL2,     /* $E0D */
       V_SPAWNS,    /* $D7A: the end bosses made, up to 1 */
       V_HP,        /* $D7C: the end boss's hit points */
       V_EYE_A,     /* $D76: the mid boss's eyes' hit points */
       V_EYE_B,     /* $D78 */
       V_EYES };    /* $D80: its eyes left */

/* The level's block of numbers ($4F03C). */
#define TURRET_HP 0x4F044u
#define MID_SPEED 0x4F046u
#define LIZARD_HP 0x4F048u
#define WALKER_HP 0x4F04Au
#define BIG_HP 0x4F04Cu
#define TURRET_RATE 0x4F04Fu   /* byte */
#define TURRET_SHOT 0x4F050u   /* words: shot (an index), speed */
#define TURRET_SPEED 0x4F052u
#define BOLT_SPEED 0x4F054u
#define BIG_CHANCE 0x4F057u    /* bytes */
#define BIG_AGAIN 0x4F059u
#define BIG_RATE 0x4F05Bu
#define BIG_SHOT 0x4F05Cu      /* words: shot (an index), speed */
#define BIG_SPEED 0x4F05Eu
#define SMALL_HP 0x4F05Eu
#define TOP_HP 0x4F060u
#define TOP_RATE 0x4F063u      /* byte */
#define TOP_SPEED 0x4F064u
#define SMALL_RATE 0x4F067u    /* byte */
#define SMALL_SPEED 0x4F068u
#define WORM_SPEED 0x4F042u
#define WORM_HP 0x4F06Au
#define EYE_HP 0x4F06Cu
#define WORM_RATE 0x4F06Fu     /* byte */
#define WORM_SHOT_SPEED 0x4F070u
#define BODY_RATE 0x4F075u     /* byte */
#define BODY_SPEED 0x4F076u
#define BLINK_RATE 0x4F079u    /* byte */

#define SHOTS 0x6D2F8u         /* the shot sprites: pointers to pointers */
#define SNAKE_HEAD 0x4F278u    /* 8 sprites by direction, each part */
#define SNAKE_BODY 0x4F298u
#define SNAKE_TAIL 0x4F2B8u
#define EDGE_ANIMS 0x4F33Eu    /* 4 animations: by the edge come in by */
#define TURRET_DX 0x4F990u     /* by its direction: where it starts, */
#define TURRET_DY 0x4F9A0u
#define TURRET_ANIMS 0x4F9B0u  /* its animation, */
#define TRACK_DX 0x50490u      /* its step, */
#define TRACK_DY 0x504A0u
#define TRACK_AHEAD 0x50650u   /* the cell ahead (a byte offset), */
#define TURN_DX 0x50660u       /* and where it turns to */
#define TURN_DY 0x50670u
#define TURN_DIR 0x50680u
#define LIZARD_LEFT 0x554B2u   /* a lizard's part, by side */
#define LIZARD_RIGHT 0x55402u
#define LIZARD_TAIL_LEFT 0x502B4u
#define LIZARD_TAIL_RIGHT 0x502DEu
#define WALK_RIGHT 0x509CEu    /* the walkers' animations */
#define STOP_RIGHT 0x509F2u
#define WALK_LEFT 0x50A1Cu
#define STOP_LEFT 0x50A40u
#define BOLT_LEFT 0x55562u
#define BOLT_RIGHT 0x555B8u
#define SMALL_LEFT 0x510DAu    /* a small wall's cell by step, its wreck */
#define SMALL_RIGHT 0x510E8u
#define SMALL_WRECK_LEFT 0x510F6u
#define SMALL_WRECK_RIGHT 0x510F8u
#define SMALL_SHOT 0x588D0u
#define BIG_CELLS 0x503F0u     /* the big wall: its 6 x 4 cells, */
#define BIG_MOUTH 0x50BA2u     /* its mouth's 2 x 2 by step, */
#define BIG_FLASH 0x50C74u     /* its 4 x 3 middle, flashed */
#define TOP_STEPS 0x50F2Cu     /* its top's 2 x 2 by step */
#define TOP_BURST 0x50DECu     /* 8 animations: its burst's shots */
#define MID_PARTS 0x4FAF0u     /* 17 x (update, hit, animation, dx, dy) */
#define MID_PATH 0x512F4u
#define BODY_SHOT 0x4FD14u
#define EYE_BLINK 0x4FC40u
#define EYE_DEAD_A 0x59290u
#define EYE_DEAD_B 0x59322u
#define CLAW 0x4FFACu          /* 16 sprites: by direction, two frames */
#define WORM_PARTS 0x4F470u    /* parts - 1, then (delay, update, sprite) */
#define WORM_PATHS 0x6D408u
#define WORM_HEAD_ANIMS 0x4F600u
#define WORM_SHOT 0x4F69Eu
#define WORM_NECK 0x4F78Eu     /* 8 sprites by direction, each part */
#define WORM_BODY 0x4F7D2u
#define WORM_TAIL 0x4F822u
#define HOLES 0x5119Au         /* the end boss's holes: (words, cols - 1,
                                  rows - 1, world y, x / 2) */

/* The original's routines the level's code names. */
#define UPD_SNAKE_HEAD 0x4F244u
#define UPD_SNAKE_BODY 0x4F24Eu
#define UPD_SNAKE_TAIL 0x4F258u
#define UPD_EDGE 0x4F2D8u
#define UPD_BODY 0x4FC88u
#define UPD_FOLLOW 0x4FCF0u
#define UPD_EYE 0x4FD38u
#define UPD_ARM 0x4FE52u
#define UPD_SEGMENT 0x4FF12u
#define UPD_CLAW 0x4FF62u
#define UPD_WORM_HEAD 0x4F620u
#define UPD_WORM_NECK 0x4F76Au
#define UPD_WORM_BODY 0x4F7AEu
#define UPD_WORM_TAIL 0x4F7F2u
#define HIT_EYE 0x4FDC0u

#define T_TURRET 0xDC
#define T_LIZARD_LEFT 0xC8
#define T_LIZARD_RIGHT 0xCC
#define T_MARK 0xB8
#define T_MARK_END 0xBC
#define T_WALKER 0xD0
#define T_SMALL_LEFT 0xF0
#define T_SMALL_RIGHT 0xF4
#define T_BIG 0xF8
#define T_HOLES 0x50

#define HOLD_LINE 2800  /* the mid boss holds the world here */
#define END_LINE 208    /* the end boss's stretch */

/* An object's place, keeping its fraction (the original writes the word). */
static void set_x(enemy_t *a, int32_t x) { a->x = x * 65536 + (a->x & 0xFFFF); }
static void set_y(enemy_t *a, int32_t y) { a->y = y * 65536 + (a->y & 0xFFFF); }

/* The fire accumulator's carry ($5E: a rate added to it): true when it
 * wraps, the accumulator then a random number below 64. */
static bool carry(enemies_t *e, enemy_t *a, uint32_t rate_addr) {
  unsigned sum = (unsigned)a->fire_acc + en_rb(e, rate_addr);
  a->fire_acc = (uint8_t)sum;
  if (sum < 256) return false;
  a->fire_acc = (uint8_t)(en_rnd(e) & 63u);
  return true;
}

/* A shot's sprite through the level's table of pointers to pointers. */
static uint32_t shot_sprite(enemies_t *e, uint32_t index_addr) {
  return en_rl(e, en_rl(e, SHOTS + 4u * en_rw(e, index_addr)));
}

static void anim(enemies_t *e, enemy_t *a, uint32_t list) { en_anim_start(e, a, list); }

/* The object before `i` on the enemy list, or -1. */
static int8_t before(const enemies_t *e, int8_t i) {
  for (int8_t k = e->enemies; k >= 0; k = e->pool[k].next) {
    if (e->pool[k].next == i) return k;
  }
  return -1;
}

static void insert_after(enemies_t *e, int8_t prev, int8_t i) {
  e->pool[i].next = e->pool[prev].next;
  e->pool[prev].next = i;
}

/* Gone below the back limit's reach ($CEA + 208), its y the world line. */
static bool gone_world(enemy_t *a, const enemies_frame_t *f) {
  if ((a->y >> 16) <= f->back_limit + 208) return false;
  en_kill(a);
  return true;
}

/* The same, its y on the screen. */
static bool gone_screen(enemy_t *a, const enemies_frame_t *f) {
  if ((a->y >> 16) <= f->back_limit + 208 - f->line) return false;
  en_kill(a);
  return true;
}

static int octant(const enemy_t *a) { return ((a->dir + 16) >> 5) & 7; }

/* ---- the waves: the snake and the edge enemy ---- */

/* $4F244, $4F24E, $4F258: the path's step and fire, the sprite by its
 * direction. */
static void update_snake(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                         uint32_t sprites) {
  en_path_step(e, a);
  if (a->flags & F_DEAD) return;
  en_box(e, a);
  en_fire(e, a, f);
  a->sprite = en_rl(e, sprites + 4u * (uint32_t)octant(a));
  en_box(e, a);
}

/* $4F2D8: a path enemy whose animation, after its first update, is the
 * one for the screen edge it came in by. */
static void update_edge(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  en_update_path(e, a, f);
  if (a->flags & F_DEAD) return;
  int32_t x = (int16_t)(a->x >> 16), y = (int16_t)(a->y >> 16);
  uint32_t d;
  if (x < 0) {
    if (y < 0) {
      d = y < x ? 4 : 0;
    } else {
      d = 192 - y < x ? 12 : 0;
    }
  } else if (y < 0) {
    d = y < 320 - x ? 4 : 8;
  } else {
    d = y - 192 < x - 320 ? 8 : 12;
  }
  anim(e, a, en_rl(e, EDGE_ANIMS + d));
  a->role = R_PATH;
  en_box(e, a);
}

static uint8_t wave_role(uint32_t update) {
  switch (update) {
    case UPD_SNAKE_HEAD: return R_SNAKE_HEAD;
    case UPD_SNAKE_BODY: return R_SNAKE_BODY;
    case UPD_SNAKE_TAIL: return R_SNAKE_TAIL;
    case UPD_EDGE: return R_EDGE;
    default: return 0;
  }
}

/* ---- the turrets on tracks ($4F8F6, update $504B0) ---- */

static void spawn_turret(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  int d = en_rs(e, rec + 6) & 7;
  a->type = T_TURRET;
  a->role = R_TURRET;
  a->hit = H_SINGLE;
  a->v42 = (int16_t)d;
  a->x = (en_rs(e, rec + 8) - 8 + en_rs(e, TURRET_DX + 2u * (uint32_t)d)) * 65536;
  a->y = (en_rs(e, rec + 10) - 8 - f->line + en_rs(e, TURRET_DY + 2u * (uint32_t)d)) * 65536;
  anim(e, a, en_rl(e, TURRET_ANIMS + 4u * (uint32_t)d));
  a->hp = en_rs(e, TURRET_HP);
  a->worth = 100;
  en_box(e, a);
}

/* A map word at a world point, 0 off the map. */
static uint16_t map_at(const enemies_t *e, int32_t x, int32_t line) {
  if (x < 0 || line < 0) return 0;
  uint32_t c = (uint32_t)(line >> 4) * 20u + (uint32_t)(x >> 4);
  return c < e->map_rows * 20u ? e->map[c] : 0;
}

static void turn(enemies_t *e, enemy_t *a) { /* $505C6 */
  uint32_t d = (uint32_t)a->v42;
  set_x(a, (a->x >> 16) + en_rs(e, TURN_DX + 2u * d));
  set_y(a, (a->y >> 16) + en_rs(e, TURN_DY + 2u * d));
  a->v42 = en_rs(e, TURN_DIR + 2u * d);
  anim(e, a, en_rl(e, TURRET_ANIMS + 4u * (uint32_t)a->v42));
}

static void update_turret(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  en_animate(e, a);
  a->y += f->step * 65536;
  if (gone_screen(a, f)) return;
  uint32_t d = (uint32_t)a->v42 & 7u;
  int dx = en_rs(e, TRACK_DX + 2u * d);
  bool at_cell;
  if (dx != 0) {
    int32_t x = (a->x >> 16) + dx;
    set_x(a, x);
    at_cell = dx < 0 ? (x & 15) == 15 : (x & 15) == 0;
  } else {
    int dy = en_rs(e, TRACK_DY + 2u * d);
    int32_t y = (a->y >> 16) + dy;
    set_y(a, y);
    int32_t c = (y + f->line) & 15;
    at_cell = dy < 0 ? c == 15 : c == 0;
  }
  if (at_cell) { /* $5053C: a cell's edge: on, or turn */
    int32_t x = (int16_t)(a->x >> 16), y = (int16_t)(a->y >> 16);
    if (x == 0 || (uint32_t)x >= 320u) {
      turn(e, a);
    } else if (map_at(e, x, y + f->line)) { /* blocked: back and turn */
      set_x(a, x - en_rs(e, TRACK_DX + 2u * d));
      set_y(a, y - en_rs(e, TRACK_DY + 2u * d));
      turn(e, a);
    } else {
      int32_t c = (int32_t)((uint32_t)((y + f->line) >> 4) * 20u + (uint32_t)(x >> 4)) +
                  en_rs(e, TRACK_AHEAD + 2u * d) / 2;
      if (c < 0 || (uint32_t)c >= e->map_rows * 20u || e->map[c] == 0) turn(e, a);
    }
  }
  if (carry(e, a, TURRET_RATE)) {
    int32_t x = a->x >> 16, y = a->y >> 16;
    en_shot(e, shot_sprite(e, TURRET_SHOT), x, y,
            en_dir_to(f->ship_x - x, f->ship_y - y), en_rw(e, TURRET_SPEED));
  }
  en_box(e, a);
}

/* ---- the wall lizards ($500EA; $50690, $507B2, $5073C) ---- */

/* A start marker, eight parts (the head first, the tail last) and an end
 * marker, in that order on the enemy list; each part at its own place in
 * the line out of the wall, the head's reach times its rank. */
static void spawn_lizard(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  int8_t end = en_alloc(e);
  if (end < 0) return;
  en_push(&e->enemies, e, end);
  bool right = en_rw(e, rec + 12) != 0;
  int32_t x = en_rs(e, rec + 8) - 8 + (right ? 32 : 0);
  int32_t y = en_rs(e, rec + 10) - f->line;
  int8_t head = -1, prev = -1;
  for (int k = 7; k >= 0; k--) {
    int8_t i = en_alloc(e);
    if (i < 0) break;
    enemy_t *a = &e->pool[i];
    if (prev < 0) {
      en_push(&e->enemies, e, i);
      head = i;
    } else {
      insert_after(e, prev, i);
      e->pool[prev].chain = i;
    }
    a->type = right ? T_LIZARD_RIGHT : T_LIZARD_LEFT;
    a->sprite = right ? LIZARD_RIGHT : LIZARD_LEFT;
    if (k == 0) anim(e, a, right ? LIZARD_TAIL_RIGHT : LIZARD_TAIL_LEFT);
    a->role = k == 7 ? R_LIZARD_HEAD : k == 0 ? R_LIZARD_TAIL : R_LIZARD_BODY;
    a->hit = H_CHAIN;
    a->x = x * 65536;
    a->ox = (int16_t)x; /* its place at rest */
    a->y = y * 65536;
    a->hp = en_rs(e, LIZARD_HP);
    a->worth = 400;
    a->v48 = (int16_t)(right ? -(7 - k) : 7 - k);
    a->head = head;
    en_box(e, a);
    prev = i;
  }
  int8_t mark = en_alloc(e);
  if (mark < 0) return;
  en_push(&e->enemies, e, mark);
  e->pool[mark].type = T_MARK;
  e->pool[mark].role = R_MARK;
  e->pool[mark].flags = 0;
  en_box_off(&e->pool[mark]);
  enemy_t *z = &e->pool[end];
  z->type = T_MARK_END;
  z->role = R_MARK_END;
  z->flags = 0;
  z->head = mark;
  en_box_off(z);
}

static void update_lizard_head(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  if ((a->y >> 16) >= 232) {
    en_kill(a);
    return;
  }
  a->flags &= (uint8_t)~F_DRAW;
  if (a->v42 == 0) {
    if (a->v48 < 0) {
      a->v48++;
      en_box(e, a);
      return;
    }
    int32_t d = (a->y >> 16) - f->ship_y;
    if (d < 0) d = -d;
    if (d > 32) {
      en_box(e, a);
      return;
    }
    a->v42 = 2;
    a->v48 = (int16_t)((en_rnd(e) & 6u) + 10u);
  }
  a->flags |= F_DRAW;
  a->v40 = (int16_t)(a->v40 + a->v42);
  if (a->v40 == 0) {
    a->v42 = 0;
    a->v48 = -5;
  } else if (a->v40 == a->v48) {
    a->v42 = (int16_t)-a->v42;
  }
  en_box(e, a);
}

/* A body part or the tail: its rank times the head's reach out of the
 * wall; the tail's animation stepped as the head turns. */
static void update_lizard_part(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                               bool tail) {
  a->y += f->step * 65536;
  if ((a->y >> 16) >= 232) {
    en_kill(a);
    return;
  }
  const enemy_t *h = &e->pool[a->head];
  if (tail) {
    en_animate(e, a);
  } else {
    a->flags &= (uint8_t)~F_DRAW;
  }
  set_x(a, (int16_t)(a->v48 * h->v40 + a->ox));
  if (tail) {
    if ((h->v42 == 2 && h->v40 == 2) || (h->v48 > 0 && h->v48 == h->v40)) a->frames = 1;
  } else if (h->v42 != 0) {
    a->flags |= F_DRAW;
  }
  en_box(e, a);
}

/* $4020: with no part left between them, both markers go. */
static void update_mark_end(enemies_t *e, int8_t i) {
  int8_t mark = e->pool[i].head;
  if (e->pool[mark].next != i) return;
  en_free_now(e, &e->enemies, i);
  en_free_now(e, &e->enemies, mark);
}

/* ---- the walkers ($50308, update $50818) and their bolts ---- */

static void spawn_walker(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  a->type = T_WALKER;
  a->role = R_WALKER;
  a->hit = H_SINGLE;
  int32_t x = en_rs(e, rec + 8) - 8;
  a->x = x * 65536;
  a->y = (en_rs(e, rec + 10) + 8 - f->line) * 65536;
  bool left = x > 160;
  a->v42 = left ? -2 : 2;
  anim(e, a, left ? WALK_LEFT : WALK_RIGHT);
  a->hp = en_rs(e, WALKER_HP);
  a->flags |= F_BIG;
  a->worth = 300;
  en_box(e, a);
}

static void walker_turn(enemies_t *e, enemy_t *a, uint32_t list) {
  a->v42 = (int16_t)-a->v42;
  anim(e, a, list);
}

static void update_walker(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  if (gone_screen(a, f)) return;
  en_animate(e, a);
  if (a->v40 == 0) { /* walking */
    int32_t x = (a->x >> 16) + a->v42;
    set_x(a, x);
    if (x <= -32) {
      set_x(a, 0);
      walker_turn(e, a, WALK_RIGHT);
    } else if (x >= 352) {
      set_x(a, 320);
      walker_turn(e, a, WALK_LEFT);
    } else if (x == 136) {
      a->v40 = 1;
      anim(e, a, STOP_RIGHT);
    } else if (x == 184) {
      a->v40 = 1;
      anim(e, a, STOP_LEFT);
    }
  } else if (a->frames == 0) { /* stopped and done: back the way it came */
    a->v40 = 0;
    if ((a->x >> 16) <= 160) {
      set_x(a, 104);
      walker_turn(e, a, WALK_LEFT);
    } else {
      set_x(a, 216);
      walker_turn(e, a, WALK_RIGHT);
    }
  } else if (++a->v40 == 11) { /* a bolt, on the effects' list */
    int32_t x = (a->x >> 16) - 5, y = (a->y >> 16) + 4;
    uint32_t s = BOLT_LEFT;
    if (x > 160) {
      x += 10;
      s = BOLT_RIGHT;
    }
    int8_t k = en_alloc(e);
    if (k >= 0) {
      enemy_t *b = &e->pool[k];
      en_push(&e->fx, e, k);
      b->type = T_SHOT;
      b->role = R_BOLT;
      b->sprite = s;
      b->x = x * 65536;
      b->y = y * 65536;
      b->speed = en_rs(e, BOLT_SPEED);
      b->v42 = (a->x >> 16) < 160 ? 3 : 5;
      en_box_off(b);
    }
  }
  en_box(e, a);
}

/* $50996: an enemy's shot, kept off the screen's middle third. */
static void update_bolt(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                        enemies_events_t *ev) {
  en_update_shot(e, a, f, ev);
  if (a->flags & F_DEAD) return;
  int32_t x = a->x >> 16;
  if (x < 128) {
    a->v42 = 3;
    a->sprite = BOLT_LEFT;
  }
  if (x >= 192) {
    a->v42 = 5;
    a->sprite = BOLT_RIGHT;
  }
}

/* ---- the wall objects in the map ($50420, $503A6) ---- */

/* A scenery object drawn in the map on the enemy list ($3CCC). */
static enemy_t *in_map(enemies_t *e, uint32_t rec, uint16_t type, uint8_t role) {
  int8_t i = en_alloc(e);
  if (i < 0) return NULL;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  int32_t x = en_rs(e, rec + 8) - 8, y = en_rs(e, rec + 10) - 8;
  a->type = type;
  a->role = role;
  a->flags = 0;
  a->x = x * 65536;
  a->y = y * 65536; /* the world line */
  a->cell = (int16_t)((y >> 4) * 20 + (x >> 4));
  en_box_off(a);
  return a;
}

/* `n` words from `words` into the map at `a`'s cell + `at`. */
static void cells(enemies_t *e, const enemy_t *a, int at, uint32_t words, int n) {
  for (int k = 0; k < n; k++) {
    uint32_t c = (uint32_t)(a->cell + at + k);
    if (c < e->map_rows * 20u) e->map[c] = en_rw(e, words + 2u * (uint32_t)k);
  }
}

static void clear_cells(enemies_t *e, const enemy_t *a, int at, int n) {
  for (int k = 0; k < n; k++) {
    uint32_t c = (uint32_t)(a->cell + at + k);
    if (c < e->map_rows * 20u) e->map[c] = 0;
  }
}

static void spawn_small(enemies_t *e, uint32_t rec) {
  bool right = en_rw(e, rec + 12) != 0;
  enemy_t *a = in_map(e, rec, right ? T_SMALL_RIGHT : T_SMALL_LEFT, R_WALL_SMALL);
  if (!a) return;
  a->hit = H_WALL;
  a->cols = a->rows = 1;
  a->hp = en_rs(e, SMALL_HP);
  a->worth = 100;
  a->tiles = right ? SMALL_RIGHT : SMALL_LEFT;
  a->wreck = right ? SMALL_WRECK_RIGHT : SMALL_WRECK_LEFT;
  cells(e, a, 0, a->tiles, 1);
}

static void update_small(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (gone_world(a, f)) return;
  int32_t x = a->x >> 16, y = a->y >> 16;
  a->box[0] = (int16_t)(x + 2);
  a->box[2] = (int16_t)(x + 14);
  a->box[1] = (int16_t)(y - f->line + 2);
  a->box[3] = (int16_t)(a->box[1] + 12);
  if (a->v40 == 0) {
    if (carry(e, a, SMALL_RATE)) a->v40 = 1;
    return;
  }
  cells(e, a, 0, a->tiles + (uint32_t)(a->v40 & ~1), 1);
  if (++a->v40 == 8) {
    bool down = a->type != T_SMALL_LEFT;
    en_shot(e, SMALL_SHOT, x + 8, y - f->line + (down ? 10 : 0), down ? 4 : 0,
            en_rw(e, SMALL_SPEED));
    return;
  }
  if (a->v40 == 14) a->v40 = 0;
}

static void spawn_big(enemies_t *e, uint32_t rec) {
  enemy_t *a = in_map(e, rec, T_BIG, R_WALL_BIG);
  if (!a) return;
  a->hit = H_BIG_WALL;
  a->hp = en_rs(e, BIG_HP);
  for (int r = 0; r < 6; r++) cells(e, a, r * 20, BIG_CELLS + 8u * (uint32_t)r, 4);
}

/* $50A6A: its mouth opening and closing, firing as it is open. */
static void update_big(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (gone_world(a, f)) return;
  int32_t x = a->x >> 16, y = a->y >> 16;
  a->box[0] = (int16_t)(x + 16);
  a->box[2] = (int16_t)(x + 47);
  a->box[1] = (int16_t)(y - f->line + 64);
  a->box[3] = (int16_t)(a->box[1] + 26);
  if (a->v40 == 0 && (uint8_t)en_rnd(e) >= en_rb(e, BIG_CHANCE)) return;
  uint32_t m = BIG_MOUTH + 4u * (uint32_t)(a->v40 & ~1);
  cells(e, a, 81, m, 2);
  cells(e, a, 101, m + 4, 2);
  a->v40++;
  if (a->v40 == 18) {
    a->v40 = 0;
    return;
  }
  if (a->v40 < 4) return;
  if (a->v40 <= 11) { /* open: it may fire */
    if (!carry(e, a, BIG_RATE)) return;
    int s = a->v40 & ~1;
    int dir = s == 6 ? 3 : s == 8 ? 5 : 4;
    en_shot(e, shot_sprite(e, BIG_SHOT), x + 32, y - f->line + 88, dir,
            en_rw(e, BIG_SPEED));
    return;
  }
  if (a->v40 == 12 && (uint8_t)en_rnd(e) < en_rb(e, BIG_AGAIN)) a->v40 = 4;
}

/* An explosion at (x, y) on the screen. */
static void boom(enemies_t *e, int32_t x, int32_t y, bool big, enemies_events_t *ev) {
  enemy_t c;
  memset(&c, 0, sizeof(c));
  c.x = x * 65536;
  c.y = y * 65536;
  en_explode(e, &c, big, ev);
}

/* $50BEA: its bottom destroyed, the top goes on ($50CF8, $50F54). */
static void hit_big(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) {
    a->flags |= F_FLASH | F_DRAW;
    return;
  }
  int32_t x = a->x >> 16, y = a->y >> 16;
  boom(e, x + 32, y - e->line + 64, true, ev);
  clear_cells(e, a, 40, 4);
  clear_cells(e, a, 60, 4);
  clear_cells(e, a, 80, 4);
  clear_cells(e, a, 101, 2);
  a->role = R_WALL_TOP;
  a->hit = H_WALL_TOP;
  a->hp = en_rs(e, TOP_HP);
  en_box_off(a);
  a->v40 = 0;
  ev->score += 500;
}

/* $50CF8: its top opening, a burst of eight at the fourth step. */
static void update_top(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (gone_world(a, f)) return;
  int32_t x = a->x >> 16, y = a->y >> 16;
  a->box[0] = (int16_t)(x + 16);
  a->box[2] = (int16_t)(x + 47);
  a->box[1] = (int16_t)(y + 4 - f->line);
  a->box[3] = (int16_t)(a->box[1] + 23);
  if (a->v40 == 0 && !carry(e, a, TOP_RATE)) return;
  uint32_t m = TOP_STEPS + 4u * (uint32_t)(a->v40 & ~1);
  cells(e, a, 1, m, 2);
  cells(e, a, 21, m + 4, 2);
  a->v40++;
  if (a->v40 == 10) {
    a->v40 = 0;
    return;
  }
  if (a->v40 != 4) return;
  for (int d = 7; d >= 0; d--) {
    int8_t k = en_shot(e, 0, x + 32, y - f->line + 24, d, en_rw(e, TOP_SPEED));
    if (k >= 0) anim(e, &e->pool[k], en_rl(e, TOP_BURST + 4u * (uint32_t)(7 - d)));
  }
}

static void hit_top(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) {
    a->flags |= F_FLASH | F_DRAW;
    return;
  }
  int32_t x = a->x >> 16, y = a->y >> 16;
  boom(e, x + 32, y - e->line + 16, true, ev);
  clear_cells(e, a, 0, 4);
  clear_cells(e, a, 20, 4);
  ev->score += 500;
  en_kill(a);
}

/* A block of the level's map words, drawn flashing at (x, y). */
static void flash_block(enemies_t *e, enemies_block_fn block, void *ctx,
                        uint32_t words, int cols, int rows, int32_t x, int32_t y) {
  uint16_t w[12];
  int n = cols * rows;
  if (n > 12) return;
  for (int k = 0; k < n; k++) w[k] = en_rw(e, words + 2u * (uint32_t)k);
  block(ctx, w, cols, rows, x, y, true);
}

/* ---- the mid boss ($4FFEC) ---- */

static void spawn_mid(enemies_t *e) {
  int8_t head = -1, prev = -1;
  for (uint32_t p = MID_PARTS; en_rl(e, p) != 0; p += 16) {
    int8_t i = en_alloc(e);
    if (i < 0) break;
    enemy_t *a = &e->pool[i];
    en_append(&e->enemies, e, i);
    if (head < 0) head = i;
    if (prev >= 0) e->pool[prev].chain = i;
    uint32_t update = en_rl(e, p);
    a->type = T_BOSS_B;
    a->role = update == UPD_BODY      ? R_BODY
              : update == UPD_EYE     ? R_EYE
              : update == UPD_ARM     ? R_ARM
              : update == UPD_SEGMENT ? R_SEGMENT
              : update == UPD_CLAW    ? R_CLAW
                                      : R_FOLLOW_PART;
    a->hit = en_rl(e, p + 4) == HIT_EYE ? H_EYE : H_NONE;
    anim(e, a, en_rl(e, p + 8));
    a->ox = en_rs(e, p + 12);
    a->oy = en_rs(e, p + 14);
    a->rate = (int32_t)((uint32_t)(uint16_t)a->ox << 16 | (uint16_t)a->oy);
    a->worth = 1000;
    a->flags |= F_BIG;
    a->start = MID_PATH;
    a->x = (en_rs(e, MID_PATH + 2) - 32) * 65536;
    a->y = (en_rs(e, MID_PATH + 4) - 40) * 65536;
    a->pc = MID_PATH + 6;
    a->speed = en_rs(e, MID_SPEED);
    a->head = head;
    a->v48 = (int16_t)(p + 16 - MID_PARTS);
    en_box(e, a);
    prev = i;
  }
  e->vars[V_BODY_SEEN] = 1;
}

/* $4FCF0: where it sits from the body. */
static void follow(enemies_t *e, enemy_t *a) {
  en_animate(e, a);
  const enemy_t *h = &e->pool[a->head];
  set_x(a, (h->x >> 16) + a->ox);
  set_y(a, (h->y >> 16) + a->oy);
  en_box(e, a);
}

/* $4FC88: on its path, aiming at the ship. */
static void update_body(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  e->vars[V_BODY_SEEN] = 1;
  en_update_path(e, a, f);
  if (a->flags & F_DEAD) return;
  if (!carry(e, a, BODY_RATE)) return;
  int32_t x = a->x >> 16, y = a->y >> 16;
  int8_t k = en_shot(e, 0, x + 16, y + 60, en_dir_to(f->ship_x - x, f->ship_y - y),
                     en_rw(e, BODY_SPEED));
  if (k >= 0) anim(e, &e->pool[k], BODY_SHOT);
}

/* $4FD38: open to hits while its blink runs; destroyed, its dead sprite. */
static void update_eye(enemies_t *e, enemy_t *a) {
  follow(e, a);
  bool first = a->v48 == 64;
  if (e->vars[first ? V_EYE_A : V_EYE_B] <= 0) {
    a->sprite = first ? EYE_DEAD_A : EYE_DEAD_B;
    a->frames = 0;
    a->anim = 0;
    a->box[0] = 1000;
    a->hit = H_NONE;
    return;
  }
  if (a->frames != 0) return; /* open: the box follow() set */
  if (carry(e, a, BLINK_RATE)) anim(e, a, EYE_BLINK);
  a->box[0] = a->box[2] = 1000;
}

/* $4FDC0: the whole boss flashes; an eye's last hit an explosion, both
 * eyes' the boss destroyed and its payout. */
static void hit_eye(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  for (int8_t k = a->head; k >= 0; k = e->pool[k].chain) e->pool[k].flags |= F_FLASH;
  int16_t *hp = &e->vars[a->v48 == 64 ? V_EYE_A : V_EYE_B];
  uint16_t before = (uint16_t)*hp;
  *hp = (int16_t)(*hp - damage);
  if (before > (uint16_t)damage) return;
  a->box[0] = 1000;
  a->hit = H_NONE;
  en_explode(e, a, false, ev);
  if (--e->vars[V_EYES] != 0) return;
  e->pool[a->head].hp = 0;
  a->hit = H_CHAIN; /* $6160: every part, the body destroyed */
  en_hit(e, a, 1, ev);
  a->hit = H_NONE;
  en_payout(e, 5);
  e->mid_line = 0;
}

/* $4FE52: now and then it swings its chain out at the ship and back. */
static void update_arm(enemies_t *e, int8_t i, const enemies_frame_t *f,
                       enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  follow(e, a);
  int8_t n = a->next;
  if (n < 0) return;
  enemy_t *s = &e->pool[n];
  if (a->v40 == 0) {
    if ((uint8_t)en_rnd(e) >= 3) {
      s->speed = 0;
      s->dir = 64;
      return;
    }
    en_sound(ev, 26, 1);
    a->v40 = 1;
    a->v42 = 1;
    int32_t x = a->x >> 16, y = a->y >> 16;
    int d = en_dir_to(f->ship_x - x, f->ship_y - y);
    s->frac = 0;
    s->dir = (uint8_t)(d * 32 - 96);
    s->rate = (int32_t)(((en_rnd(e) & 31u) + 32u) << 10);
    s->acc = 0;
    s->speed = 1;
    if (a->v48 == 96) {
      s->dir = (uint8_t)(s->dir + 64);
      s->rate = -s->rate;
    }
    return;
  }
  a->v40 = (int16_t)(a->v40 + a->v42);
  if (a->v40 == 14) a->v42 = (int16_t)-a->v42;
  s->speed = a->v40;
}

/* $4FF12: from the part before it, its arc's steps; the arc passed on. */
static void update_segment(enemies_t *e, int8_t i) {
  enemy_t *a = &e->pool[i];
  int8_t p = before(e, i);
  if (p >= 0) {
    a->x = e->pool[p].x;
    a->y = e->pool[p].y;
  }
  a->count = 1000;
  uint8_t dir = a->dir;
  uint16_t frac = a->frac;
  int32_t rate = a->rate;
  int16_t acc = a->acc;
  en_path_step(e, a);
  if (!(a->flags & F_DEAD)) en_box(e, a);
  if (a->next >= 0) {
    enemy_t *n = &e->pool[a->next];
    n->dir = a->dir;
    n->frac = a->frac;
    n->rate = a->rate;
    n->acc = a->acc;
    n->speed = a->speed;
  }
  a->dir = dir;
  a->frac = frac;
  a->rate = rate;
  a->acc = acc;
}

/* $4FF62: the chain's end, its sprite by direction, snapping at full
 * reach. */
static void update_claw(enemies_t *e, int8_t i, enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  int8_t p = before(e, i);
  if (p >= 0) {
    a->x = e->pool[p].x;
    a->y = e->pool[p].y;
  }
  a->count = 1000;
  en_path_step(e, a);
  if (a->flags & F_DEAD) return;
  en_box(e, a);
  uint32_t k = (uint32_t)((a->dir >> 5) & 7) * 2u + ((e->frame >> 2) & 1u);
  a->sprite = en_rl(e, CLAW + 4u * k);
  a->frames = 0;
  a->anim = 0;
  if (a->speed == 14) en_sound(ev, 8, 2);
  en_box(e, a);
}

/* ---- the end boss ($4F510) ---- */

static uint32_t worm_rnd(enemies_t *e) { /* $4F4F4 */
  uint32_t r = e->worm_rng[0];
  r = r >> 5 | r << 27;
  e->worm_rng[0] = r;
  return r;
}

/* Its eleven parts on one of the level's paths, each on the enemy list's
 * head, each a delay behind the one before. */
static void spawn_worm(enemies_t *e, int32_t line, int32_t step) {
  uint32_t n;
  if (e->vars[V_SPAWNS] != 1) {
    n = (uint32_t)e->vars[V_SPAWNS]++;
  } else {
    do {
      n = worm_rnd(e) & 7u;
    } while (n < 1 || n == 7);
  }
  uint32_t path = en_rl(e, WORM_PATHS + 4u * n);
  int32_t x = en_rs(e, path + 2), y = en_rs(e, path + 4) - line - step;
  int parts = en_rw(e, WORM_PARTS) + 1;
  int16_t delay = 0;
  int8_t head = -1, prev = -1;
  for (int k = 0; k < parts; k++) {
    uint32_t rec = WORM_PARTS + 2u + 10u * (uint32_t)k;
    int8_t i = en_alloc(e);
    if (i < 0) break;
    enemy_t *a = &e->pool[i];
    en_push(&e->enemies, e, i);
    if (head < 0) head = i;
    if (prev >= 0) e->pool[prev].chain = i;
    uint32_t update = en_rl(e, rec + 2);
    a->type = T_BOSS_A;
    a->role = update == UPD_WORM_HEAD   ? R_WORM_HEAD
              : update == UPD_WORM_NECK ? R_WORM_NECK
              : update == UPD_WORM_TAIL ? R_WORM_TAIL
                                        : R_WORM_BODY;
    a->hit = k == 0 ? H_WORM : H_SHIELD;
    a->count = delay;
    delay = (int16_t)(delay - en_rs(e, rec));
    a->sprite = en_rl(e, rec + 6);
    a->worth = 2000;
    a->start = a->pc = path;
    a->dir = 192;
    a->x = x * 65536;
    a->y = y * 65536;
    a->speed = en_rs(e, WORM_SPEED);
    a->v48 = 0; /* its slot */
    a->head = head;
    en_box(e, a);
    prev = i;
  }
}

/* A part after the head: on the path with the world, its sprite by its
 * direction; the tail says the worm is out. */
static void update_worm_part(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                             uint32_t sprites, bool tail) {
  a->y += f->step * 65536;
  if (tail) e->vars[V_TAIL] = 1;
  en_path_step(e, a);
  if (a->flags & F_DEAD) return;
  en_box(e, a);
  en_fire(e, a, f);
  a->sprite = en_rl(e, sprites + 4u * (uint32_t)octant(a));
  en_box(e, a);
}

/* $4F620: its animation by its direction, a burst of eight now and then. */
static void update_worm_head(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  int was = octant(a);
  en_update_path(e, a, f);
  if (a->flags & F_DEAD) return;
  int now = octant(a);
  if (now != was) anim(e, a, en_rl(e, WORM_HEAD_ANIMS + 4u * (uint32_t)now));
  unsigned sum = (unsigned)a->fire_acc + en_rb(e, WORM_RATE);
  a->fire_acc = (uint8_t)sum;
  if (sum >= 256) {
    int32_t x = a->x >> 16, y = a->y >> 16;
    for (int d = 7; d >= 0; d--) {
      int8_t k = en_shot(e, 0, x, y, d, en_rw(e, WORM_SHOT_SPEED));
      if (k >= 0) anim(e, &e->pool[k], WORM_SHOT);
    }
  }
  en_box(e, a);
}

/* $4F842: the shared hit points; at 0 every part gone, twenty explosions
 * over the screen, the enemies cleared. */
static void hit_worm(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  for (int8_t k = a->head; k >= 0; k = e->pool[k].chain) e->pool[k].flags |= F_FLASH;
  uint16_t before = (uint16_t)e->vars[V_HP];
  e->vars[V_HP] = (int16_t)(e->vars[V_HP] - damage);
  if (before > (uint16_t)damage) return;
  e->vars[V_DEAD] = 1;
  e->vars[V_TAIL] = 0;
  for (int8_t k = a->head; k >= 0;) {
    int8_t n = e->pool[k].chain;
    en_kill(&e->pool[k]);
    k = n;
  }
  enemies_burst(e, 0, 0, 320, 192, 20, true, ev);
  for (int8_t k = e->enemies; k >= 0; k = e->pool[k].next) en_kill(&e->pool[k]);
}

/* $51160: the end boss's holes, over the playfield while it is out. */
static bool draw_holes(enemies_t *e, enemy_t *a, int32_t line, bool flash,
                       enemies_draw_fn draw, enemies_block_fn block, void *ctx) {
  (void)draw;
  (void)flash;
  if (a->role == R_WALL_BIG || a->role == R_WALL_TOP) { /* a hit's flash */
    a->flags &= (uint8_t)~F_DRAW;
    if (!flash) return true;
    int32_t x = a->x >> 16, y = (a->y >> 16) - line;
    if (a->role == R_WALL_BIG) {
      flash_block(e, block, ctx, BIG_FLASH, 4, 3, ((x >> 1) & ~3) * 2, y + 32);
      flash_block(e, block, ctx, BIG_MOUTH + 4u * (uint32_t)(a->v40 & ~1), 2, 2,
                  (((x + 16) >> 1) & ~3) * 2, y + 64);
    } else {
      flash_block(e, block, ctx, TOP_STEPS, 2, 2, (((x + 16) >> 1) & ~3) * 2, y);
    }
    return true;
  }
  if (a->role != R_HOLES) return false;
  if (!e->vars[V_TAIL] && !e->vars[V_TAIL2]) return true;
  for (uint32_t h = HOLES; en_rl(e, h) != 0; h += 12) {
    int32_t y = en_rs(e, h + 8) - line;
    if (y >= 192 || y <= -48) continue;
    int cols = en_rs(e, h + 4) + 1, rows = en_rs(e, h + 6) + 1;
    uint16_t w[12];
    if (cols * rows > 12) continue;
    for (int k = 0; k < cols * rows; k++) w[k] = en_rw(e, en_rl(e, h) + 2u * (uint32_t)k);
    block(ctx, w, cols, rows, en_rs(e, h + 10) * 2, y, false);
  }
  return true;
}

/* ---- the hooks ---- */

/* $511EE: the holes' overlay, the end boss's generator and hit points, the
 * mid boss's eyes, the world held at 2800 for the mid boss, the enemies'
 * hit points twice and three times the round's. */
static void start(enemies_t *e) {
  e->mid_line = HOLD_LINE;
  int8_t i = en_alloc(e);
  if (i >= 0) {
    enemy_t *a = &e->pool[i];
    en_append(&e->cells, e, i);
    a->type = T_HOLES;
    a->role = R_HOLES;
    a->flags = F_DRAW | F_KEEP;
    en_box_off(a);
  }
  e->wave_role = wave_role;
  e->lowest = HOLD_LINE;
  e->worm_rng[0] = 0xE496AA8Au;
  e->worm_rng[1] = 0x5274B949u;
  e->vars[V_DEAD] = 0;
  e->vars[V_TAIL] = e->vars[V_TAIL2] = 0;
  e->vars[V_SPAWNS] = 0;
  e->vars[V_HP] = en_rs(e, WORM_HP);
  e->vars[V_BODY_SEEN] = 0;
  e->vars[V_EYES] = 2;
  e->vars[V_EYE_A] = e->vars[V_EYE_B] = en_rs(e, EYE_HP);
  int16_t loop = e->hp_small;
  e->hp_small = (int16_t)(loop * 2);
  e->hp_big = (int16_t)(loop * 3);
}

/* $4F8D0: kind 1 the lizards, 2 a walker, 3 the mid boss, 4 and 5 wall
 * objects, 6 a turret. */
static void scenery(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                    int kind, enemies_events_t *ev) {
  (void)ev;
  switch (kind) {
    case 1: spawn_lizard(e, f, rec); break;
    case 2: spawn_walker(e, f, rec); break;
    case 3: spawn_mid(e); break;
    case 4: spawn_small(e, rec); break;
    case 5: spawn_big(e, rec); break;
    case 6: spawn_turret(e, f, rec); break;
    default: break;
  }
}

/* $4F3DE: the world held at 2800 while the mid boss lives (the backdrop
 * and the stars going on), the back limits, the end boss's stretch. */
static int32_t frame(enemies_t *e, int32_t line, int32_t step, int32_t ship_y,
                     int32_t *back_limit) {
  (void)ship_y;
  if (e->lowest == HOLD_LINE && line == HOLD_LINE) {
    *back_limit = HOLD_LINE;
    e->drift = (int16_t)step;
    if (e->vars[V_BODY_SEEN]) {
      e->vars[V_BODY_SEEN] = 0;
    } else {
      e->lowest = 0;
    }
  }
  if (line <= END_LINE) {
    if (*back_limit < 32) *back_limit = 32;
    if (!e->vars[V_DEAD] && !e->vars[V_TAIL]) spawn_worm(e, line, step);
    e->vars[V_TAIL] = e->vars[V_TAIL2] = 0;
    if (e->vars[V_DEAD] && !e->won) {
      en_payout(e, 10);
      e->won = true;
    }
  } else if (*back_limit < 2608) {
    *back_limit = 2608;
  }
  return -1;
}

static void update(enemies_t *e, int8_t i, const enemies_frame_t *f,
                   enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  switch (a->role) {
    case R_SNAKE_HEAD: update_snake(e, a, f, SNAKE_HEAD); break;
    case R_SNAKE_BODY: update_snake(e, a, f, SNAKE_BODY); break;
    case R_SNAKE_TAIL: update_snake(e, a, f, SNAKE_TAIL); break;
    case R_EDGE: update_edge(e, a, f); break;
    case R_TURRET: update_turret(e, a, f); break;
    case R_LIZARD_HEAD: update_lizard_head(e, a, f); break;
    case R_LIZARD_BODY: update_lizard_part(e, a, f, false); break;
    case R_LIZARD_TAIL: update_lizard_part(e, a, f, true); break;
    case R_MARK_END: update_mark_end(e, i); break;
    case R_WALKER: update_walker(e, a, f); break;
    case R_WALL_SMALL: update_small(e, a, f); break;
    case R_WALL_BIG: update_big(e, a, f); break;
    case R_WALL_TOP: update_top(e, a, f); break;
    case R_BODY: update_body(e, a, f); break;
    case R_FOLLOW_PART: follow(e, a); break;
    case R_EYE: update_eye(e, a); break;
    case R_ARM: update_arm(e, i, f, ev); break;
    case R_SEGMENT: update_segment(e, i); break;
    case R_CLAW: update_claw(e, i, ev); break;
    case R_WORM_HEAD: update_worm_head(e, a, f); break;
    case R_WORM_NECK: update_worm_part(e, a, f, WORM_NECK, false); break;
    case R_WORM_BODY: update_worm_part(e, a, f, WORM_BODY, false); break;
    case R_WORM_TAIL: update_worm_part(e, a, f, WORM_TAIL, true); break;
    default: break;
  }
}

static void update_fx(enemies_t *e, int8_t i, const enemies_frame_t *f,
                      enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  if (a->role == R_BOLT) update_bolt(e, a, f, ev);
}

static void hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  switch (a->hit) {
    case H_BIG_WALL: hit_big(e, a, damage, ev); break;
    case H_WALL_TOP: hit_top(e, a, damage, ev); break;
    case H_EYE: hit_eye(e, a, damage, ev); break;
    case H_WORM: hit_worm(e, a, damage, ev); break;
    default: break;
  }
}

const enemies_level_t enemies_level3 = {start, scenery, frame, update, update_fx,
                                         NULL, hit, draw_holes, NULL};
