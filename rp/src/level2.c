/**
 * File: level2.c
 * Description: Level 2's own code (enemies_level.h), ported from the
 *              original's at $4F03C-$51470 and run on the level's tables in
 *              the store.
 *
 * - Its scenery ($4FD64): wall guns (kind 1: a map cell opening and closing,
 *   $4FAE2), wall crawlers (kind 3, $4FC06), eggs in the map hatching
 *   creatures when the ship comes near (kinds 4 and 5, $4F710 and $4F540;
 *   the creatures $50346: up the screen, then over the ship and down at
 *   it), and blocks that burst into eight spores (kind 2, $4F88E; the
 *   spores $50798: at the ship, turning to it every 16 frames); their
 *   numbers in the level's block at $4F03C.
 * - Its mid boss (made at level start, $51258): a spider of three eyes set
 *   in the map (update $50C48, hit $50B46) over two holes that send out
 *   chains of twelve creatures ($50218, $5025C; each part on the hole's
 *   path, $508D0, behind the scenery until out); the world held at line
 *   2528 ($CE8) while it lives, scrolling back once two eyes are gone;
 *   destroyed, its structure cleared from the map, the world goes on and a
 *   payout brings the mid shop ($A6C).
 * - Its end boss (made at level start, update $50DE6, drawn $510CE, hit
 *   $50A5C): a block of map words at the level's top that wakes when the
 *   ship comes near, moves up and down, fires and lays sacs ($50440) that
 *   hatch spiders ($505E2) walking the web from junction to junction; only
 *   a small box under its middle hurts it; destroyed, the level is won.
 * - Its webs (+$0C, $5114E, $511A2, $511E6): map cells near the top that
 *   bullets and the ship break, which the spiders spin again; a cell is
 *   there while its map word is.
 */

#include <string.h>

#include "enemies_level.h"

/* Its roles and hits. */
enum { R_GUN = R_LEVEL, R_CRAWLER, R_EGG, R_EGG_BIG, R_HATCHLING, R_BURST,
       R_SPORE, R_EYE, R_CREATURE, R_PIECE, R_BOSS, R_SAC, R_SPIDER };
enum { H_SPORE = H_LEVEL, H_EYE, H_CREATURE, H_BOSS };

/* The level's block of numbers ($4F03C). */
#define GUN_HP 0x4F04Eu
#define GUN_CHANCE 0x4F047u /* byte: the chance a closed gun starts to open */
#define GUN_SHOT 0x4F048u   /* words: its shot's sprite (an index), speed */
#define GUN_SPEED 0x4F04Au
#define CRAWLER_HP 0x4F050u
#define CRAWLER_SPEED 0x4F04Cu
#define HATCHLING_BIG_HP 0x4F042u
#define HATCHLING_HP 0x4F044u
#define BURST_NEAR 0x4F062u /* the screen line a burst block wakes at */

#define SHOTS 0x6BA5Eu         /* the walls' shot sprites: pointers to pointers */
#define CRAWLER_SHOT 0x57078u  /* the crawler's shot sprite */
#define GUN_RIGHT 0x4FBCAu     /* a gun's cell, frame by frame */
#define GUN_LEFT 0x4FBE8u
#define GUN_WRECK_RIGHT 0x4FBC6u
#define GUN_WRECK_LEFT 0x4FBC8u
#define CRAWLER_RIGHT 0x4FF14u /* the crawlers' animations: patrolling, */
#define CRAWLER_LEFT 0x4FED2u
#define CRAWLER_FIRE_RIGHT 0x4FF44u /* firing */
#define CRAWLER_FIRE_LEFT 0x4FF02u

#define EGG_CELLS 0x4F87Cu     /* a small egg's cell, by step */
#define EGG_BIG_CELLS 0x4F6C8u /* a big egg's 2 x 2 cells, by step */
#define HATCHLING 0x4F24Cu     /* the creatures' animations: rising, */
#define HATCHLING_BIG 0x4F21Cu
#define HATCHLING_DIVE 0x4F252u /* diving */
#define HATCHLING_BIG_DIVE 0x4F234u
#define WOBBLE 0x503A6u         /* 8 words: their sway */
#define BURST_RIGHT 0x4FA92u    /* a burst block's 2 x 2 cells: at first, */
#define BURST_LEFT 0x4FABAu
#define BURST_STEPS_RIGHT 0x4F9EAu /* by step (pointers) */
#define BURST_STEPS_LEFT 0x4FA3Eu
#define BURST_SPORES 0x4F9AAu   /* 8 x (animation, dx, dy) */
#define SPORE_DX 0x5085Eu       /* by direction of 8 */
#define SPORE_DY 0x5086Eu
#define SPORE_ANIMS 0x50778u    /* 8 animation lists, by direction */
#define POOL_BASE 0x3927Eu      /* the original's objects, 98 bytes each */
#define EYES 0x5146Cu           /* 3 x (column, row): the spider's eyes */
#define EYE_CELLS 0x50D4Cu      /* an eye's cell, by step */
#define EYE_HP 0x4F060u
#define EYE_DEAD 0x9039u        /* a destroyed eye's cell */
#define HOLE_CELLS 0x50D56u     /* 8 x (map offset, 2 x 2 cells twice) */
#define HOLES 0x50158u          /* 16 x (x, y, hole, end hole, path) */
#define CREATURE_PARTS 0x500C6u /* parts - 1, then (type, hit, sprite, worth) */
#define CREATURE_HP 0x4F052u
#define CREATURE_SPEED 0x4F054u
#define CREATURE_HEAD_SPRITES 0x509FCu /* 8 sprites by direction, each type */
#define CREATURE_BODY_SPRITES 0x50A1Cu
#define CREATURE_TAIL_SPRITES 0x50A3Cu
#define PIECE_HEAD 0x4F264u     /* a destroyed part's piece, each type */
#define PIECE_BODY 0x4F2A0u
#define PIECE_TAIL 0x4F2DCu
#define LEVEL_MAP 0x5873Eu      /* the level's map, as its code names it */
#define SPIDER_CLEARED 0x5A08Eu /* where the spider's death clears the map */
#define SPIDER_WORDS 460
#define SPIDER_LOWEST 2528      /* $CE8 while it lives */
#define SPIDER_FROM 2512        /* the world lines its holes are open in */
#define SPIDER_TO 2896
#define SPIDER_PAST 2544        /* once dead, the world jumps to it */
#define SPIDER_BACK 2880        /* scrolling back, the world may go to it */
#define BOSS_HP 0x4F05Eu
#define BOSS_RATE 0x4F065u      /* byte: its fire rate */
#define BOSS_SHOT_SPEED 0x4F066u
#define BOSS_BLOCK 0x51478u     /* 6 x 9 map words */
#define BOSS_ROWS 9
#define BOSS_MOUTH 0x50E42u     /* 4 pointers to 2 x 2 words: rows 6-7, cols 2-3 */
#define BOSS_LEGS_LEFT 0x51044u /* 2 pointers each to 2 x 2 words: rows 7-8, */
#define BOSS_LEGS_RIGHT 0x5104Cu /* cols 0-1 and 4-5 */
#define BOSS_SHOT 0x5108Cu      /* its shots' animations: aimed, the ring */
#define BOSS_RING 0x510AAu
#define BOSS_TOP 288            /* the back limit while it is there */
#define SAC_ANIM 0x4F318u
#define SAC_HATCH 0x4F33Cu
#define SAC_HATCHED 0x55958u    /* the hatching's last sprite */
#define SAC_DX 0x50420u
#define SAC_DY 0x50430u
#define SPIDER_ANIMS 0x505C2u   /* 8 pointers to animations, by direction */
#define SPIDER_DX 0x506F6u
#define SPIDER_DY 0x50706u
#define SPIDER_JUNCTIONS 0x50726u /* count - 1, then (x, y, 3 ways) */
#define SPIDER_HP 0x4F056u
#define SPIDER_RATE 0x4F059u    /* byte */
#define SPIDER_SHOT 0x4F05Au    /* words: its shot's sprite (an index), speed */
#define SPIDER_SHOT_SPEED 0x4F05Cu
#define WEBS 0x51122u           /* 4 pointers: the cells of each quarter */
#define WEBS_BELOW 432          /* no cells from this world line down */

