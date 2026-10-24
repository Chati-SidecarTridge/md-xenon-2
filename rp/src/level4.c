/**
 * File: level4.c
 * Description: Level 4's own code (enemies_level.h), ported from the
 *              original's at $4F03C-$51580 and run on the level's tables in
 *              the store.
 *
 * - Its frame code ($4F290): every 16 frames between two world lines a
 *   walker comes down one side or the other (two kinds: $4F44A drops an
 *   egg, $F0/$F4 on the effects list, that hatches a creature on a path
 *   that never ends, $4F61E, $4F924; $4F9CC fires sideways); past line 3568
 *   the world may not go back.
 * - Its scenery ($4FA5E): wall guns (kind 1, $51206: 2 x 2 cells), movers
 *   coming out of the walls (kind 3, $50E3A: across, then up or down to the
 *   ship; back to their start when shot or gone), arms reaching out of the
 *   walls (kind 5, $50C28: drawn as tiles, hurting the ship), the mid boss
 *   (kind 2, $50B72) and the end boss (kind 4, $50180).
 * - The mid boss: twenty parts at line 2560, the world held at 2176 ($CE8)
 *   while it lives: a chain lunging at the ship with a head and a jaw that
 *   fires, a second chain swaying from the frame counter whose last part
 *   fires a ring, four pods; the head hurt only once the pods and the
 *   second chain's end are gone; destroyed: its structure cleared from the
 *   map, the world on to line 2208, a payout and the mid shop.
 * - The end boss: a head drawn as tiles, bobbing; two claws that aim and
 *   fire; a tongue of sixteen parts that shoots out now and then; the head
 *   hurt once both claws are gone; destroyed: its payout, the level won.
 */

#include <string.h>

#include "enemies_level.h"

/* Its roles and hits. */
enum { R_WALKER = R_LEVEL, R_DROP, R_CRITTER, R_STRIDER, R_GUN, R_MOVER,
       R_ARM, R_MID_ROOT, R_MID_LINK, R_MID_CHAIN, R_MID_HEAD, R_MID_JAW,
       R_MID_SWAY,
       R_MID_END, R_POD, R_HEAD, R_CLAW, R_CLAW_DEAD, R_TONGUE_ROOT,
       R_TONGUE };
enum { H_MID_HEAD = H_LEVEL, H_MID_JAW, H_MID_END, H_POD, H_HEAD, H_CLAW,
       H_MOVER };

/* Its variables (e->vars). */
enum { V_MID_LEFT,   /* $D82: the mid boss's targets left */
       V_CLAWS,      /* $D84: the end boss's claws left */
       V_DY,         /* $D86: the end boss's move this frame */
       V_MID_MADE,   /* $E0F */
       V_END_MADE,   /* $E10 */
       V_JUMP };     /* the world line the mid boss's death sends it to */

/* The level's block of numbers ($4F03C). */
#define WALKER_HP 0x4F042u
#define WALKER_SPEED 0x4F044u
#define WALKER_DROP 0x4F047u   /* bytes: rates */
#define CRITTER_HP 0x4F048u
#define CRITTER_SPEED 0x4F04Au
#define STRIDER_SPEED 0x4F04Cu
#define STRIDER_RATE 0x4F04Fu
#define STRIDER_SHOT_SPEED 0x4F050u
#define HEAD_RATE 0x4F053u
#define CLAW_RATE 0x4F055u
#define CLAW_SHOT_SPEED 0x4F056u
#define MID_LUNGE 0x4F059u
#define POD_RATE 0x4F05Bu
#define POD_SHOT_SPEED 0x4F05Cu
#define ARM_RATE 0x4F05Fu
#define MOVER_HP 0x4F060u
#define MOVER_CHANCE 0x4F063u
#define MOVER_SPEED 0x4F064u
#define GUN_HP 0x4F066u
#define WALKER_RATE 0x4F067u
#define GUN_RATE 0x4F069u
#define GUN_SHOT 0x4F06Au
#define GUN_SHOT_SPEED 0x4F06Cu
#define CLAW_HP 0x4F06Eu       /* the start writes them into the parts' table */
#define HEAD_HP 0x4F070u
#define JAW_RATE 0x4F073u
#define JAW_SHOT_SPEED 0x4F074u
#define POD_HP 0x4F077u        /* bytes */
#define MID_END_HP 0x4F079u
#define MID_HEAD_HP 0x4F07Bu
#define MOVER_FIRE 0x4F07Cu
#define MID_END_RATE 0x4F07Fu
#define MID_END_SHOT_SPEED 0x4F080u
#define MID_END_SHOT 0x4F082u
#define WALKER_SHOT_SPEED 0x4F086u
#define WALKER_SHOT 0x4F088u

