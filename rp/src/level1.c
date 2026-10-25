/**
 * File: level1.c
 * Description: Level 1's own code (enemies_level.h), ported from the
 *              original's at $4F03C-$51000 and run on the level's tables in
 *              the store.
 *
 * - Its scenery ($4FB42): wall objects drawn in the map ($4F4CE, $4F7A2,
 *   $4F602) and wall crawlers ($4F8DC); their numbers in the level's block
 *   at $4F03C.
 * - Its frame code ($4FA06): the mid shop's line and the jump past it;
 *   between two world lines ($4FA3C, $4FA44) the world may go back to the
 *   lower, five worm slots ($5002C) each send a worm out of one of 16 holes
 *   ($4FF6C) chosen by the slot's own generator ($D5C), and the holes' cells
 *   animate ($4FAA2). A worm ($50050) is a start marker ($B8, the original's
 *   box over the chain), eleven parts (update $502C2: where the object ahead
 *   was last frame; hit $4FE2E, $4FE46: pieces $5025A), its head on a path
 *   ($503B4, never shown) and an end marker ($BC, $4020); the parts follow
 *   the head into the end hole.
 * - Its end boss (made at level start, $508A8; update $505AA, draw $5081A,
 *   hit $503DA): asleep until the world line reaches 448, then eight turrets
 *   ($50478) chained along an arc around it, an opening cycle, its weak
 *   point the eye; destroyed: its payout ($2996), 20 explosions ($4A98), the
 *   enemies cleared ($707E), the level won ($A07).
 */

#include <string.h>

#include "enemies_level.h"

/* Its roles and hits. */
enum { R_WALL1 = R_LEVEL, R_WALL2, R_WALL3, R_CRAWLER, R_BOSS, R_TURRET,
       R_WORM_HEAD, R_WORM_PART, R_WORM_END, R_WORM_MARK, R_WORM_PIECE };
enum { H_BOSS = H_LEVEL, H_WORM };

#define L1_SHOTS 0x67BD2u
#define L1_RATE1 0x4F043u /* bytes */
#define L1_RATE3 0x4F049u
#define L1_RATE2 0x4F04Fu
#define L1_SHOT1 0x4F044u /* words: shot sprite, speed */
#define L1_SPEED1 0x4F046u
#define L1_SHOT3 0x4F04Au
#define L1_SPEED3 0x4F04Cu
#define L1_SHOT2 0x4F050u
#define L1_SPEED2 0x4F052u
#define L1_SHOT4 0x4F054u
#define L1_SPEED4 0x4F056u
#define L1_HP1 0x4F05Au
#define L1_HP2 0x4F05Cu
#define L1_HP3 0x4F05Eu
#define L1_HP4 0x4F060u
#define L1_BOSS_HP 0x4F058u
#define L1_BOSS_BLOCK 0x509EEu /* 6 x 7 map words */
#define L1_BOSS_ROWS 7
#define L1_BOSS_MOUTH 0x507FCu /* 4 pointers to 8 words: rows 5-6, cols 1-4 */
#define L1_BOSS_EYES 0x50866u  /* 4 sprite pointers */
#define L1_TURRET 0x5405Au
#define L1_TURRET_AIM 0x5058Au /* 8 sprite pointers, by direction */
#define L1_TURRET_SHOT 0x4F4B0u
#define T_BOSS 0x50
#define T_TURRET 0xFC
#define T_TURRET_AIMS 0x100
#define BOSS_WAKE 448
#define BOSS_OPEN 11
#define L1_SHOP_AT 0x4FA18u     /* words: the mid shop's line, then the */
#define L1_SHOP_JUMP 0x4FA26u   /* line after it ($4FA06) */
#define L1_WORMS_FROM 0x4FA3Cu  /* words: the stretch's lines ($4FA06) */
#define L1_WORMS_TO 0x4FA44u
#define L1_HOLE_CELLS 0x4FAA2u  /* 16 x (a map offset, 4 tile words) */
#define L1_HOLES 0x4FF6Cu       /* 16 x (x, y, hole, end hole, path) */
#define L1_HOLE_OPEN 0x50090u   /* word: a hole's frames when opened */
#define L1_WORM_PARTS 0x4FEE6u  /* parts - 1, then (type, hit, sprite, worth) */
#define L1_WORM_SPEED 0x4F062u
#define L1_WORM_SEEDS 0x50954u  /* 5 x movel #seed,abs.w: 8 bytes apart */
#define L1_WORM_SPRITES 0x50374u /* 8 sprites by direction: the first part */
#define L1_WORM_BODY 0x50394u    /* the others ($110) */
#define T_WORM_BODY 0x110
#define T_WORM_HEAD 0x10C
#define T_WORM_MARK 0xB8 /* the chain's start: the original's box over it */
#define T_WORM_END 0xBC