#define T_GUN_RIGHT 0xE8
#define T_GUN_LEFT 0xEC
#define T_CRAWLER_RIGHT 0x100
#define T_CRAWLER_LEFT 0x104
#define T_EGG 0xFC
#define T_EGG_BIG 0xF8
#define T_HATCHLING 0xE4
#define T_HATCHLING_BIG 0xE0
#define T_BURST_RIGHT 0xF0
#define T_BURST_LEFT 0xF4
#define T_SPORE 0x108
#define T_EYE 0x54
#define T_HEAD 0x114
#define T_BODY 0x118
#define T_TAIL 0x11C
#define T_BOSS_END 0x50 /* the end boss: the spider's death spares it */
#define T_SAC 0x10C
#define T_SPIDER 0x110

/* Below the back limit's reach ($CEA + 208): gone. */
static bool gone(enemy_t *a, const enemies_frame_t *f, int32_t y) {
  if (y <= f->back_limit + 208) return false;
  en_kill(a);
  return true;
}

/* ---- the wall gun ($4FD86, update $4FAE2) ---- */

static void spawn_gun(enemies_t *e, uint32_t rec) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  bool right = en_rw(e, rec + 12) != 0;
  int32_t x = en_rs(e, rec + 8) - 8, y = en_rs(e, rec + 10) - 8;
  a->type = right ? T_GUN_RIGHT : T_GUN_LEFT;
  a->role = R_GUN;
  a->hit = H_WALL;
  a->flags = 0; /* drawn in the map */
  a->x = x * 65536;
  a->y = y * 65536; /* the world line */
  a->cell = (int16_t)((y >> 4) * 20 + (x >> 4));
  a->cols = a->rows = 1;
  a->hp = en_rs(e, GUN_HP);
  a->worth = 100;
  a->tiles = right ? GUN_RIGHT : GUN_LEFT;
  a->wreck = right ? GUN_WRECK_RIGHT : GUN_WRECK_LEFT;
  en_box_off(a);
  if ((uint32_t)a->cell < e->map_rows * 20u) e->map[a->cell] = en_rw(e, a->tiles);
}

static void update_gun(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (gone(a, f, y)) return;
  a->box[0] = (int16_t)(x - 8);
  a->box[2] = (int16_t)(x + 12);
  a->box[1] = (int16_t)(y - f->line + 4);
  a->box[3] = (int16_t)(a->box[1] + 8);
  if (a->v40 == 0) { /* closed: it may start to open */
    if ((uint8_t)en_rnd(e) < en_rb(e, GUN_CHANCE)) a->v40 = 1;
    return;
  }
  if ((uint32_t)a->cell < e->map_rows * 20u) {
    e->map[a->cell] = en_rw(e, a->tiles + 2u * (uint32_t)a->v40);
  }
  if (++a->v40 == 7) { /* open: it fires */
    uint32_t sprite = en_rl(e, en_rl(e, SHOTS + 4u * en_rw(e, GUN_SHOT)));
    en_shot(e, sprite, x + 8, y - f->line + 8, a->type == T_GUN_LEFT ? 2 : 6,
            en_rw(e, GUN_SPEED));
    return;
  }
  if (a->v40 == 15) a->v40 = 0;
}

/* ---- the wall crawler ($4FE2C, update $4FC06) ---- */