#define SHOTS 0x76938u          /* the level's shot sprites: pointers to pointers */
#define WALKERS 0x4F35Au        /* 4 x (type, x, update, animation) */
#define DROPS 0x4F57Eu          /* by walker: (type, x, animation) */
#define DROP_HATCH_RIGHT 0x596D2u /* the drops' sprites a creature leaves at */
#define DROP_HATCH_LEFT 0x58FF2u
#define CRITTER_PATH 0x515B6u
#define CRITTER_PATH_LEFT 0x5165Cu
#define CRITTER_PATHS 0x4F914u  /* 4 paths, once one ends */
#define CRITTER_ANIMS 0x4F774u  /* 8 animations by direction */
#define STRIDER_SHOT_RIGHT 0x5A0E4u
#define STRIDER_SHOT_LEFT 0x5A08Eu
#define GUN_RIGHT 0x5135Cu      /* a gun's 2 x 2 cells by step */
#define GUN_LEFT 0x5138Cu
#define GUN_WRECK_RIGHT 0x51432u
#define GUN_WRECK_LEFT 0x5143Au
#define MOVER_RIGHT 0x50EF4u    /* its animation */
#define MOVER_LEFT 0x50ED0u
#define MOVER_DOOR_RIGHT 0x511FAu /* its door: 1 x 3 cells */
#define MOVER_DOOR_LEFT 0x51200u
#define MOVER_DOOR_X 0x511CEu   /* by its record's index: x / 2, world line */
#define MOVER_DOOR_Y 0x511E4u
#define ARM_RIGHT 0x50C70u
#define ARM_LEFT 0x50C94u
#define ARM_CELLS 0x50E2Eu      /* the arm's two rows of cells, either side */
#define MID_PARTS 0x5033Au      /* 20 x (update, hit, animation, x, y, +44, hp, big) */
#define MID_ROOT_SPRITES 0x50772u
#define MID_HEAD_SPRITES 0x507D6u
#define JAW_ANIMS 0x5087Cu
#define JAW_DX 0x5089Cu
#define JAW_DY 0x508ACu
#define MID_SHOT_ANIM 0x5074Eu
#define POD_ANIMS 0x50ACEu
#define POD_SHOT 0x59FCEu
#define MID_CLEARED 0x63330u    /* the structure, cleared from the map */
#define MID_WORDS 320
#define END_PARTS 0x4FA80u      /* 19 x (update, hit, draw, x, y, hp) */
#define HEAD_CELLS 0x4FCACu     /* 6 x 5 */
#define HEAD_CELLS_OPEN 0x4FCE8u /* 6 x 6, the tongue out */
#define HEAD_BOB 0x4FE54u       /* 16 words */
#define CLAW_CELLS 0x4FDA0u     /* pointers to 2 x 2 cells, by state */
#define CLAW_CELLS_RIGHT 0x4FDC4u
#define CLAW_AIM 0x500C8u       /* bytes: a claw's state by direction */
#define CLAW_AIM_RIGHT 0x500D0u
#define CLAW_SHOT_DX 0x50010u
#define CLAW_SHOT_DY 0x50020u
#define CLAW_SHOTS 0x50030u     /* animations by state */
#define TONGUE_SPRITE 0x59AF2u
#define TONGUE_TIP 0x59A42u
#define LEVEL_MAP 0x61CD8u      /* the level's map, as its code names it */

#define T_DROP_RIGHT 0xF0
#define T_CRITTER 0xF8
#define T_GUN_RIGHT 0xD0
#define T_GUN_LEFT 0xD4
#define T_STRIDER_RIGHT 0xD8
#define T_WALKER_RIGHT 0xC8
#define T_MOVER_RIGHT 0xE0
#define T_MOVER_LEFT 0xE4
#define T_ARM_RIGHT 0x10C
#define T_ARM_LEFT 0x110
#define T_MID 0x54
#define T_END 0x50

/* A shot of the level's ($76938: pointers to pointers to sprites). */
static uint32_t shot_sprite(const enemies_t *e, uint32_t index_addr) {
  return en_rl(e, en_rl(e, SHOTS + 4u * en_rw(e, index_addr)));
}

/* A rate's carry: a byte added to an accumulator ($9A48 and its like). */
static bool carry(enemies_t *e, uint8_t *acc, uint32_t rate_addr) {
  unsigned sum = (unsigned)*acc + en_rb(e, rate_addr);
  *acc = (uint8_t)sum;
  if (sum < 256) return false;
  *acc = (uint8_t)(en_rnd(e) & 63u);
  return true;
}

static int tri(uint32_t n, uint32_t period) {
  n &= period - 1;
  return (int)(n & (period / 2) ? period - 1 - n : n);
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The direction of 8 to the ship from (x, y) ($39A4). */
static int to_ship(const enemies_frame_t *f, int32_t x, int32_t y) {
  return en_dir_to(f->ship_x - x, f->ship_y - y);
}

/* Map cells: `cols` x `rows` words from `words`, at cell `cell`. */
static void cells(enemies_t *e, int cell, uint32_t words, int cols, int rows) {
  for (int r = 0; r < rows; r++) {
    for (int k = 0; k < cols; k++) {
      uint32_t c = (uint32_t)(cell + r * 20 + k);
      if (c < e->map_rows * 20u) {
        e->map[c] = en_rw(e, words + 2u * (uint32_t)(r * cols + k));
      }
    }
  }
}

/* A block of the level's map words, `n` of them into `buf` (48 at most),
 * for drawing. */
static const uint16_t *words_at(const enemies_t *e, uint32_t addr, int n,
                                uint16_t *buf) {
  for (int i = 0; i < n && i < 48; i++) buf[i] = en_rw(e, addr + 2u * (uint32_t)i);
  return buf;
}

/* Gone: dead in the original's way (type 4, freed at its next update). */
static void gone(enemy_t *a) { en_kill(a); }

/* ---- the walkers ($4F290, $4F44A, $4F9CC) and what they drop ---- */

static void spawn_walker(enemies_t *e) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  uint32_t k = (en_rnd(e) & 1u) ? 12u : 0u;
  if (e->frame & 32u) k += 24u;
  uint32_t rec = WALKERS + k;
  a->type = en_rw(e, rec);
  a->x = en_rs(e, rec + 2) * 65536;
  a->role = en_rl(e, rec + 4) == 0x4F44Au ? R_WALKER : R_STRIDER;
  a->hit = H_SINGLE;
  en_anim_start(e, a, en_rl(e, rec + 8));
  a->fire_acc = (uint8_t)en_rnd(e);
  a->y = -32 * 65536;
  a->hp = en_rs(e, WALKER_HP);
  a->flags |= F_BIG;
  a->worth = 300;
  en_box(e, a);
}