/* A wall object's shot sprite: level 1's table of pointers to pointers. */
static uint32_t shot_sprite(const enemies_t *e, uint32_t index_addr) {
  return en_rl(e, en_rl(e, L1_SHOTS + 4u * en_rw(e, index_addr)));
}

/* $503DA, the eye's last hit. */
static void boss_destroyed(enemies_t *e, enemy_t *a, enemies_events_t *ev) {
  e->boss_awake = false;
  en_kill(a);
  en_payout(e, 9); /* $2996 */
  for (int k = 0; k < 20; k++) { /* $4A98 */
    int8_t i = en_alloc(e);
    if (i < 0) break;
    enemy_t *b = &e->pool[i];
    b->type = T_EXPLOSION;
    b->role = R_EXPLOSION;
    b->x = en_rnd_mod(e, 320) * 65536;
    b->y = en_rnd_mod(e, 192) * 65536;
    en_anim_start(e, b, BOOM_BIG_ANIM);
    en_push(&e->fx, e, i);
  }
  en_sound(ev, 15, 2);
  en_sound(ev, 15, 1);
  en_sound(ev, 15, 0);
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) en_kill(&e->pool[i]);
  e->won = true;
}

/* Level 1's wall objects ($4F4CE, $4F7A2, $4F602): their 2 x 2 cells. */
static void wall_cells(enemies_t *e, const enemy_t *a, uint32_t frame,
                       bool both_rows) {
  for (int r = 0; r < (both_rows ? 2 : 1); r++) {
    for (int k = 0; k < 2; k++) {
      int cell = a->cell + r * 20 + k;
      if (cell >= 0 && (uint32_t)cell < e->map_rows * 20u) {
        e->map[cell] = en_rw(e, frame + 2u * (uint32_t)(r * 2 + k));
      }
    }
  }
}

static bool wall_gone(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if ((a->y >> 16) > f->back_limit + 208) {
    en_kill(a);
    return true;
  }
  (void)e;
  return false;
}

static void wall_box(enemy_t *a, const enemies_frame_t *f, int x0, int y0,
                     int w, int h) {
  int32_t x = a->x >> 16, y = (a->y >> 16) - f->line;
  a->box[0] = (int16_t)(x + x0);
  a->box[2] = (int16_t)(x + x0 + w);
  a->box[1] = (int16_t)(y + y0);
  a->box[3] = (int16_t)(y + y0 + h);
}

static bool carry(enemies_t *e, enemy_t *a, uint32_t rate_addr) {
  unsigned sum = (unsigned)a->fire_acc + en_rb(e, rate_addr);
  a->fire_acc = (uint8_t)sum;
  if (sum < 256) return false;
  a->fire_acc = (uint8_t)(en_rnd(e) & 63u);
  return true;
}

static void wall_fire(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                      bool left, int dx, int dy, uint32_t shot_addr,
                      uint32_t speed_addr) {
  int32_t x = (a->x >> 16) + dx, y = (a->y >> 16) - f->line + dy;
  if (!left) x -= dx;
  en_shot(e, shot_sprite(e, shot_addr), x, y, left ? 2 : 6, en_rw(e, speed_addr));
}