static void spawn_crawler(enemies_t *e, const enemies_frame_t *f, uint32_t rec) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  bool right = en_rw(e, rec + 12) != 0;
  a->type = right ? T_CRAWLER_RIGHT : T_CRAWLER_LEFT;
  a->role = R_CRAWLER;
  a->hit = H_SINGLE;
  en_anim_start(e, a, right ? CRAWLER_RIGHT : CRAWLER_LEFT);
  a->hp = en_rs(e, CRAWLER_HP);
  a->worth = 200;
  a->x = (en_rs(e, rec + 8) - 8) * 65536;
  a->y = (en_rs(e, rec + 10) - 8 - f->line) * 65536;
  a->v40 = -1;
  a->v42 = a->v48 = (int16_t)(en_rs(e, rec + 4) << 4);
  en_box(e, a);
}

static void update_crawler(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (a->y >> 16 > f->back_limit + 208 - f->line) {
    en_kill(a);
    return;
  }
  en_animate(e, a);
  a->y += f->step * 65536;
  if (a->v40 == 0) return;
  int32_t d = (a->y >> 16) - f->ship_y;
  if (d <= 10 && d >= -30 && (uint8_t)en_rnd(e) < 30u) { /* it stops and fires */
    a->v40 = 0;
    a->flags |= F_TO_FX;
    bool right = a->type == T_CRAWLER_RIGHT;
    en_anim_start(e, a, right ? CRAWLER_FIRE_RIGHT : CRAWLER_FIRE_LEFT);
    int32_t x = (a->x >> 16) + (right ? 11 : 3), y = (a->y >> 16) + 7;
    en_shot(e, CRAWLER_SHOT, x, y, right ? 6 : 2, en_rw(e, CRAWLER_SPEED));
    return;
  }
  a->y += a->v40 * 65536;
  a->v48 = (int16_t)(a->v48 + a->v40);
  if (a->v48 == 0 || a->v48 == a->v42) a->v40 = (int16_t)-a->v40;
  en_box(e, a);
}

/* A scenery object in the map, on the list the original's $3CEE makes
 * ($C10): at the record's place, the world line its y. */
static enemy_t *in_map(enemies_t *e, uint32_t rec, uint16_t type, uint8_t role) {
  int8_t i = en_alloc(e);
  if (i < 0) return NULL;
  enemy_t *a = &e->pool[i];
  en_push(&e->cells, e, i);
  int32_t x = en_rs(e, rec + 8) - 8, y = en_rs(e, rec + 10) - 8;
  a->type = type;
  a->role = role;
  a->flags = 0; /* drawn in the map */
  a->x = x * 65536;
  a->y = y * 65536;
  a->cell = (int16_t)((y >> 4) * 20 + (x >> 4));
  en_box_off(a);
  return a;
}

/* The map's cells at `a`'s place: one word, or two rows of two. */
static void cell(enemies_t *e, const enemy_t *a, uint32_t word) {
  if ((uint32_t)a->cell < e->map_rows * 20u) e->map[a->cell] = en_rw(e, word);
}

static void cells2(enemies_t *e, const enemy_t *a, uint32_t words) {
  for (int r = 0; r < 2; r++) {
    for (int k = 0; k < 2; k++) {
      uint32_t c = (uint32_t)(a->cell + r * 20 + k);
      if (c < e->map_rows * 20u) e->map[c] = en_rw(e, words + 2u * (uint32_t)(r * 2 + k));
    }
  }
}

/* ---- the eggs ($4FF56, $4FF76; updates $4F710, $4F540) ---- */

static void spawn_egg(enemies_t *e, uint32_t rec, bool big) {
  enemy_t *a = in_map(e, rec, big ? T_EGG_BIG : T_EGG, big ? R_EGG_BIG : R_EGG);
  if (!a) return;
  a->v48 = 1; /* hatches twice */
  if (!big) cell(e, a, EGG_CELLS);
}

/* A creature out of the egg at (x, y) on the screen. */
static void hatch(enemies_t *e, const enemy_t *egg, int32_t x, int32_t y) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  bool big = egg->type == T_EGG_BIG;
  a->type = big ? T_HATCHLING_BIG : T_HATCHLING;
  a->role = R_HATCHLING;
  a->hit = H_SINGLE;
  en_anim_start(e, a, big ? HATCHLING_BIG : HATCHLING);
  a->x = x * 65536;
  a->y = y * 65536;
  a->hp = en_rs(e, big ? HATCHLING_BIG_HP : HATCHLING_HP);
  if (big) a->flags |= F_BIG;
  a->worth = big ? 300 : 150;
  en_box(e, a);
}

static void update_egg(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  bool big = a->type == T_EGG_BIG;
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (y >= f->back_limit + 208) {
    en_kill(a);
    return;
  }
  if (y - f->line >= 180) return; /* not on the screen yet */
  if (a->v40 >= 5) { /* hatched: after a while it may hatch again */
    if (a->v48 == 0) {
      en_kill(a);
      return;
    }
    if (a->v40 >= 9) {
      a->v40 = 0;
      a->v48--;
    }
  }
  if (a->v40 <= 1) {
    if (y - f->line < 100) { /* the ship far: a big one blinks */
      if (!big) return;
      if ((e->frame & 3u) == 0) a->v40 ^= 1;
    } else {
      a->v40 = 2;
    }
  }
  if (big) {
    cells2(e, a, EGG_BIG_CELLS + 8u * (uint32_t)a->v40);
  } else {
    cell(e, a, EGG_CELLS + 2u * (uint32_t)a->v40);
  }
  if (a->v40 < 2 || (e->frame & 3u) != 0) return;
  if (++a->v40 != 4) return;
  if (big) {
    hatch(e, a, x + 16, y - f->line + 16);
  } else {
    hatch(e, a, x, y - f->line);
  }
}

/* $50346: up to line 50, swaying; then across to the ship, and down at it
 * once over it; back up from line 160. */