static void update_walker(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  en_animate(e, a);
  int32_t y = (a->y >> 16) + f->step + en_rs(e, WALKER_SPEED);
  a->y = y * 65536;
  if (y >= 200) {
    gone(a);
    return;
  }
  bool right = a->type == T_WALKER_RIGHT;
  if (++a->v40 == 8) {
    if (carry(e, &a->fire_acc, WALKER_DROP)) { /* an egg dropped */
      int8_t i = en_alloc(e);
      if (i >= 0) {
        enemy_t *d = &e->pool[i];
        en_push(&e->fx, e, i);
        uint32_t rec = DROPS + (right ? 0u : 8u);
        d->type = en_rw(e, rec);
        d->role = R_DROP;
        d->x = en_rs(e, rec + 2) * 65536;
        d->y = (y - 30) * 65536;
        en_anim_start(e, d, en_rl(e, rec + 4));
      }
    }
  } else if (a->v40 == 12) {
    a->v40 = 0;
  }
  if (carry(e, &a->fire, WALKER_RATE)) {
    int32_t x = (a->x >> 16) + 20 - (right ? 0 : 36);
    en_shot(e, shot_sprite(e, WALKER_SHOT), x, y, 4, en_rw(e, WALKER_SHOT_SPEED));
  }
  en_box(e, a);
}

static void update_strider(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  en_animate(e, a);
  int32_t y = (a->y >> 16) + f->step + en_rs(e, STRIDER_SPEED);
  a->y = y * 65536;
  if (y >= 200) {
    gone(a);
    return;
  }
  if (carry(e, &a->fire_acc, STRIDER_RATE)) {
    bool right = a->type == T_STRIDER_RIGHT;
    int32_t x = (a->x >> 16) + 26 - (right ? 0 : 52);
    en_shot(e, right ? STRIDER_SHOT_RIGHT : STRIDER_SHOT_LEFT, x, y + 26,
            right ? 10 : 13, en_rw(e, STRIDER_SHOT_SPEED));
  }
  en_box(e, a);
}

/* $4F61E: the egg falls with the world; on two frames of its animation a
 * creature leaves it; at the animation's end it bursts in place. */
static void update_drop(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  en_animate(e, a);
  if (a->frames == 0) { /* an explosion where it was, no sound */
    const x2_sprite_t *s = en_sprite(e, a->sprite);
    int32_t x = a->x >> 16, y = a->y >> 16;
    if (s) {
      x = (2 * (x - s->hot_x) + s->span) >> 1;
      y = (2 * (y - s->hot_y) + s->height - 1) >> 1;
    }
    a->type = 12;
    a->role = R_EXPLOSION;
    a->x = x * 65536;
    a->y = y * 65536;
    en_anim_start(e, a, BOOM_ANIM);
    return;
  }
  bool left = a->sprite == DROP_HATCH_LEFT;
  if (!left && a->sprite != DROP_HATCH_RIGHT) return;
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *c = &e->pool[i];
  en_push(&e->enemies, e, i);
  c->type = T_CRITTER;
  c->role = R_CRITTER;
  c->hit = H_SINGLE;
  c->dir = left ? 128 : 0;
  c->x = a->x + (left ? -10 : 10) * 65536;
  c->y = a->y;
  c->worth = 100;
  c->hp = en_rs(e, CRITTER_HP);
  c->group = en_group_new(e);
  en_box_off(c);
  en_anim_start(e, c, en_rl(e, CRITTER_ANIMS + 4u * (uint32_t)(c->dir >> 5)));
  c->start = c->pc = left ? CRITTER_PATH_LEFT : CRITTER_PATH;
  c->speed = en_rs(e, CRITTER_SPEED);
}

/* $4F924: a path, and when it ends another, its animation by direction. */
static void update_critter(enemies_t *e, enemy_t *a) {
  uint8_t was = a->dir;
  if (!en_animate(e, a)) {
    en_remove_chain(e, a);
  } else {
    en_path_step(e, a);
    if (!(a->flags & F_DEAD)) en_box(e, a);
  }
  if (a->flags & F_DEAD) { /* on again, on one of four paths */
    a->type = T_CRITTER;
    a->flags = F_DRAW;
    a->hit = H_SINGLE;
    uint32_t k = en_rnd(e) & 8u;
    if ((a->x >> 16) >= 160) k += 4;
    a->start = a->pc = en_rl(e, CRITTER_PATHS + k);
    a->count = 0;
  }
  uint32_t now = ((a->dir + 16u) >> 3) & 28u, before = ((was + 16u) >> 3) & 28u;
  if (now == before) return;
  en_anim_start(e, a, en_rl(e, CRITTER_ANIMS + now));
  en_box(e, a);
}

/* ---- the wall guns (kind 1, $51206, update $51260) ---- */

static void spawn_gun(enemies_t *e, uint32_t rec) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  bool right = en_rw(e, rec + 12) != 0;
  int32_t x = en_rs(e, rec + 8) - 8, y = en_rs(e, rec + 10) - 8;
  a->type = right ? T_GUN_RIGHT : T_GUN_LEFT;
  a->role = R_GUN;
  a->hit = H_WALL; /* $513BC: as the game's wall routine, 2 x 2 */
  a->flags = 0;
  a->x = x * 65536;
  a->y = y * 65536;
  a->cell = (int16_t)((y >> 4) * 20 + (x >> 4));
  a->cols = a->rows = 2;
  a->hp = en_rs(e, GUN_HP);
  a->worth = 400;
  a->tiles = right ? GUN_RIGHT : GUN_LEFT;
  a->wreck = right ? GUN_WRECK_RIGHT : GUN_WRECK_LEFT;
  en_box_off(a);
  cells(e, a->cell, a->tiles, 2, 2);
}