static void update_wall1(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (wall_gone(e, a, f)) return;
  wall_box(a, f, 4, 4, 24, 24);
  wall_cells(e, a, a->tiles + 8u * (uint32_t)a->v40, true);
  a->v40 = (int16_t)((a->v40 + 1) & 3);
  if (carry(e, a, L1_RATE1)) {
    wall_fire(e, a, f, a->type == 0xE4, 16, 14, L1_SHOT1, L1_SPEED1);
  }
}

static void update_wall2(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (wall_gone(e, a, f)) return;
  wall_box(a, f, 10, 4, 18, 24);
  wall_cells(e, a, a->tiles + 2u * (uint32_t)(a->v40 & ~3), true);
  a->v40 = (int16_t)((a->v40 + 1) & 15);
  if (carry(e, a, L1_RATE2)) {
    wall_fire(e, a, f, a->type == 0xEC, 32, 14, L1_SHOT2, L1_SPEED2);
  }
}

static void update_wall3(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (wall_gone(e, a, f)) return;
  wall_box(a, f, 8, 8, 16, 16);
  wall_cells(e, a, a->tiles + 2u * (uint32_t)(a->v40 & ~1), false);
  if (a->v40 == 0 && !carry(e, a, L1_RATE3)) return;
  if (++a->v40 == 40) {
    a->v40 = 0;
    return;
  }
  if (a->v40 == 24) {
    wall_fire(e, a, f, a->type == 0xDC, 26, 2, L1_SHOT3, L1_SPEED3);
  }
}

static void update_crawler(enemies_t *e, enemy_t *a,
                           const enemies_frame_t *f) { /* $4F8DC */
  if (a->y >> 16 > f->back_limit + 208 - f->line) {
    en_kill(a);
    return;
  }
  if (!en_animate(e, a)) {
    en_kill(a);
    return;
  }
  a->y += f->step * 65536;
  if (a->v40 == 0) return;
  int32_t d = (a->y >> 16) - f->ship_y;
  if (d <= 10 && d >= -30 && (en_rnd(e) & 255u) < 50u) {
    a->v40 = 0;
    a->hit = H_NONE;
    a->flags |= F_TO_FX;
    en_anim_start(e, a, a->type == 0xD4 ? 0x4F436u : 0x4F3F8u);
    for (int k = 2; k >= 0; k--) {
      en_shot(e, shot_sprite(e, L1_SHOT4), a->x >> 16, (a->y >> 16) + 7,
           (a->type == 0xD4 ? 5 : 1) + k, en_rw(e, L1_SPEED4));
    }
    return;
  }
  a->y += a->v40 * 65536;
  a->v48 = (int16_t)(a->v48 + a->v40);
  if (a->v48 == 0 || a->v48 == a->v42) a->v40 = (int16_t)-a->v40;
  en_box(e, a);
}

/* ---- the worms ---- */

/* The head ($503B4): it follows the path, out of sight; at its end it goes,
 * and the parts behind it go into the end hole one a frame. */
static void update_worm_head(enemies_t *e, int8_t i, const enemies_frame_t *f) {
  enemy_t *a = &e->pool[i];
  a->y += f->step * 65536;
  e->worm_slot[a->v48] = true;
  en_path_step(e, a);
  if (a->flags & F_DEAD) en_free_now(e, &e->enemies, i);
}

/* A part ($502C2): where the object ahead of it was last frame, shown once
 * that one had started; behind the end marker, gone into the end hole. */