static void update_hatchling(enemies_t *e, int8_t i, const enemies_frame_t *f) {
  enemy_t *a = &e->pool[i];
  bool big = a->type == T_HATCHLING_BIG;
  uint32_t phase = e->frame + POOL_BASE + 98u * (uint32_t)i; /* its address */
  en_animate(e, a);
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (a->v40 != 0) { /* diving */
    y += 12;
    if (y >= 160) {
      a->v40 = 0;
      en_anim_start(e, a, big ? HATCHLING_BIG : HATCHLING);
    }
  } else if (y > 50) {
    y -= 5;
    x += en_rs(e, WOBBLE + ((phase >> 1) & 14u));
  } else if ((x > f->ship_x ? x - f->ship_x : f->ship_x - x) < 10) {
    a->v40 = 1;
    en_anim_start(e, a, big ? HATCHLING_BIG_DIVE : HATCHLING_DIVE);
  } else {
    x += x >= f->ship_x ? -4 : 4;
    y += en_rs(e, WOBBLE + (phase & 14u));
    if (y > 50) y = 50;
  }
  a->x = x * 65536;
  a->y = y * 65536;
  en_box(e, a);
}

/* ---- the burst blocks ($4FDF6, update $4F88E) and their spores ---- */

static void spawn_burst(enemies_t *e, uint32_t rec) {
  bool right = en_rw(e, rec + 12) != 0;
  enemy_t *a = in_map(e, rec, right ? T_BURST_RIGHT : T_BURST_LEFT, R_BURST);
  if (a) cells2(e, a, right ? BURST_RIGHT : BURST_LEFT);
}

static void update_burst(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (a->v40 == 0) {
    if (y - f->line >= en_rs(e, BURST_NEAR)) a->v40 = 1;
    return;
  }
  uint32_t steps = a->type == T_BURST_RIGHT ? BURST_STEPS_RIGHT : BURST_STEPS_LEFT;
  cells2(e, a, en_rl(e, steps + 4u * (uint32_t)a->v40));
  if (++a->v40 < 21) return;
  for (int d = 7; d >= 0; d--) { /* eight spores, each its way */
    int8_t i = en_alloc(e);
    if (i < 0) break;
    enemy_t *s = &e->pool[i];
    en_push(&e->enemies, e, i);
    uint32_t t = BURST_SPORES + 8u * (uint32_t)(7 - d);
    s->type = T_SPORE;
    s->role = R_SPORE;
    s->hit = H_SPORE;
    en_anim_start(e, s, en_rl(e, t));
    s->x = (x + 16 + en_rs(e, t + 4)) * 65536;
    s->y = (y - f->line + 16 + en_rs(e, t + 6)) * 65536;
    s->v42 = (int16_t)d;
    s->hp = 1;
    s->worth = 20;
    s->v40 = (int16_t)(en_rnd(e) & 31u);
    en_box(e, s);
  }
  en_kill(a);
}

/* $50798: at the ship, turning to it every 16 frames; gone off the
 * screen, into the ship (4 of its shield; an explosion if it cannot be
 * hurt) or after 80 frames (an explosion). */
static void update_spore(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                         enemies_events_t *ev) {
  en_animate(e, a);
  if (++a->v40 >= 80) {
    en_explode(e, a, false, ev);
    en_kill(a);
    return;
  }
  int32_t x = a->x >> 16, y = a->y >> 16;
  if ((a->v40 & 15) == 0) {
    a->v42 = (int16_t)(~en_dir_to(f->ship_x - x, f->ship_y - y) & 7);
    en_anim_start(e, a, en_rl(e, SPORE_ANIMS + 4u * (uint32_t)a->v42));
  }
  x += en_rs(e, SPORE_DX + 2u * (uint32_t)a->v42);
  y += en_rs(e, SPORE_DY + 2u * (uint32_t)a->v42) + f->step;
  if ((uint32_t)x >= 320u || (uint32_t)y >= 192u) {
    en_kill(a);
    return;
  }
  a->x = x * 65536;
  a->y = y * 65536;
  en_box(e, a);
  if (!f->ship_flying || x < f->ship_box[0] || x > f->ship_box[2] ||
      y < f->ship_box[1] || y > f->ship_box[3]) {
    return;
  }
  if (f->ship_invulnerable) {
    en_explode(e, a, false, ev);
  } else {
    ev->shield_damage += 4;
    ev->ship_flash = true;
  }
  en_kill(a);
}

/* ---- the spider ($51258, $50C48, $50B46) and its holes' creatures ---- */

static void make_eyes(enemies_t *e) {
  for (int k = 2; k >= 0; k--) {
    int8_t i = en_alloc(e);
    if (i < 0) return;
    enemy_t *a = &e->pool[i];
    en_push(&e->enemies, e, i);
    uint32_t at = EYES + 4u * (uint32_t)(2 - k);
    int32_t col = en_rs(e, at), row = en_rs(e, at + 2);
    a->type = T_EYE;
    a->role = R_EYE;
    a->hit = H_EYE;
    a->flags = F_KEEP; /* in the map; it outlives a restart */
    a->x = col * 65536;
    a->y = row * 65536;
    a->cell = (int16_t)(row * 20 + col);
    a->hp = en_rs(e, EYE_HP);
    a->v48 = (int16_t)k;
    en_box_off(a);
  }
}

/* A hole's generator, a step on ($50240). */
static uint32_t hole_rnd(enemies_t *e, int slot) {
  uint32_t r = e->worm_rng[slot];
  r = r >> 5 | r << 27;
  e->worm_rng[slot] = r;
  return r;
}

/* A chain of twelve out of a hole for slot `slot` ($5025C): from the first
 * holes while the spider has its last eye or the early ones last, else
 * from the others; each part on the hole's path, ten steps behind the one
 * before, pushed on the enemy list. */