static void update_gun(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (y > f->back_limit + 208) {
    gone(a);
    return;
  }
  a->box[0] = (int16_t)(x + 2);
  a->box[2] = (int16_t)(x + 30);
  a->box[1] = (int16_t)(y - f->line + 2);
  a->box[3] = (int16_t)(a->box[1] + 28);
  if (a->v40 == 0) {
    if (carry(e, &a->fire_acc, GUN_RATE)) a->v40 = 1;
    return;
  }
  cells(e, a->cell, a->tiles + 8u * (uint32_t)a->v40, 2, 2);
  if (++a->v40 == 4) {
    bool left = a->type == T_GUN_LEFT;
    en_shot(e, shot_sprite(e, GUN_SHOT), x + 18 - (left ? 0 : 8),
            y - f->line + 14, left ? 2 : 6, en_rw(e, GUN_SHOT_SPEED));
    return;
  }
  if (a->v40 == 6) a->v40 = 0;
}

/* ---- the movers (kind 3, $50E3A; update $50F18, hit $51080) ---- */

static void mover_home(enemies_t *e, enemy_t *a) { /* $5102A */
  a->x = a->v42 * 65536; /* its start */
  a->y = a->count * 65536;
  a->v40 = 0;
  en_box_off(a);
  en_anim_start(e, a, a->type == T_MOVER_RIGHT ? MOVER_RIGHT : MOVER_LEFT);
  a->hp = en_rs(e, MOVER_HP);
}

static void spawn_mover(enemies_t *e, uint32_t rec) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  bool right = en_rw(e, rec + 12) == 0;
  int32_t x = en_rs(e, rec + 8) - 8 + (right ? 16 : 0);
  int32_t y = en_rs(e, rec + 10) - 8 + 28; /* the world line */
  a->type = right ? T_MOVER_RIGHT : T_MOVER_LEFT;
  a->role = R_MOVER;
  a->hit = H_MOVER;
  a->flags |= F_BIG;
  a->worth = 200;
  a->v48 = en_rs(e, rec + 6);
  uint16_t fire = en_rw(e, MOVER_FIRE);
  a->fire_acc = (uint8_t)(fire >> 8);
  a->fire = (uint8_t)fire;
  a->v42 = (int16_t)x;
  a->count = (int16_t)y;
  mover_home(e, a);
}

static void update_mover(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (y > f->back_limit + 208) {
    gone(a);
    return;
  }
  en_animate(e, a);
  if (a->v40 == 0) { /* in its door: out at random */
    if ((uint8_t)en_rnd(e) < en_rb(e, MOVER_CHANCE)) {
      a->v40 = 1;
      a->frames = 1;
    }
    return;
  }
  int speed = en_rs(e, MOVER_SPEED);
  x += a->type == T_MOVER_RIGHT ? speed : -speed;
  bool across = a->v40 != 1;
  if (!across) { /* coming out: then up or down, the way to the ship */
    if (a->frames == 0) {
      a->v40 = 2;
      a->acc = (int16_t)(f->ship_y - y + f->line);
    }
  } else if (x >= 40 && x <= 280) {
    y += a->acc >= 0 ? speed : -speed;
  }
  a->x = x * 65536;
  a->y = y * 65536;
  if (across && (x < -32 || x >= 352)) {
    mover_home(e, a);
    return;
  }
  en_box(e, a);
  a->box[1] = (int16_t)(a->box[1] - f->line);
  a->box[3] = (int16_t)(a->box[3] - f->line);
}

static void hit_mover(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) return;
  enemy_t c = *a; /* the explosion on the screen */
  c.y = a->y - e->line * 65536;
  en_explode(e, &c, true, ev);
  ev->score += a->worth;
  int8_t i = en_alloc(e); /* $4EA0: a big cash */
  if (i >= 0) {
    enemy_t *b = &e->pool[i];
    en_push(&e->fx, e, i);
    b->type = 24;
    b->role = R_DRIFT;
    b->v40 = 7;
    b->x = a->x;
    b->y = c.y;
    en_anim_start(e, b, CASH_BIG_ANIM);
    b->v42 = (int16_t)en_rnd(e);
  }
  mover_home(e, a);
}

/* ---- the arms (kind 5, $50C28; update $50CB8, draw $50D8A) ---- */

static void spawn_arm(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->cells, e, i);
  bool right = en_rw(e, rec + 12) == 0;
  a->type = right ? T_ARM_RIGHT : T_ARM_LEFT;
  a->role = R_ARM;
  a->x = (en_rs(e, rec + 8) - 8) * 65536;
  a->y = (en_rs(e, rec + 10) - 8 - f->line) * 65536; /* on the screen */
  en_anim_start(e, a, right ? ARM_RIGHT : ARM_LEFT);
  en_box_off(a);
}

static void update_arm(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                       enemies_events_t *ev) {
  int32_t y = (a->y >> 16) + f->step;
  a->y = y * 65536;
  if (y > 392) {
    gone(a);
    return;
  }
  en_animate(e, a);
  bool out = true;
  if (a->v40 == 0) {
    a->v42 = 1;
    out = carry(e, &a->fire_acc, ARM_RATE);
  }
  if (out) {
    a->v40 = (int16_t)(a->v40 + a->v42);
    if (a->v40 == 6) a->v42 = (int16_t)-a->v42;
  }
  int32_t x = a->x >> 16;
  if (a->type == T_ARM_RIGHT) {
    a->box[0] = (int16_t)x;
    a->box[2] = (int16_t)(x + a->v40 * 16 + 16);
  } else {
    a->box[2] = (int16_t)(x + 16);
    a->box[0] = (int16_t)(x + 16 - a->v40 * 16 - 16);
  }
  a->box[1] = (int16_t)(y + 8);
  a->box[3] = (int16_t)(y + 24);
  if (f->ship_flying && en_overlap(a->box, f->ship_box) && !f->ship_invulnerable) {
    ev->shield_damage += 6;
    ev->ship_flash = true;
  }
}