static void update_worm_part(enemies_t *e, int8_t i, const enemies_frame_t *f) {
  enemy_t *a = &e->pool[i];
  e->worm_slot[a->v48] = true;
  const enemy_t *n = &e->pool[a->next];
  if (n->type == T_WORM_END) {
    if (e->holes[a->v42] == 0) e->holes[a->v42] = en_rw(e, L1_HOLE_OPEN);
    en_free_now(e, &e->enemies, i);
    return;
  }
  a->x = n->x & ~0xFFFF;
  a->y = ((n->y >> 16) + f->step) * 65536;
  a->count = n->count;
  a->dir = n->dir;
  a->frac = n->frac;
  if (a->count >= 0) a->flags |= F_DRAW;
  uint32_t t = a->type == T_WORM_BODY ? L1_WORM_BODY : L1_WORM_SPRITES;
  a->sprite = en_rl(e, t + 4u * (uint32_t)(((a->dir + 16) >> 5) & 7));
  en_box(e, a);
}

/* The end marker ($4020): with no part left, both markers go. */
static void update_worm_end(enemies_t *e, int8_t i) {
  int8_t mark = e->pool[i].head;
  if (e->pool[mark].next != i) return;
  en_free_now(e, &e->enemies, i);
  en_free_now(e, &e->enemies, mark);
}

/* A part's piece ($5025A): a quarter of the sine table's step a frame in
 * its own direction, until it leaves the playfield. */
static void update_worm_piece(enemies_t *e, enemy_t *a) {
  if (!en_animate(e, a)) {
    en_kill(a);
    return;
  }
  const uint8_t *sine = en_raw(e, SINE, 320);
  if (!sine) return;
  int32_t x = (a->x >> 16) + ((int8_t)sine[a->dir + 64] >> 2);
  int32_t y = (a->y >> 16) + ((int8_t)sine[a->dir] >> 2);
  if ((uint32_t)x >= 320u || (uint32_t)y >= 192u) {
    en_kill(a);
    return;
  }
  a->x = x * 65536;
  a->y = y * 65536;
}

/* A worm from slot `slot` ($50050): at one of the 16 holes, chosen by the
 * slot's own generator; its objects pushed on the enemy list in the
 * original's order, so the list reads start marker, the parts (the last
 * first), the head, the end marker. False, nothing made, when the pool has
 * no room for all of them. */
static bool spawn_worm(enemies_t *e, int slot, int32_t line, int32_t step) {
  uint32_t parts = en_rw(e, L1_WORM_PARTS) + 1u;
  int spare = 0;
  for (int8_t i = e->free; i >= 0; i = e->pool[i].next) spare++;
  if ((uint32_t)spare < parts + 3u) return false; /* the original steals instead */
  uint32_t r = e->worm_rng[slot];
  r = r >> 5 | r << 27;
  e->worm_rng[slot] = r;
  uint32_t hole = L1_HOLES + 12u * (r & 15u);
  int32_t x = en_rs(e, hole), y = en_rs(e, hole + 2) - step - line;
  uint16_t start = en_rw(e, hole + 4), end = en_rw(e, hole + 6);
  if (start < ENEMIES_HOLES && e->holes[start] == 0) {
    e->holes[start] = en_rw(e, L1_HOLE_OPEN);
  }
  if (end >= ENEMIES_HOLES) end = 0;
  uint32_t path = en_rl(e, hole + 8);
  int8_t last = en_alloc(e);
  en_push(&e->enemies, e, last);
  int8_t h = en_alloc(e);
  en_push(&e->enemies, e, h);
  enemy_t *a = &e->pool[h];
  a->type = T_WORM_HEAD;
  a->role = R_WORM_HEAD;
  a->flags = 0;
  a->x = x * 65536;
  a->y = y * 65536;
  a->pc = a->start = path;
  a->speed = en_rs(e, L1_WORM_SPEED);
  a->v42 = (int16_t)end;
  a->v48 = (int16_t)slot;
  en_box_off(a);
  int16_t delay = 0;
  for (uint32_t k = 0; k < parts; k++) { /* 7 path steps apart */
    uint32_t rec = L1_WORM_PARTS + 2u + 12u * k;
    int8_t i = en_alloc(e);
    en_push(&e->enemies, e, i);
    enemy_t *p = &e->pool[i];
    p->type = en_rw(e, rec);
    p->role = R_WORM_PART;
    p->hit = H_WORM;
    p->flags = 0; /* shown once it starts */
    p->wreck = en_rl(e, en_rl(e, rec + 2) + 18u); /* its hit's lea: the pieces */
    p->sprite = en_rl(e, rec + 6);
    p->worth = en_rw(e, rec + 10);
    p->hp = 1;
    p->x = x * 65536;
    p->y = y * 65536;
    p->count = delay;
    p->v42 = (int16_t)end;
    p->v48 = (int16_t)slot;
    en_box(e, p);
    delay = (int16_t)(delay - 7);
  }
  int8_t mark = en_alloc(e);
  en_push(&e->enemies, e, mark);
  e->pool[mark].type = T_WORM_MARK;
  e->pool[mark].role = R_WORM_MARK;
  e->pool[mark].flags = 0;
  en_box_off(&e->pool[mark]);
  e->pool[last].type = T_WORM_END;
  e->pool[last].role = R_WORM_END;
  e->pool[last].flags = 0;
  e->pool[last].head = mark;
  en_box_off(&e->pool[last]);
  return true;
}