static void spawn_creatures(enemies_t *e, int slot, int32_t line, int32_t step) {
  uint32_t r;
  if (e->eyes == 1) {
    r = hole_rnd(e, slot) & 6u;
  } else if (e->early) {
    e->early--;
    r = hole_rnd(e, slot) & 6u;
  } else {
    r = (hole_rnd(e, slot) & 6u) + 8u;
  }
  uint32_t hole = HOLES + 12u * (r + (uint32_t)slot);
  int32_t x = en_rs(e, hole), y = en_rs(e, hole + 2) - step - line;
  int h = en_rs(e, hole + 4), end = en_rs(e, hole + 6);
  if (h >= 1 && h <= 8 && e->holes[h - 1] == 0) e->holes[h - 1] = 24;
  uint32_t path = en_rl(e, hole + 8);
  int parts = en_rw(e, CREATURE_PARTS) + 1;
  int16_t delay = 0;
  for (int k = 0; k < parts; k++, delay = (int16_t)(delay - 10)) {
    int8_t i = en_alloc(e);
    if (i < 0) break;
    enemy_t *a = &e->pool[i];
    en_push(&e->enemies, e, i);
    uint32_t rec = CREATURE_PARTS + 2u + 12u * (uint32_t)k;
    a->type = en_rw(e, rec);
    a->role = R_CREATURE;
    a->hit = H_CREATURE;
    a->flags = 0; /* hidden until its turn */
    a->sprite = en_rl(e, rec + 6);
    a->worth = en_rw(e, rec + 10);
    a->hp = en_rs(e, CREATURE_HP);
    a->x = x * 65536;
    a->y = y * 65536;
    a->pc = a->start = path;
    a->count = delay;
    a->speed = en_rs(e, CREATURE_SPEED);
    a->v42 = (int16_t)end;
    a->v48 = (int16_t)slot;
    en_box(e, a);
  }
  e->slots |= (uint8_t)(1u << slot);
}

/* $508D0: on the hole's path with the world; hidden until its turn, behind
 * the scenery while it comes out of the hole and goes into the end one;
 * its sprite by its direction. */
static void update_creature(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  a->y += f->step * 65536;
  e->worm_slot[a->v48] = true;
  en_path_step(e, a);
  if (a->flags & F_DEAD) { /* the path's end: the end hole opens */
    int end = a->v42;
    if (end >= 1 && end <= 8 && e->holes[end - 1] == 0) e->holes[end - 1] = 24;
    return;
  }
  en_box(e, a);
  if (!(a->flags & F_DRAW)) {
    if (a->count >= 0) a->flags |= F_DRAW | F_BEHIND;
  } else if (a->flags & F_BEHIND) {
    if (en_rw(e, a->pc) != 0 && a->pc - 10 != a->start) a->flags &= (uint8_t)~F_BEHIND;
  } else if (en_rw(e, a->pc) == 0) {
    a->flags |= F_BEHIND;
  }
  uint32_t t = a->type == T_BODY   ? CREATURE_BODY_SPRITES
               : a->type == T_TAIL ? CREATURE_TAIL_SPRITES
                                   : CREATURE_HEAD_SPRITES;
  a->sprite = en_rl(e, t + 4u * (uint32_t)(((a->dir + 16) >> 5) & 7));
}

/* A destroyed part's piece: off in its own direction a quarter of the sine
 * table's step a frame, until it leaves the playfield. */
static void update_piece(enemies_t *e, enemy_t *a) {
  en_animate(e, a);
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

/* $4FFEE, $50010, $5002A: a part takes hits; the one that would destroy it
 * makes it a piece instead (worth nothing), and a head's slot is free. */
static void hit_creature(enemies_t *e, enemy_t *a, int damage) {
  a->flags |= F_FLASH;
  if (damage < a->hp) {
    a->hp = (int16_t)(a->hp - damage);
    return;
  }
  if (a->type == T_HEAD) e->slots &= (uint8_t)~(1u << a->v48);
  int8_t i = en_alloc(e);
  if (i >= 0) {
    enemy_t *p = &e->pool[i];
    en_push(&e->fx, e, i);
    p->type = a->type;
    p->role = R_PIECE;
    p->x = a->x;
    p->y = a->y;
    en_anim_start(e, p, a->type == T_HEAD   ? PIECE_HEAD
                        : a->type == T_BODY ? PIECE_BODY
                                            : PIECE_TAIL);
    p->dir = (uint8_t)en_rnd(e);
  }
  en_kill(a);
}

/* An eye: open to hits when the ship is on its side (the third once the
 * others are gone) and both holes are not out; its cell blinking; the
 * third also animates the holes' cells. */
static void update_eye(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  int k = a->v48;
  e->back_floor = e->eyes == 1 ? SPIDER_BACK : 0; /* scrolling back: $CCC < 0 */
  bool open;
  if (k == 2) {
    open = e->eyes == 1;
  } else {
    open = (k == 1 ? f->ship_x < 160 : f->ship_x >= 160) && e->slots != 3;
  }
  bool hold = false;
  if (open) {
    int32_t x = (a->x >> 16) * 16, y = (a->y >> 16) * 16 - f->line;
    a->box[0] = (int16_t)x;
    a->box[2] = (int16_t)(x + 15);
    a->box[1] = (int16_t)y;
    a->box[3] = (int16_t)(y + 15);
    hold = a->v40 == 4 && e->slots != 3; /* wide open */
  } else {
    en_box_off(a);
  }
  if (!hold && (e->frame & 1u) == 0) a->v40 = (int16_t)((a->v40 + 1) & 7);
  int step = a->v40 > 4 ? 8 - a->v40 : a->v40;
  if ((uint32_t)a->cell < e->map_rows * 20u) {
    e->map[a->cell] = en_rw(e, EYE_CELLS + 2u * (uint32_t)step);
  }
  if (k != 2) return;
  for (int h = 0; h < 8; h++) { /* the holes' cells */
    if (e->holes[h] == 0) continue;
    uint32_t rec = HOLE_CELLS + 18u * (uint32_t)h;
    e->holes[h]--;
    uint32_t words = rec + (e->holes[h] & 2u ? 10u : 2u);
    uint32_t c = en_rw(e, rec) / 2u;
    for (int r = 0; r < 2; r++) {
      for (int q = 0; q < 2; q++) {
        uint32_t m = c + (uint32_t)(r * 20 + q);
        if (m < e->map_rows * 20u) e->map[m] = en_rw(e, words + 2u * (uint32_t)(r * 2 + q));
      }
    }
  }
}

/* $50B46: an eye hit; the last hit closes both holes' eyes for a while,
 * the third eye's death the spider's. */
static void hit_eye(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  if (e->slots == 3) return;
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) return;
  e->slots = 3;
  if ((uint32_t)a->cell < e->map_rows * 20u) e->map[a->cell] = EYE_DEAD;
  enemy_t c = *a; /* a small explosion at the eye's cell */
  c.x = (a->box[0] + 8) * 65536;
  c.y = (a->box[1] + 8) * 65536;
  c.sprite = 0;
  en_explode(e, &c, false, ev);
  en_kill(a);
  if (--e->eyes == 1) { /* scrolling back now, the holes reseeded */
    e->speed = -1;
    e->worm_rng[0] = 0xD25135D3u;
    e->worm_rng[1] = 0x894D5D53u;
  }
  if (e->eyes != 0) return;
  e->speed = 1; /* the spider destroyed */
  uint32_t from = (SPIDER_CLEARED - LEVEL_MAP) / 2u;
  for (uint32_t w = from; w < from + SPIDER_WORDS && w < e->map_rows * 20u; w++) {
    e->map[w] = 0;
  }
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *o = &e->pool[i];
    if (o->type != T_BOSS_END && !(o->flags & F_DEAD)) en_hit(e, o, 127, ev);
  }
  e->worm_slot[0] = e->worm_slot[1] = false;
  memset(e->groups, 0, sizeof(e->groups));
  enemies_burst(e, 0, 0, 320, 192, 40, true, ev);
  en_payout(e, 5);
  e->lowest = e->mid_line = 0;
  e->back_floor = 0;
  e->beaten = true;
}