static void draw_arm(enemies_t *e, enemy_t *a, enemies_draw_fn draw,
                     enemies_block_fn block, void *ctx) {
  int32_t x = a->x >> 16, y = a->y >> 16;
  int n = a->v40;
  uint16_t buf[48];
  bool right = a->type == T_ARM_RIGHT;
  if (n > 0) {
    int32_t bx = right ? x : ((x - n * 16 + 16) >> 1) * 2;
    block(ctx, words_at(e, ARM_CELLS - 2u * (uint32_t)n, 2 * n, buf), n, 2,
          right ? (x >> 1) * 2 : bx, y, false);
  }
  const x2_sprite_t *s = en_sprite(e, a->sprite);
  if (!s) return;
  int32_t sx = right ? n * 16 + x : x - n * 16 + 15;
  draw(ctx, s, (const uint8_t *)e->level + s->offset, sx, y + 8, false);
}

static void draw_mover(enemies_t *e, enemy_t *a, int32_t line, bool flash,
                       enemies_draw_fn draw, enemies_block_fn block, void *ctx) {
  int32_t y = (a->y >> 16) - line;
  uint16_t buf[48];
  if (y > 220) return;
  const x2_sprite_t *s = en_sprite(e, a->sprite);
  if (s) draw(ctx, s, (const uint8_t *)e->level + s->offset, a->x >> 16, y, flash);
  if (a->v40 >= 2) return;
  uint32_t k = 2u * (uint32_t)a->v48;
  uint32_t door = a->type == T_MOVER_RIGHT ? MOVER_DOOR_RIGHT : MOVER_DOOR_LEFT;
  block(ctx, words_at(e, door, 3, buf), 1, 3, en_rs(e, MOVER_DOOR_X + k) * 2,
        en_rs(e, MOVER_DOOR_Y + k) - line, false);
}

/* ---- the mid boss (kind 2, $50B72) ---- */

static void make_mid(enemies_t *e, const enemies_frame_t *f, enemies_events_t *ev) {
  ev->restart = true; /* a restart point of its own ($CE6) */
  ev->restart_line = 2480;
  ev->restart_x = 160;
  int8_t prev = -1;
  for (int k = 0; k < 20; k++) {
    int8_t i = en_alloc(e);
    if (i < 0) return;
    enemy_t *a = &e->pool[i];
    en_append(&e->enemies, e, i);
    uint32_t rec = MID_PARTS + 20u * (uint32_t)k;
    uint32_t upd = en_rl(e, rec), hit = en_rl(e, rec + 4);
    a->type = T_MID;
    a->role = upd == 0x5060Cu   ? R_MID_ROOT
              : upd == 0x50782u ? R_MID_HEAD
              : upd == 0x507F6u ? R_MID_JAW
              : upd == 0x509DCu ? R_MID_SWAY
              : upd == 0x50A60u ? R_MID_END
              : upd == 0x50AEEu ? R_POD
              : upd == 0x50A50u ? R_MID_CHAIN
                                : R_MID_LINK;
    a->hit = hit == 0x50554u   ? H_MID_HEAD
             : hit == 0x50550u ? H_MID_JAW
             : hit == 0x504CAu ? H_MID_END
             : hit == 0x504FEu ? H_POD
                               : H_NONE;
    a->flags = F_DRAW | F_KEEP;
    en_anim_start(e, a, en_rl(e, rec + 8));
    a->x = en_rs(e, rec + 12) * 65536;
    a->y = (en_rs(e, rec + 14) - f->line) * 65536;
    a->dir = (uint8_t)en_rs(e, rec + 16);
    a->speed = 3;
    a->hp = en_rb(e, rec + 18);
    if (a->role == R_POD) a->hp = en_rb(e, POD_HP);
    if (a->role == R_MID_END) a->hp = en_rb(e, MID_END_HP);
    if (a->role == R_MID_HEAD) a->hp = en_rb(e, MID_HEAD_HP);
    if (en_rb(e, rec + 19)) a->flags |= F_BIG;
    a->head = prev; /* the part before it */
    prev = i;
    en_box(e, a);
  }
  e->vars[V_MID_LEFT] = 5;
}

/* $506FA: where the part before it is, on as many of its arc's steps. */
static void follow(enemies_t *e, enemy_t *a) {
  const enemy_t *p = &e->pool[a->head];
  a->x = p->x;
  a->y = p->y;
  a->dir = p->dir;
  a->frac = p->frac;
  a->rate = p->rate;
  a->acc = p->acc;
  a->speed = p->speed;
  a->count = 100;
  en_path_step(e, a);
  en_box(e, a);
}

static void mid_sprite(enemies_t *e, enemy_t *a) { /* $50736 */
  a->sprite = en_rl(e, MID_ROOT_SPRITES + (((a->dir + 16u) >> 3) & 12u));
  en_box(e, a);
}