/* $4FA06: the mid shop and the worms' stretch. */
static int32_t frame(enemies_t *e, int32_t line, int32_t step, int32_t ship_y,
                     int32_t *back_limit) {
  int32_t jump = -1;
  if (!e->shop_asked && line + ship_y < en_rw(e, L1_SHOP_AT)) {
    e->shop_asked = e->shop_wanted = true;
    e->mid_line = 0;
    jump = line = en_rw(e, L1_SHOP_JUMP);
    *back_limit = line;
  }
  int32_t from = en_rw(e, L1_WORMS_FROM), to = en_rw(e, L1_WORMS_TO);
  if (line >= from && line <= to) {
    if (*back_limit < to) *back_limit = to;
    for (int s = 0; s < ENEMIES_WORMS; s++) { /* $5002C */
      if (e->worm_slot[s]) continue;
      e->worm_slot[s] = spawn_worm(e, s, line, step); /* busy until its end */
    }
    for (int k = 0; k < ENEMIES_HOLES; k++) { /* the holes' cells */
      if (e->holes[k] == 0) continue;
      uint32_t cell = L1_HOLE_CELLS + 10u * (uint32_t)k;
      uint32_t word = en_rw(e, cell) / 2u;
      e->holes[k]--;
      if (word < e->map_rows * 20u) {
        e->map[word] = en_rw(e, cell + 2u + 2u * (e->holes[k] & 3u));
      }
    }
  }
  for (int s = 0; s < ENEMIES_WORMS; s++) e->worm_slot[s] = false;
  return jump;
}

/* ---- the end boss ---- */

/* Its eight turrets ($505C0): the first at the enemy list's head, each next
 * after the one before, each following the one before (the first, the
 * boss). */
static void make_turrets(enemies_t *e, int8_t boss) {
  int8_t prev = boss, last = -1;
  for (int k = 7; k >= 0; k--) {
    int8_t i = en_alloc(e);
    if (i < 0) return;
    enemy_t *t = &e->pool[i];
    if (last < 0) {
      en_push(&e->enemies, e, i);
    } else {
      t->next = e->pool[last].next;
      e->pool[last].next = i;
    }
    t->type = k ? T_TURRET : T_TURRET_AIMS;
    t->role = R_TURRET;
    t->hit = H_SHIELD;
    t->sprite = L1_TURRET;
    t->head = prev;
    t->flags |= F_KEEP;
    t->y = -100 * 65536;
    en_box_off(t);
    prev = last = i;
  }
}

/* A triangle wave: n rising over half the period, falling over the rest. */
static int tri(uint32_t n, uint32_t period) {
  n &= period - 1;
  return (int)(n & (period / 2) ? period - 1 - n : n);
}