/* $4FD20: the holes open between two lines while the spider lives; once
 * it is dead the world is past it. */
static int32_t frame(enemies_t *e, int32_t line, int32_t step, int32_t ship_y,
                     int32_t *back_limit) {
  (void)ship_y;
  int32_t jump = -1;
  if (!e->beaten) {
    if (line >= SPIDER_FROM && line <= SPIDER_TO) { /* $50218 */
      for (int slot = 0; slot < 2; slot++) {
        if (!e->worm_slot[slot]) spawn_creatures(e, slot, line, step);
        e->worm_slot[slot] = true;
      }
    }
  } else if (line > SPIDER_PAST) {
    jump = SPIDER_PAST;
    *back_limit = SPIDER_PAST;
  }
  e->worm_slot[0] = e->worm_slot[1] = false;
  return jump;
}

static void make_boss(enemies_t *e);

/* $51258: the end boss, the spider and its holes; the level's generators. */
static void start(enemies_t *e) {
  make_boss(e);
  e->lowest = e->mid_line = SPIDER_LOWEST;
  e->beaten = false;
  make_eyes(e);
  e->eyes = 3;
  e->slots = 0;
  e->early = 2;
  memset(e->holes, 0, sizeof(e->holes));
  e->worm_rng[0] = 0x45D272BAu;
  e->worm_rng[1] = 0xAA6A4A56u;
}

/* ---- the webs (+$0C: $5114E, $511A2, $511E6) ---- */

/* The cells of the quarter a box's top-left corner is in ($51132). */
static uint32_t webs(const enemies_t *e, int32_t x0, int32_t y0) {
  uint32_t q = (y0 <= 128 ? 4u : 0u) | (x0 <= 128 ? 8u : 0u);
  return en_rl(e, WEBS + q);
}

static uint32_t web_cell(int32_t x, int32_t y) {
  return (uint32_t)((y >> 4) * 20 + (x >> 4));
}

/* $5114E, $511A2: the first cell there under the box breaks: its map word
 * cleared, a small explosion. */
static bool cells_hit(enemies_t *e, const int16_t box[4], int32_t line,
                      enemies_events_t *ev) {
  int16_t b[4] = {box[0], (int16_t)(box[1] + line), box[2], (int16_t)(box[3] + line)};
  if (b[1] >= WEBS_BELOW) return false;
  uint32_t t = webs(e, b[0], b[1]);
  int n = en_rw(e, t);
  for (t += 2; n > 0; n--, t += 8) {
    int32_t x = en_rs(e, t + 2), y = en_rs(e, t + 4);
    uint32_t c = web_cell(x, y);
    if (c >= e->map_rows * 20u || e->map[c] == 0) continue;
    if (x > b[2]) return false; /* the cells by x */
    const int16_t cb[4] = {(int16_t)x, (int16_t)y, (int16_t)(x + 15), (int16_t)(y + 15)};
    if (!en_overlap(b, cb)) continue;
    e->map[c] = 0;
    enemy_t blast = {0};
    blast.x = (x + 8) * 65536;
    blast.y = (y - line + 8) * 65536;
    en_explode(e, &blast, false, ev);
    return true;
  }
  return false;
}

/* $511E6: the first broken cell under a spider spun again. */
static void spin(enemies_t *e, const enemy_t *a, int32_t line) {
  int16_t b[4] = {a->box[0], (int16_t)(a->box[1] + line), a->box[2],
                  (int16_t)(a->box[3] + line)};
  uint32_t t = webs(e, b[0], b[1]);
  int n = en_rw(e, t);
  for (t += 2; n > 0; n--, t += 8) {
    int32_t x = en_rs(e, t + 2), y = en_rs(e, t + 4);
    uint32_t c = web_cell(x, y);
    if (c >= e->map_rows * 20u || e->map[c] != 0) continue;
    const int16_t cb[4] = {(int16_t)x, (int16_t)y, (int16_t)(x + 15), (int16_t)(y + 15)};
    if (!en_overlap(b, cb)) continue;
    e->map[c] = en_rw(e, t + 6);
    return;
  }
}

/* ---- the end boss ($50DE6, $510CE, $50A5C) ---- */

static void make_boss(enemies_t *e) {
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  a->type = T_BOSS_END;
  a->role = R_BOSS;
  a->hit = H_BOSS;
  a->flags = F_DRAW | F_KEEP;
  a->y = 96 * 65536; /* the world line */
  a->world = true;
  a->hp = en_rs(e, BOSS_HP);
  a->v40 = -1; /* asleep */
  en_box_off(a);
  for (int k = 0; k < ENEMIES_BOSS_COLS * BOSS_ROWS; k++) {
    e->boss_block[k] = en_rw(e, BOSS_BLOCK + 2u * (uint32_t)k);
  }
  e->boss = i;
}