/* $5060C: the first part lunges at the ship now and then. */
static void update_mid_root(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (a->count == 0) {
    a->dir = (uint8_t)((clamp(to_ship(f, x, y), 2, 6) << 5) - 64);
    a->frac = 0;
    a->rate = 0;
    a->acc = 0;
    if ((uint8_t)en_rnd(e) >= en_rb(e, MID_LUNGE)) {
      mid_sprite(e, a);
      return;
    }
    a->v42 = 2;
    a->dir = (uint8_t)((clamp(to_ship(f, x, y), 2, 6) << 5) - 96);
    a->frac = 0;
    a->rate = (int32_t)(((en_rnd(e) & 31u) + 32u) << 10);
    a->acc = 0;
    if (a->dir < 32 || a->dir >= 160) {
      a->dir = (uint8_t)(a->dir + 64);
      a->rate = -a->rate;
    }
  }
  a->count = (int16_t)(a->count + a->v42);
  if (a->count >= 24) a->v42 = (int16_t)-a->v42;
  a->speed = (int16_t)(a->count + 4);
  mid_sprite(e, a);
}

static void update_mid_head(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  e->back_floor = 2480;
  a->y += f->step * 65536;
  follow(e, a);
  int d = clamp(to_ship(f, a->x >> 16, a->y >> 16), 2, 6);
  a->v42 = (int16_t)d;
  a->sprite = en_rl(e, MID_HEAD_SPRITES + 4u * (uint32_t)d);
  en_box(e, a);
}

static void update_mid_jaw(enemies_t *e, enemy_t *a, enemies_events_t *ev) {
  (void)ev;
  en_animate(e, a);
  const enemy_t *p = &e->pool[a->head];
  a->x = (p->x & (int32_t)0xFFFF0000) | (a->x & 0xFFFF);
  a->y = (p->y & (int32_t)0xFFFF0000) | (a->y & 0xFFFF);
  if (p->v42 != a->v42) {
    a->v42 = p->v42;
    en_anim_start(e, a, en_rl(e, JAW_ANIMS + 4u * (uint32_t)a->v42));
  }
  if (carry(e, &a->fire_acc, JAW_RATE)) {
    uint32_t k = 2u * (uint32_t)a->v42;
    int8_t s = en_shot(e, 0, (a->x >> 16) + en_rs(e, JAW_DX + k),
                       (a->y >> 16) + en_rs(e, JAW_DY + k), a->v42,
                       en_rw(e, JAW_SHOT_SPEED));
    if (s >= 0) en_anim_start(e, &e->pool[s], MID_SHOT_ANIM);
  }
  en_box(e, a);
}

/* $509EA: an arc swaying with the frame counter. */
static void sway(enemies_t *e, enemy_t *a) {
  a->dir = (uint8_t)(160 + tri(e->frame * 4u, 128));
  a->rate = (tri(e->frame * 2u + 80u, 256) - 64) * 256;
  a->acc = (int16_t)((tri(e->frame + 160u, 512) - 128) * 4);
  en_box(e, a);
}

static void update_mid_end(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  en_animate(e, a);
  follow(e, a);
  if (carry(e, &a->fire_acc, MID_END_RATE)) { /* a ring of eight */
    for (int d = 7; d >= 0; d--) {
      int8_t s = en_shot(e, shot_sprite(e, MID_END_SHOT), a->x >> 16, a->y >> 16,
                         d, en_rw(e, MID_END_SHOT_SPEED));
      if (s >= 0) en_anim_start(e, &e->pool[s], MID_SHOT_ANIM);
    }
  }
  en_box(e, a);
}

static void update_pod(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  en_animate(e, a);
  int d = to_ship(f, a->x >> 16, a->y >> 16);
  if (d != a->v42) {
    a->v42 = (int16_t)d;
    en_anim_start(e, a, en_rl(e, POD_ANIMS + 4u * (uint32_t)d));
  }
  if (carry(e, &a->fire_acc, POD_RATE)) {
    en_shot(e, POD_SHOT, a->x >> 16, a->y >> 16, a->v42, en_rw(e, POD_SHOT_SPEED));
  }
  en_box(e, a);
  a->box[1] = (int16_t)(a->box[1] - 8);
  a->box[3] = (int16_t)(a->box[3] + 8);
}

static bool hurt(enemy_t *a, int damage) {
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  return before <= (uint16_t)damage;
}

/* $504FE: its edges stop bullets; hit between them, a pod goes. */
static void hit_pod(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  const int16_t top[4] = {a->box[0], a->box[1], a->box[2], a->box[1]};
  const int16_t bottom[4] = {a->box[0], a->box[3], a->box[2], a->box[3]};
  if (en_overlap(e->attacker, bottom) || en_overlap(e->attacker, top)) {
    a->flags |= F_FLASH; /* $6210 */
    return;
  }
  if (!hurt(a, damage)) return;
  e->vars[V_MID_LEFT]--;
  a->worth = 300;
  en_destroyed(e, a, ev);
}

/* $504CA: the second chain's end; destroyed, the world scrolls back and
 * the chain's ten parts go, 100 each. */
static void hit_mid_end(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  if (!hurt(a, damage)) return;
  e->vars[V_MID_LEFT]--;
  e->speed = -1;
  for (int n = 10; n > 0 && a; n--) {
    int8_t prev = a->head;
    a->worth = 100;
    en_destroyed(e, a, ev);
    a = prev >= 0 ? &e->pool[prev] : NULL;
  }
}

/* $50554: the head, once the pods and the chain's end are gone. */
static void hit_mid_head(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  if (e->vars[V_MID_LEFT] != 0) return;
  if (a->next >= 0) e->pool[a->next].flags |= F_FLASH;
  if (!hurt(a, damage)) return;
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) en_kill(&e->pool[i]);
  enemies_burst(e, 0, 0, 320, 192, 40, true, ev);
  en_payout(e, 5);
  uint32_t from = (MID_CLEARED - LEVEL_MAP) / 2u;
  for (uint32_t w = from; w < from + MID_WORDS && w < e->map_rows * 20u; w++) e->map[w] = 0;
  e->lowest = e->mid_line = 0;
  e->back_floor = 0;
  ev->score += 2000;
  e->speed = 1;
  e->vars[V_JUMP] = 2208;
}