static void update_boss(enemies_t *e, int8_t i, const enemies_frame_t *f,
                        enemies_events_t *ev) { /* $505AA */
  enemy_t *a = &e->pool[i];
  if (f->line > BOSS_WAKE) {
    en_box_off(a);
    return;
  }
  if (!e->boss_awake) {
    e->boss_awake = true;
    make_turrets(e, i);
    a->dir = 0;
    a->frac = 0;
    a->rate = 0;
    a->acc = 0;
  }
  ev->back_limit = BOSS_WAKE;
  int32_t y = a->y >> 16;
  a->box[0] = 128;
  a->box[2] = 200;
  a->box[1] = (int16_t)(y - f->line + 4);
  a->box[3] = (int16_t)(a->box[1] + 80);
  bool cycling = true;
  if (a->v40 == 0) {
    if (f->line > 48) {
      cycling = false;
    } else {
      a->v40 = 1;
      a->v42 = 0;
      a->v48 = 170;
    }
  }
  if (cycling) {
    if (a->v40 >= 0) {
      if (a->speed < BOSS_OPEN) {
        if (a->speed == 0) en_sound(ev, 26, 1);
        a->speed++;
      }
    } else if (a->speed) {
      a->speed--;
    }
    if (++a->v40 == 0) a->v40++;
    if (a->v40 >= 100) a->v40 = -40;
    /* The turrets' arc, from the frame counter */
    a->dir = (uint8_t)(32 + tri(e->frame, 128));
    a->rate = (tri(e->frame + 80, 256) - 64) * 1024;
    a->acc = (int16_t)((tri(e->frame + 160, 512) - 128) * 32);
    /* Up and down */
    y += a->v42;
    if (y < 16) {
      y = 16;
      a->v48 = 0;
    }
    if (y > 480) {
      y = 480;
      a->v48 = 0;
    }
    a->y = y * 65536;
    if (--a->v48 < 0) {
      if (a->v42 == -1) {
        a->v42 = 0;
        a->v48 = 170;
      } else if (a->v42 == 1) {
        a->v42 = 0;
        a->v48 = 60;
      } else {
        a->v48 = 50;
        a->v42 = y == 16 ? 1 : -1;
      }
    }
    e->eye = (uint8_t)(e->eye + 30 - a->hp);
  }
  /* The mouth: rows 5 and 6, columns 1-4 */
  uint32_t frame = en_rl(e, L1_BOSS_MOUTH + (e->frame & 12u));
  for (int k = 0; k < 8; k++) {
    e->boss_block[(5 + k / 4) * ENEMIES_BOSS_COLS + 1 + k % 4] =
        en_rw(e, frame + 2u * (uint32_t)k);
  }
  if (a->v40 == 0) en_box_off(a);
}

static void update_turret(enemies_t *e, enemy_t *a,
                          const enemies_frame_t *f) { /* $50478 */
  if (!e->boss_awake) {
    en_kill(a);
    return;
  }
  const enemy_t *p = &e->pool[a->head];
  a->x = p->x;
  a->y = p->y;
  a->dir = p->dir;
  a->frac = p->frac;
  a->rate = p->rate;
  a->acc = p->acc;
  a->speed = p->speed;
  if (p->type == T_BOSS) {
    a->x = 160 * 65536;
    a->y = ((p->y >> 16) + 96 - f->line) * 65536;
  }
  a->count = 100;
  en_path_step(e, a);
  if (a->speed == 0) {
    en_box_off(a);
    return;
  }
  if (a->type == T_TURRET_AIMS) {
    int d = en_dir_to(f->ship_x - (a->x >> 16), f->ship_y - (a->y >> 16));
    a->v42 = (int16_t)d;
    a->sprite = en_rl(e, L1_TURRET_AIM + 4u * (uint32_t)d);
    if (a->speed >= BOSS_OPEN) {
      unsigned sum = (unsigned)a->fire_acc + 15u;
      a->fire_acc = (uint8_t)sum;
      if (sum >= 256) {
        a->fire_acc = (uint8_t)(en_rnd(e) & 63u);
        int8_t k = en_shot(e, a->sprite, a->x >> 16, a->y >> 16, d, 4);
        if (k >= 0) en_anim_start(e, &e->pool[k], L1_TURRET_SHOT);
      }
    }
  }
  en_box(e, a);
}