/* Two words of a pointed-to pair of rows into the block at (row, col). */
static void block_pair(enemies_t *e, uint32_t words, int row, int col) {
  for (int r = 0; r < 2; r++) {
    for (int k = 0; k < 2; k++) {
      e->boss_block[(row + r) * ENEMIES_BOSS_COLS + col + k] =
          en_rw(e, words + 2u * (uint32_t)(r * 2 + k));
    }
  }
}

static void update_boss(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                        enemies_events_t *ev) {
  e->back_floor = BOSS_TOP;
  if (a->count == 0) { /* not moving */
    if (a->v40 < 0) { /* asleep: the ship near wakes it */
      en_box_off(a);
      if (f->ship_y + f->line <= 176 && f->ship_x >= 0 && f->ship_x <= 319) {
        a->count = 1000;
        a->v42 = 3;
        a->v40 = 340;
      }
      return;
    }
    if (--a->v40 < 0) { /* awake: up */
      a->acc = 0;
      a->count = 1000;
      a->v42 = -3;
    }
  }
  int32_t y = (a->y >> 16) - f->line + 112; /* its mouth */
  unsigned sum = (unsigned)a->fire_acc + en_rb(e, BOSS_RATE);
  a->fire_acc = (uint8_t)sum;
  if (sum >= 256) {
    a->fire_acc = (uint8_t)(en_rnd(e) & 63u);
    if (en_rnd(e) & 3u) {
      int8_t k = en_shot(e, 0, 160, y, 4, en_rw(e, BOSS_SHOT_SPEED));
      if (k >= 0) en_anim_start(e, &e->pool[k], BOSS_SHOT);
    } else {
      for (int d = 15; d >= 0; d--) { /* a ring */
        int8_t k = en_shot(e, 0, 160, y, d, en_rw(e, BOSS_SHOT_SPEED));
        if (k >= 0) en_anim_start(e, &e->pool[k], BOSS_RING);
      }
    }
  }
  a->box[0] = 152;
  a->box[2] = 167;
  a->box[1] = (int16_t)y;
  a->box[3] = (int16_t)(y + 10);
  if (f->line >= 352) return;
  if (a->acc != 0) { /* laying: the mouth opens */
    a->v48 = (int16_t)((a->v48 + 4) & 12);
    block_pair(e, en_rl(e, BOSS_MOUTH + (uint32_t)a->v48), 6, 2);
    if (--a->acc != 0) return;
    int8_t i = en_alloc(e);
    if (i < 0) return;
    enemy_t *s = &e->pool[i];
    en_push(&e->enemies, e, i);
    s->type = T_SAC;
    s->role = R_SAC;
    s->x = 160 * 65536;
    s->y = ((a->y >> 16) - f->line + 128) * 65536;
    en_anim_start(e, s, SAC_ANIM);
    s->v42 = (int16_t)(en_rnd(e) & 7u);
    en_box_off(s);
    return;
  }
  if (a->count != 0) { /* moving: the legs */
    a->count--;
    int32_t wy = (a->y >> 16) + a->v42;
    if (wy < 96) {
      wy = 96;
      a->count = 0;
    }
    if (wy > 192) {
      wy = 192;
      a->count = 0;
    }
    a->y = wy * 65536;
    en_sound(ev, 6, 2);
    uint32_t odd = 4u * (e->frame & 1u);
    block_pair(e, en_rl(e, BOSS_LEGS_LEFT + odd), 7, 0);
    block_pair(e, en_rl(e, BOSS_LEGS_RIGHT + odd), 7, 4);
    return;
  }
  uint32_t r = (uint32_t)e->count; /* $4E2: the enemies about */
  if (e->count < 16) {
    r = en_rnd(e);
    if (!(r & 0x2000u)) { /* a sac soon */
      a->acc = (int16_t)((r & 7u) + 2u);
      return;
    }
  }
  a->count = (int16_t)((r & 7u) + 8u);
  a->v42 = (int16_t)(r & 0x4000u ? -3 : 3);
}

/* $50A5C: destroyed, its payout, explosions over it, the enemies gone,
 * the level won. */
static void hit_boss(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  a->flags |= F_FLASH;
  uint16_t before = (uint16_t)a->hp;
  a->hp = (int16_t)(a->hp - damage);
  if (before > (uint16_t)damage) return;
  en_kill(a);
  en_payout(e, 10);
  enemies_burst(e, 80, (a->y >> 16) - e->line, 160, 112, 30, true, ev);
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) en_kill(&e->pool[i]);
  e->won = true;
}

static bool draw_boss(enemies_t *e, enemy_t *a, int32_t line, bool flash,
                      enemies_draw_fn draw, enemies_block_fn block, void *ctx) {
  (void)draw;
  if (a->role != R_BOSS) return false;
  if (line < 352) {
    block(ctx, e->boss_block, ENEMIES_BOSS_COLS, BOSS_ROWS, ENEMIES_BOSS_X,
          (a->y >> 16) - line, flash);
  }
  return true;
}

/* $50440: a sac drifting inside its box until it reaches an edge, then
 * hatching a spider at the next chance. */