/* ---- the end boss (kind 4, $50180) ---- */

static void make_end(enemies_t *e, const enemies_frame_t *f) {
  int8_t first = -1, prev = -1;
  for (int k = 0; k < 19; k++) {
    int8_t i = en_alloc(e);
    if (i < 0) return;
    enemy_t *a = &e->pool[i];
    if (first < 0) first = i;
    en_append(&e->enemies, e, i);
    uint32_t rec = END_PARTS + 18u * (uint32_t)k;
    a->type = T_END;
    a->role = k == 0 ? R_HEAD : k <= 2 ? R_CLAW : k == 3 ? R_TONGUE_ROOT : R_TONGUE;
    a->hit = k == 0 ? H_HEAD : k <= 2 ? H_CLAW : H_SHIELD;
    a->flags = F_KEEP | (k >= 3 ? 0 : F_DRAW);
    a->x = en_rs(e, rec + 12) * 65536;
    a->y = (en_rs(e, rec + 14) - f->line) * 65536;
    a->speed = 3;
    a->hp = k == 0 ? en_rs(e, HEAD_HP) : k <= 2 ? en_rs(e, CLAW_HP) : 0;
    a->flags |= F_BIG;
    en_box_off(a);
    a->chain = first;
    a->head = prev;
    a->sprite = k == 18 ? TONGUE_TIP : TONGUE_SPRITE;
    prev = i;
  }
  e->vars[V_CLAWS] = 2;
}

/* $4FE74: it bobs; hurt once both claws are gone; now and then its
 * tongue shoots out for 128 frames. */
static void update_head(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                        enemies_events_t *ev) {
  e->back_floor = 16;
  int16_t dy = (int16_t)(f->step + en_rs(e, HEAD_BOB + 2u * (e->frame & 15u)));
  e->vars[V_DY] = dy;
  a->y += dy * 65536;
  int32_t y = a->y >> 16;
  if (e->vars[V_CLAWS] != 0) {
    en_box_off(a);
  } else {
    a->box[0] = 148;
    a->box[2] = 172;
    a->box[1] = (int16_t)(y + 32);
    a->box[3] = (int16_t)(y + 40);
  }
  if (a->v40 == 0) { /* the tongue in: out on the rate's carry */
    if (!carry(e, &a->fire_acc, HEAD_RATE)) return;
    en_sound(ev, 29, 1);
  }
  if (++a->v40 >= 128) {
    a->v40 = 0;
  } else if (a->v40 == 120) {
    en_sound(ev, 29, 1);
  }
}

static void update_claw(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                        bool alive) {
  a->y += e->vars[V_DY] * 65536;
  int32_t x = a->x >> 16, y = a->y >> 16;
  a->box[0] = (int16_t)(x + 6);
  a->box[2] = (int16_t)(x + 26);
  a->box[1] = (int16_t)(y + 6);
  a->box[3] = (int16_t)(y + 26);
  if (!alive) return;
  int d = en_dir_to(f->ship_x - x - 16, f->ship_y - y - 16);
  a->v42 = en_rb(e, (x < 160 ? CLAW_AIM : CLAW_AIM_RIGHT) + (uint32_t)d);
  if (carry(e, &a->fire_acc, CLAW_RATE)) {
    uint32_t k = (uint32_t)a->v42;
    int8_t s = en_shot(e, 0, x + 16, y + 16, a->v42, en_rw(e, CLAW_SHOT_SPEED));
    if (s >= 0) {
      enemy_t *b = &e->pool[s];
      en_anim_start(e, b, en_rl(e, CLAW_SHOTS + 4u * k));
      b->x += en_rs(e, CLAW_SHOT_DX + 2u * k) * 65536;
      b->y += en_rs(e, CLAW_SHOT_DY + 2u * k) * 65536;
    }
  }
}

static void hit_claw(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  if (!hurt(a, damage)) return;
  e->vars[V_CLAWS]--;
  enemy_t c = *a; /* a big explosion at the claw */
  c.x = ((a->x >> 16) + 16) * 65536;
  c.y = ((a->y >> 16) - e->line + 32) * 65536; /* as the original: off it */
  c.sprite = 0;
  en_explode(e, &c, true, ev);
  a->v42 = -1;
  a->role = R_CLAW_DEAD;
  a->hit = H_NONE;
}

/* $4FBD6: destroyed, the enemies cleared, its payout, the level won. */
static void hit_head(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  if (!hurt(a, damage)) return;
  int32_t y = a->y >> 16;
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) en_kill(&e->pool[i]);
  en_payout(e, 10);
  enemies_burst(e, 112, y, 96, 80, 20, true, ev);
  e->won = true;
}

/* The tongue: its root at the mouth, the others each on the arc of the
 * one before, as far out as the head's counter says (8 at most). */
static void update_tongue(enemies_t *e, enemy_t *a, bool root) {
  a->y += e->vars[V_DY] * 65536;
  if (root) {
    sway(e, a);
    a->dir ^= 128;
    int n = e->pool[a->chain].v40;
    if (n > 8) n = 128 - n;
    if (n > 8) n = 8;
    a->speed = (int16_t)n;
  } else {
    follow(e, a);
  }
  if (a->speed == 0) {
    en_box_off(a);
    a->flags &= (uint8_t)~F_DRAW;
  } else {
    a->flags |= F_DRAW;
  }
}