/* The boss, as the level's start code makes it ($508A8). */
static void make_boss(enemies_t *e) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  a->type = T_BOSS;
  a->role = R_BOSS;
  a->hit = H_BOSS;
  a->flags = F_DRAW | F_KEEP;
  a->x = 160 * 65536;
  a->y = 16 * 65536; /* the world line */
  a->world = true;
  a->hp = en_rs(e, L1_BOSS_HP);
  en_box_off(a);
  for (int k = 0; k < ENEMIES_BOSS_COLS * L1_BOSS_ROWS; k++) {
    e->boss_block[k] = en_rw(e, L1_BOSS_BLOCK + 2u * (uint32_t)k);
  }
  e->boss = i;
}

static void spawn_wall(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                       int kind) { /* level 1's $4FBEC, $4FB60, $4FC64 */
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  bool right = en_rw(e, rec + 12) != 0;
  int32_t x = en_rs(e, rec + 8) - 8, y = en_rs(e, rec + 10) - 8;
  a->cell = (int16_t)((y >> 4) * 20 + (x >> 4));
  a->x = x * 65536;
  a->y = y * 65536;
  a->hit = H_WALL;
  a->cols = a->rows = 2;
  a->flags = 0; /* drawn in the map */
  a->worth = 100;
  a->box[0] = a->box[1] = a->box[2] = a->box[3] = 1000;
  uint32_t init;
  if (kind == 2) { /* the dispatch at $4FB4C: kind 1 $4FBEC, kind 2 $4FB60 */
    a->type = right ? 0xE0 : 0xE4;
    a->role = R_WALL1;
    a->hp = en_rs(e, L1_HP1);
    a->tiles = a->type == 0xE0 ? 0x4F5B2u : 0x4F5D2u;
    a->wreck = right ? 0x4F5F2u : 0x4F5FAu;
    init = right ? 0x4F5B2u : 0x4F5D2u;
    if (!right) {
      a->cell = (int16_t)(a->cell - 1);
      a->x -= 16 * 65536;
    }
  } else if (kind == 1) {
    a->type = right ? 0xE8 : 0xEC;
    a->role = R_WALL2;
    a->hp = en_rs(e, L1_HP2);
    a->tiles = a->type == 0xE8 ? 0x4F8ACu : 0x4F88Cu;
    init = right ? 0x4F8ACu : 0x4F88Cu;
    a->wreck = right ? 0x4F8CCu : 0x4F8D4u;
  } else {
    a->type = right ? 0xD8 : 0xDC;
    a->role = R_WALL3;
    a->hp = en_rs(e, L1_HP3);
    a->tiles = a->type == 0xD8 ? 0x4F74Au : 0x4F6FAu;
    init = right ? 0x4FCDCu : 0x4FCD4u;
    a->wreck = 0x4F79Au;
  }
  wall_cells(e, a, init, true);
  (void)f;
}

static void spawn_crawler(enemies_t *e, const enemies_frame_t *f,
                          uint32_t rec) { /* level 1's $4FCE4 */
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  bool right = en_rw(e, rec + 12) != 0;
  a->type = right ? 0xD4 : 0xD0;
  a->role = R_CRAWLER;
  a->hit = H_SINGLE;
  en_anim_start(e, a, right ? 0x4F412u : 0x4F3D4u);
  a->hp = en_rs(e, L1_HP4);
  a->flags |= F_BIG;
  a->worth = 250;
  int32_t x = en_rs(e, rec + 8) - 8 + (right ? 16 : 0);
  a->x = x * 65536;
  a->y = (en_rs(e, rec + 10) - 8 - f->line) * 65536;
  a->v40 = -1;
  a->v42 = a->v48 = (int16_t)(en_rs(e, rec + 6) << 4);
  en_box(e, a);
}

/* ---- the hooks ---- */