static void update_sac(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  en_animate(e, a);
  a->y += f->step * 65536;
  if (a->v40 == 0) {
    int32_t x = (a->x >> 16) + en_rs(e, SAC_DX + 2u * (uint32_t)a->v42);
    int32_t y = (a->y >> 16) + en_rs(e, SAC_DY + 2u * (uint32_t)a->v42) + f->line;
    if (x <= 116) { x = 116; a->v40 = 1; a->v42 = 4; }
    if (x >= 204) { x = 204; a->v40 = 1; a->v42 = 0; }
    if (y <= 108) { y = 108; a->v40 = 1; a->v42 = 6; }
    if (y >= 340) { y = 340; a->v40 = 1; a->v42 = 2; }
    a->x = x * 65536;
    a->y = (y - f->line) * 65536;
    return;
  }
  if (a->v40 == 1) {
    if ((en_rnd(e) & 7u) == 0) {
      a->v40 = 2;
      en_anim_start(e, a, SAC_HATCH);
    }
    return;
  }
  if (a->sprite != SAC_HATCHED) return;
  int8_t i = en_alloc(e);
  if (i >= 0) {
    enemy_t *s = &e->pool[i];
    en_push(&e->enemies, e, i);
    s->type = T_SPIDER;
    s->role = R_SPIDER;
    s->hit = H_SINGLE;
    s->hp = en_rs(e, SPIDER_HP);
    s->worth = 30;
    en_anim_start(e, s, en_rl(e, SPIDER_ANIMS + 4u * (uint32_t)a->v42));
    s->x = a->x;
    s->y = a->y;
    s->v42 = a->v42;
    s->v40 = -1;
    en_box(e, s);
  }
  en_kill(a);
}

/* $505E2: a spider turning on alternate frames to its new way, then
 * walking it, firing at the ship; at a junction it picks one of three
 * ways; walking, it spins the web again. */
static void update_spider(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  en_animate(e, a);
  a->y += f->step * 65536;
  if (a->v40 >= 0) { /* turning */
    if (!(e->frame & 1u)) {
      en_box(e, a);
      return;
    }
    int d = a->v40 - a->v42;
    if (d != 0) {
      if (d < 0) d += 8;
      a->v42 = (int16_t)((a->v42 + (d > 4 ? -1 : 1)) & 7);
      en_anim_start(e, a, en_rl(e, SPIDER_ANIMS + 4u * (uint32_t)a->v42));
      en_box(e, a);
      return;
    }
    a->v40 = -1;
  }
  int32_t x = a->x >> 16, y = a->y >> 16;
  if ((uint32_t)y < 192u) {
    unsigned sum = (unsigned)a->fire_acc + en_rb(e, SPIDER_RATE);
    a->fire_acc = (uint8_t)sum;
    if (sum >= 256) {
      a->fire_acc = (uint8_t)(en_rnd(e) & 63u);
      uint32_t sprite = en_rl(e, en_rl(e, SHOTS + 4u * en_rw(e, SPIDER_SHOT)));
      en_shot(e, sprite, x, y, en_dir_to(f->ship_x - x, f->ship_y - y),
              en_rw(e, SPIDER_SHOT_SPEED));
    }
  }
  x += en_rs(e, SPIDER_DX + 2u * (uint32_t)a->v42);
  y += en_rs(e, SPIDER_DY + 2u * (uint32_t)a->v42);
  a->x = x * 65536;
  a->y = y * 65536;
  int n = en_rw(e, SPIDER_JUNCTIONS) + 1;
  for (int k = 0; k < n; k++) {
    uint32_t j = SPIDER_JUNCTIONS + 2u + 10u * (uint32_t)k;
    if (x != en_rs(e, j) || y + f->line != en_rs(e, j + 2)) continue;
    uint32_t r;
    do {
      r = en_rnd(e) & 3u;
    } while (r == 0);
    a->v40 = en_rs(e, j + 2u + 2u * r);
    en_box(e, a);
    return;
  }
  en_box(e, a);
  spin(e, a, f->line);
}

/* ---- the hooks ---- */

/* $4FD64: kind 1 a wall gun, 2 a burst block, 3 a wall crawler, 4 and 5
 * eggs. */
static void scenery(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                    int kind, enemies_events_t *ev) {
  (void)ev;
  if (kind == 1) spawn_gun(e, rec);
  if (kind == 2) spawn_burst(e, rec);
  if (kind == 3) spawn_crawler(e, f, rec);
  if (kind == 4 || kind == 5) spawn_egg(e, rec, kind == 5);
}

static void update(enemies_t *e, int8_t i, const enemies_frame_t *f,
                   enemies_events_t *ev) {
  enemy_t *a = &e->pool[i];
  switch (a->role) {
    case R_GUN: update_gun(e, a, f); break;
    case R_CRAWLER: update_crawler(e, a, f); break;
    case R_HATCHLING: update_hatchling(e, i, f); break;
    case R_SPORE: update_spore(e, a, f, ev); break;
    case R_EYE: update_eye(e, a, f); break;
    case R_BOSS: update_boss(e, a, f, ev); break;
    case R_SAC: update_sac(e, a, f); break;
    case R_SPIDER: update_spider(e, a, f); break;
    case R_CREATURE: update_creature(e, a, f); break;
    default: break;
  }
}

static void update_fx(enemies_t *e, int8_t i, const enemies_frame_t *f,
                      enemies_events_t *ev) {
  (void)ev;
  enemy_t *a = &e->pool[i];
  if (a->role == R_CRAWLER) update_crawler(e, a, f);
  if (a->role == R_PIECE) update_piece(e, a);
}

/* $C10: the eggs and the burst blocks. */
static void update_cells(enemies_t *e, int8_t i, const enemies_frame_t *f,
                         enemies_events_t *ev) {
  (void)ev;
  enemy_t *a = &e->pool[i];
  if (a->role == R_EGG || a->role == R_EGG_BIG) update_egg(e, a, f);
  if (a->role == R_BURST) update_burst(e, a, f);
}

static void hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  switch (a->hit) {
    case H_SPORE: /* $634C: it bursts, worth nothing */
      en_explode(e, a, a->flags & F_BIG, ev);
      en_kill(a);
      break;
    case H_EYE: hit_eye(e, a, damage, ev); break;
    case H_CREATURE: hit_creature(e, a, damage); break;
    case H_BOSS: hit_boss(e, a, damage, ev); break;
    default: break;
  }
}

const enemies_level_t enemies_level2 = {start, scenery, frame, update,
                                         update_fx, update_cells, hit,
                                         draw_boss, cells_hit};