/* $4FC68, $4FD6A: the head and the claws, drawn as tiles. */
static bool draw_end(enemies_t *e, enemy_t *a, bool flash, enemies_block_fn block,
                     void *ctx) {
  uint16_t buf[48];
  if (a->role == R_HEAD) {
    bool open = a->v40 != 0;
    block(ctx, words_at(e, open ? HEAD_CELLS_OPEN : HEAD_CELLS, open ? 36 : 30, buf), 6,
          open ? 6 : 5, 112, a->y >> 16, flash);
    return true;
  }
  if (a->role == R_CLAW || a->role == R_CLAW_DEAD) {
    uint32_t t = (a->x >> 16) < 160 ? CLAW_CELLS : CLAW_CELLS_RIGHT;
    uint32_t w = en_rl(e, t + 4u * (uint32_t)a->v42);
    block(ctx, words_at(e, w, 4, buf), 2, 2, ((a->x >> 16) >> 1) * 2, a->y >> 16, flash);
    return true;
  }
  return false;
}

/* ---- the hooks ---- */

/* $514D4: the world held at 2176 while the mid boss lives; its enemies
 * hardest of all. */
static void start(enemies_t *e) {
  e->lowest = e->mid_line = 2176;
  memset(e->vars, 0, sizeof(e->vars));
  e->hp_small = (int16_t)(e->hp_small * 6);
  e->hp_big = (int16_t)(e->hp_big * 4);
}

/* $4FA5E: kind 1 a wall gun, 2 the mid boss, 3 a mover, 4 the end boss, 5
 * an arm. */
static void scenery(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                    int kind, enemies_events_t *ev) {
  switch (kind) {
    case 1: spawn_gun(e, rec); break;
    case 2:
      if (!e->vars[V_MID_MADE]) {
        e->vars[V_MID_MADE] = 1;
        make_mid(e, f, ev);
      }
      break;
    case 3: spawn_mover(e, rec); break;
    case 4:
      if (!e->vars[V_END_MADE]) {
        e->vars[V_END_MADE] = 1;
        make_end(e, f);
      }
      break;
    case 5: spawn_arm(e, f, rec); break;
    default: break;
  }
}

/* $4F290. */
static int32_t frame(enemies_t *e, int32_t line, int32_t step, int32_t ship_y,
                     int32_t *back_limit) {
  (void)step;
  (void)ship_y;
  if (*back_limit >= 3568 && line <= 3568) *back_limit = 3568;
  if ((e->frame & 15u) == 0 && line <= 3664 && line >= 2784) spawn_walker(e);
  if (e->vars[V_JUMP]) { /* past the mid boss */
    int32_t jump = e->vars[V_JUMP];
    e->vars[V_JUMP] = 0;
    *back_limit = jump;
    return jump;
  }
  return -1;
}

static void update(enemies_t *e, int8_t i, const enemies_frame_t *f,
                   enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  switch (a->role) {
    case R_WALKER: update_walker(e, a, f); break;
    case R_STRIDER: update_strider(e, a, f); break;
    case R_CRITTER: update_critter(e, a); break;
    case R_GUN: update_gun(e, a, f); break;
    case R_MOVER: update_mover(e, a, f); break;
    case R_MID_ROOT: update_mid_root(e, a, f); break;
    case R_MID_LINK: /* $5072C: the first chain's, its sprite by direction */
      a->y += f->step * 65536;
      follow(e, a);
      mid_sprite(e, a);
      break;
    case R_MID_CHAIN: /* $50A50 */
      a->y += f->step * 65536;
      follow(e, a);
      break;
    case R_MID_HEAD: update_mid_head(e, a, f); break;
    case R_MID_JAW: update_mid_jaw(e, a, ev); break;
    case R_MID_SWAY:
      a->y += f->step * 65536;
      a->speed = 12;
      sway(e, a);
      break;
    case R_MID_END: update_mid_end(e, a, f); break;
    case R_POD: update_pod(e, a, f); break;
    case R_HEAD: update_head(e, a, f, ev); break;
    case R_CLAW: update_claw(e, a, f, true); break;
    case R_CLAW_DEAD: update_claw(e, a, f, false); break;
    case R_TONGUE_ROOT: update_tongue(e, a, true); break;
    case R_TONGUE: update_tongue(e, a, false); break;
    default: break;
  }
}

static void update_fx(enemies_t *e, int8_t i, const enemies_frame_t *f,
                      enemies_events_t *ev) {
  (void)ev;
  enemy_t *a = &e->pool[i];
  if (a->role == R_DROP) update_drop(e, a, f);
}

static void update_cells(enemies_t *e, int8_t i, const enemies_frame_t *f,
                         enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  if (a->role == R_ARM) update_arm(e, a, f, ev);
}

static void hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  switch (a->hit) {
    case H_POD: hit_pod(e, a, damage, ev); break;
    case H_MID_END: hit_mid_end(e, a, damage, ev); break;
    case H_MID_HEAD: hit_mid_head(e, a, damage, ev); break;
    case H_MID_JAW: /* $50550: its head takes it */
      if (a->head >= 0) hit_mid_head(e, &e->pool[a->head], damage, ev);
      break;
    case H_HEAD: hit_head(e, a, damage, ev); break;
    case H_CLAW: hit_claw(e, a, damage, ev); break;
    case H_MOVER: hit_mover(e, a, damage, ev); break;
    default: break;
  }
}

static bool draw(enemies_t *e, enemy_t *a, int32_t line, bool flash,
                 enemies_draw_fn fn, enemies_block_fn block, void *ctx) {
  switch (a->role) {
    case R_ARM: draw_arm(e, a, fn, block, ctx); return true;
    case R_MOVER: draw_mover(e, a, line, flash, fn, block, ctx); return true;
    case R_HEAD:
    case R_CLAW:
    case R_CLAW_DEAD: return draw_end(e, a, flash, block, ctx);
    default: return false;
  }
}

const enemies_level_t enemies_level4 = {start, scenery, frame, update,
                                         update_fx, update_cells, hit, draw,
                                         NULL};