/* $508A8: the boss, the worm slots' generators. */
static void start(enemies_t *e) {
  e->mid_line = 2520; /* just before its mid shop */
  make_boss(e);
  for (int s = 0; s < ENEMIES_WORMS; s++) {
    e->worm_rng[s] = en_rl(e, L1_WORM_SEEDS + 8u * (uint32_t)s);
  }
}

/* $4FB42: kinds 1-3 wall objects, 4 a wall crawler. */
static void scenery(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                    int kind, enemies_events_t *ev) {
  (void)ev;
  if (kind >= 1 && kind <= 3) spawn_wall(e, f, rec, kind);
  if (kind == 4) spawn_crawler(e, f, rec);
}

static void update(enemies_t *e, int8_t i, const enemies_frame_t *f,
                   enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  switch (a->role) {
    case R_WALL1: update_wall1(e, a, f); break;
    case R_WALL2: update_wall2(e, a, f); break;
    case R_WALL3: update_wall3(e, a, f); break;
    case R_BOSS: update_boss(e, i, f, ev); break;
    case R_TURRET: update_turret(e, a, f); break;
    case R_WORM_HEAD: update_worm_head(e, i, f); break;
    case R_WORM_PART: update_worm_part(e, i, f); break;
    case R_WORM_END: update_worm_end(e, i); break;
    case R_CRAWLER: update_crawler(e, a, f); break;
    default: break;
  }
}

static void update_fx(enemies_t *e, int8_t i, const enemies_frame_t *f,
                      enemies_events_t *ev) {
  (void)ev;
  enemy_t *a = &e->pool[i];
  if (a->role == R_CRAWLER) update_crawler(e, a, f);
  if (a->role == R_WORM_PIECE) update_worm_piece(e, a);
}

static void hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  switch (a->hit) {
    case H_BOSS: { /* $503DA: only the eye hurts */
      if (!e->boss_awake) break;
      int16_t y = (int16_t)((a->y >> 16) - e->line + 68);
      const int16_t eye[4] = {152, y, 167, (int16_t)(y + 14)};
      if (!en_overlap(e->attacker, eye)) break;
      a->flags |= F_FLASH;
      int16_t before = a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before <= (uint16_t)damage) boss_destroyed(e, a, ev);
      break;
    }
    case H_WORM: { /* $4FE2E, $4FE46: a worm's part bursts into pieces */
      a->flags |= F_FLASH;
      int16_t before = a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before > (uint16_t)damage) break;
      int8_t i = en_alloc(e);
      if (i >= 0) {
        enemy_t *b = &e->pool[i];
        en_push(&e->fx, e, i);
        b->type = a->type;
        b->role = R_WORM_PIECE;
        b->x = a->x & ~0xFFFF;
        b->y = a->y & ~0xFFFF;
        en_anim_start(e, b, a->wreck);
        b->dir = (uint8_t)en_rnd(e); /* the way it flies */
      }
      ev->score += a->worth;
      en_kill(a);
      break;
    }
    default:
      break;
  }
}

/* $5081A: the boss's block, then its eye. */
static bool draw_boss(enemies_t *e, enemy_t *a, int32_t line, bool flash,
                      enemies_draw_fn draw, enemies_block_fn block, void *ctx) {
  if (a->role != R_BOSS) return false;
  if (line >= 640) return true;
  int32_t y = (a->y >> 16) - line;
  block(ctx, e->boss_block, ENEMIES_BOSS_COLS, L1_BOSS_ROWS,
        ENEMIES_BOSS_X, y, flash);
  if (flash) return true;
  const x2_sprite_t *eye =
      en_sprite(e, en_rl(e, L1_BOSS_EYES + ((e->eye >> 4) & 12u)));
  if (eye) {
    draw(ctx, eye, (const uint8_t *)e->level + eye->offset, 152, y + 66, false);
  }
  return true;
}

const enemies_level_t enemies_level1 = {start, scenery, frame, update,
                                         update_fx, NULL, hit, draw_boss, NULL};
