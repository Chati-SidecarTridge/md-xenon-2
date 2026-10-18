/**
 * File: enemies.c
 * Description: The original's enemies, a frame at a time (enemies.h).
 *
 * Addresses are the original's (the game image at $400, the level at
 * $4F000); what they name is read in the store as the original holds it.
 * - The triggers ($3DC2): the wave table (level header +$38, handler +$10)
 *   and the scenery table (+$14, handler +$18), 14-byte records whose first
 *   word is a world line; a record fires on the frame the world line comes
 *   to it (at or below the lowest line the table saw, below the current).
 * - Waves ($3B18): kind 0 a capsule ($3AC0), 1-10 the level's enemy types,
 *   whose records the wave handler's jump table names: `count` members on a
 *   path (header +$30), a trail (members delayed by the spacing in steps)
 *   or a row (spacing - 100 pixels apart), the firing, the steps a frame.
 *   Members spawned in the same 4 frames make a group ($43C): the last of a
 *   group destroyed drops cash.
 * - Paths ($9ACA): 2 an arc (direction, turn rate, turn acceleration,
 *   steps), 4 a wait, 6 a random branch, 8 a jump, 10 the start, 0 the
 *   end; a step moves by the game's sine table ($9C80) / 64 pixels.
 * - Fire ($9A48): a rate added to an accumulator; on its carry a shot
 *   ($39FE, update $4180) in a random direction of 8, or at the ship.
 * - Hits: $6240 one enemy, $6160 a chain, $3EEC a capsule (its token),
 *   $63A8 a wall object; explosions ($3A5E), cash and tokens ($4EA0), the
 *   pickups ($4F7C).
 * - Level 1's scenery ($4FB42): restart points ($3D86), wall objects drawn
 *   in the map ($4F4CE, $4F7A2, $4F602) and wall crawlers ($4F8DC); their
 *   numbers in the level's block at $4F03C.
 * - Level 1's frame code ($4FA06): between two world lines ($4FA3C, $4FA44)
 *   the world may go back to the lower, five worm slots ($5002C) each send
 *   a worm out of one of 16 holes ($4FF6C) chosen by the slot's own
 *   generator ($D5C), and the holes' cells animate ($4FAA2). A worm
 *   ($50050) is a start marker ($B8, the original's box over the chain),
 *   eleven parts (update $502C2: where the object ahead was last frame;
 *   hit $4FE2E, $4FE46: pieces $5025A), its head on a path ($503B4, never
 *   shown) and an end marker ($BC, $4020); the parts follow the head into
 *   the end hole.
 * - Level 1's end boss (made at level start, $508A8; update $505AA, draw
 *   $5081A, hit $503DA): asleep until the world line reaches 448, then eight
 *   turrets ($50478) chained along an arc around it, an opening cycle, its
 *   weak point the eye; destroyed: its payout ($2996), 20 explosions
 *   ($4A98), the enemies cleared ($707E), the level won ($A07).
 * Objects marked to stay (the original's +96: the boss and its turrets)
 * outlive a restart ($750C).
 */

#include "enemies.h"

#include <stddef.h>
#include <string.h>

#include "rng.h"

/* Roles: the update an object runs. */
enum { R_PATH, R_FOLLOW, R_WALL1, R_WALL2, R_WALL3, R_CRAWLER, R_EXPLOSION,
       R_DRIFT, R_SHOT, R_BOSS, R_TURRET, R_WORM_HEAD, R_WORM_PART,
       R_WORM_END, R_WORM_MARK, R_WORM_PIECE };
/* Hits. */
enum { H_NONE, H_SINGLE, H_CHAIN, H_CAPSULE, H_WALL, H_BOSS, H_SHIELD,
       H_WORM };
/* Flags. */
#define F_DRAW 0x01
#define F_FLASH 0x02
#define F_BIG 0x04
#define F_CHAINED 0x08
#define F_DEAD 0x10
#define F_TO_FX 0x20 /* a crawler that fired: off to the other list */
#define F_KEEP 0x40  /* outlives a restart (the original's +96) */

#define T_DEAD 4
#define T_EXPLOSION 12
#define T_SHOT 20
#define T_CASH_BIG 24
#define T_CASH 96
#define T_CAPSULE 100
#define T_BOSS_A 80 /* never destroyed by the ship's body or the bomb */
#define T_BOSS_B 84

#define BASE X2LEVEL_BASE
#define UPDATE_PATH 0xE1Eu  /* the type records' routines (jump vectors) */
#define UPDATE_FOLLOW 0xE36u
#define HIT_CHAIN 0xE30u

/* The game image. */
#define SINE 0x9C80u
#define SHOT_DX 0x4140u
#define SHOT_DY 0x4160u
#define TOKEN_TABLE 0x3F96u
#define TOKEN_ROUTINES 0x5040u
#define CAPSULE_ANIM 0x2D30u
#define CASH_ANIM 0x2E9Eu
#define CASH_BIG_ANIM 0x2E3Eu
#define BOOM_ANIM 0x2EFEu
#define BOOM_BIG_ANIM 0x2F2Au

/* Level 1's own code. */
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

/* Cash and tokens: 2 pixels in one of 8 directions ($4FD8). */
static const int8_t drift[8][2] = {{0, -2}, {2, -2}, {2, 0},  {2, 2},
                                   {0, 2},  {-2, 2}, {-2, 0}, {-2, -2}};

/* ---- the original's memory ---- */

static const uint8_t *raw(const enemies_t *e, uint32_t addr, uint32_t len) {
  return addr >= BASE ? x2level_raw(e->level, addr, len)
                      : x2global_raw(e->global, addr, len);
}

static uint16_t rw(const enemies_t *e, uint32_t addr) {
  const uint8_t *p = raw(e, addr, 2);
  return p ? (uint16_t)((p[0] << 8) | p[1]) : 0;
}

static int16_t rs(const enemies_t *e, uint32_t addr) {
  return (int16_t)rw(e, addr);
}

static uint32_t rl(const enemies_t *e, uint32_t addr) {
  return ((uint32_t)rw(e, addr) << 16) | rw(e, addr + 2);
}

static uint8_t rb(const enemies_t *e, uint32_t addr) {
  const uint8_t *p = raw(e, addr, 1);
  return p ? *p : 0;
}

static const x2_sprite_t *sprite(const enemies_t *e, uint32_t source) {
  if (source >= BASE) {
    return x2sprite_find(x2level_sprites(e->level), (int)e->level->sprite_count,
                         source);
  }
  return x2sprite_find(x2global_sprites(e->global), (int)e->global->sprite_count,
                       source);
}

/* ---- the game's random generator ($27FE) ---- */

static uint32_t rnd(enemies_t *e) { return rng_next(e->rng); }

/* A random number below n ($28A4). */
static int rnd_mod(enemies_t *e, int n) {
  return (int)(((rnd(e) >> 2) & 0xFFFFu) % (uint32_t)n);
}

/* ---- the direction of 8 towards (dx, dy) ($39A4): 0 up, clockwise ---- */

static int dir_quadrant(int dx, int dy) { /* dx, dy >= 0 */
  if (dx == 0) return 4;
  if (dy == 0) return 2;
  if (dx == dy) return 3;
  if (dx < dy) {
    uint32_t q = ((uint32_t)(dx >> 1) << 16) / (uint32_t)dy;
    return (q & 0xFFFFu) >= 13572u ? 3 : 4;
  }
  uint32_t q = ((uint32_t)(dy >> 1) << 16) / (uint32_t)dx;
  return (q & 0xFFFFu) >= 13572u ? 3 : 2;
}

static int dir_half(int dx, int dy) { /* dy >= 0 */
  if (dx >= 0) return dir_quadrant(dx, dy);
  return (8 - dir_quadrant(-dx, dy)) & 7;
}

static int dir_to(int dx, int dy) {
  if (dy >= 0) return dir_half(dx, dy);
  return (4 - dir_half(dx, -dy)) & 7;
}

/* ---- the lists ---- */

static int8_t alloc(enemies_t *e) {
  int8_t i = e->free;
  if (i < 0) return -1;
  e->free = e->pool[i].next;
  memset(&e->pool[i], 0, sizeof(enemy_t));
  e->pool[i].next = e->pool[i].head = e->pool[i].chain = -1;
  e->pool[i].flags = F_DRAW;
  return i;
}

static void push(int8_t *head, enemies_t *e, int8_t i) {
  e->pool[i].next = *head;
  *head = i;
}

/* At a list's end (the original's tail pointers, $BAA). */
static void append(int8_t *head, enemies_t *e, int8_t i) {
  int8_t *link = head;
  while (*link >= 0) link = &e->pool[*link].next;
  e->pool[i].next = -1;
  *link = i;
}

static void box_off(enemy_t *a) {
  a->box[0] = a->box[1] = a->box[2] = a->box[3] = 1000;
}

static void kill(enemy_t *a) {
  a->type = T_DEAD;
  a->flags = F_DEAD;
  a->hit = H_NONE;
}

/* The dead off a list, back to the free list. */
static void sweep(enemies_t *e, int8_t *head) {
  int8_t *link = head;
  while (*link >= 0) {
    enemy_t *a = &e->pool[*link];
    if (a->flags & F_DEAD) {
      int8_t i = *link;
      *link = a->next;
      a->type = 0;
      a->next = e->free;
      e->free = i;
    } else {
      link = &a->next;
    }
  }
}

static void sound(enemies_events_t *ev, int effect, int channel) {
  if (ev->nsounds < ENEMIES_SOUNDS) {
    ev->sounds[ev->nsounds].effect = (uint8_t)effect;
    ev->sounds[ev->nsounds].channel = (uint8_t)channel;
    ev->nsounds++;
  }
}

/* ---- animation ($34C2) ---- */

static void anim_start(enemies_t *e, enemy_t *a, uint32_t list) {
  a->sprite = rl(e, list);
  a->frames = rs(e, list + 4);
  a->anim = list + 6;
}

/* False when the animation ended (the object is gone). */
static bool animate(enemies_t *e, enemy_t *a) {
  if (--a->frames < 0) {
    a->frames = 0;
    return true;
  }
  if (a->frames != 0) return true;
  uint32_t s = rl(e, a->anim);
  if (s) {
    a->sprite = s;
    a->frames = rs(e, a->anim + 4);
    a->anim += 6;
    return true;
  }
  uint32_t routine = rl(e, a->anim + 4);
  if (routine == X2SPRITE_LOOP || routine == X2SPRITE_LOOP_AT) {
    anim_start(e, a, rl(e, a->anim + 8));
    return true;
  }
  return false; /* $34FA: the end */
}

/* The end of an animation or a path: the object, or its whole chain. */
static void remove_chain(enemies_t *e, enemy_t *a) {
  if (!(a->flags & F_CHAINED) || a->head < 0) {
    kill(a);
    return;
  }
  for (int8_t i = a->head; i >= 0;) {
    int8_t n = e->pool[i].chain;
    kill(&e->pool[i]);
    i = n;
  }
}

/* ---- boxes ($3D58) ---- */

bool enemies_box(const enemies_t *e, uint32_t source, int32_t x, int32_t y,
                 int16_t box[4]) {
  const x2_sprite_t *s = sprite(e, source);
  if (!s) return false;
  box[0] = (int16_t)(x - s->hot_x + s->box_x);
  box[1] = (int16_t)(y - s->hot_y + s->box_y);
  box[2] = (int16_t)(box[0] + s->box_w - 1);
  box[3] = (int16_t)(box[1] + s->box_h - 1);
  return true;
}

static void box(const enemies_t *e, enemy_t *a) {
  if (!enemies_box(e, a->sprite, a->x >> 16, a->y >> 16, a->box)) {
    a->box[0] = a->box[1] = 1000;
    a->box[2] = a->box[3] = 1000;
  }
}

static bool overlap(const int16_t *a, const int16_t *b) {
  return a[2] >= b[0] && a[0] <= b[2] && a[3] >= b[1] && a[1] <= b[3];
}

/* ---- objects made on the way ---- */

/* An explosion at the centre of `a`'s sprite ($6250, $3A5E). */
static void explode(enemies_t *e, const enemy_t *a, bool big,
                    enemies_events_t *ev) {
  sound(ev, big ? 15 : 44, 1);
  const x2_sprite_t *s = sprite(e, a->sprite);
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (s) {
    x = (2 * (x - s->hot_x) + s->span) >> 1;
    y = (2 * (y - s->hot_y) + s->height - 1) >> 1;
  }
  int8_t i = alloc(e);
  if (i < 0) return;
  enemy_t *b = &e->pool[i];
  b->type = T_EXPLOSION;
  b->role = R_EXPLOSION;
  b->x = x * 65536;
  b->y = y * 65536;
  anim_start(e, b, big ? BOOM_BIG_ANIM : BOOM_ANIM);
  push(&e->fx, e, i);
}

/* Cash or a token, drifting from (x, y) ($62AC, $3EEC). */
static int8_t drifter(enemies_t *e, uint16_t type, uint32_t list, int32_t x,
                      int32_t y) {
  int8_t i = alloc(e);
  if (i < 0) return -1;
  enemy_t *b = &e->pool[i];
  b->type = type;
  b->role = R_DRIFT;
  b->x = x;
  b->y = y;
  b->v40 = 7;
  b->v42 = (int16_t)rnd(e);
  anim_start(e, b, list);
  push(&e->fx, e, i);
  return i;
}

/* An enemy's shot ($39FE): the sprite at `source`, direction 0-15. */
static int8_t shot(enemies_t *e, uint32_t source, int32_t x, int32_t y,
                   int dir, int speed) {
  int8_t i = alloc(e);
  if (i < 0) return -1;
  enemy_t *b = &e->pool[i];
  b->type = T_SHOT;
  b->role = R_SHOT;
  b->sprite = source;
  b->x = x * 65536;
  b->y = y * 65536;
  b->v42 = (int16_t)(dir & 15);
  b->speed = (int16_t)speed;
  b->box[0] = b->box[1] = b->box[2] = b->box[3] = 1000;
  push(&e->fx, e, i);
  return i;
}

/* A wall object's shot sprite: level 1's table of pointers to pointers. */
static uint32_t shot_sprite(const enemies_t *e, uint32_t index_addr) {
  return rl(e, rl(e, L1_SHOTS + 4u * rw(e, index_addr)));
}

/* ---- kills ---- */

static void group_down(enemies_t *e, const enemy_t *a, enemies_events_t *ev) {
  (void)ev;
  if (!a->group) return;
  for (int g = 0; g < ENEMIES_GROUPS; g++) {
    if (e->groups[g].id != a->group) continue;
    if (--e->groups[g].left == 0) {
      e->groups[g].id = 0;
      bool big = a->flags & F_BIG;
      drifter(e, big ? T_CASH_BIG : T_CASH, big ? CASH_BIG_ANIM : CASH_ANIM,
              a->x, a->y);
    }
    return;
  }
}

/* $6250: an enemy destroyed. */
static void destroyed(enemies_t *e, enemy_t *a, enemies_events_t *ev) {
  explode(e, a, a->flags & F_BIG, ev);
  group_down(e, a, ev);
  ev->score += a->worth;
  kill(a);
}

/* A bubble of the boss's payout taken or gone ($4EE0, $4FA2). */
static void payout_down(enemies_t *e) {
  if (e->payout > 0 && --e->payout == 0) e->paid = true;
}

/* $503DA, the eye's last hit. */
static void boss_destroyed(enemies_t *e, enemy_t *a, enemies_events_t *ev) {
  e->boss_awake = false;
  kill(a);
  for (int k = 0; k < 9; k++) { /* $2996: a 50 and a 100, nine times */
    for (int big = 0; big < 2; big++) {
      int8_t i = alloc(e);
      if (i < 0) break;
      enemy_t *b = &e->pool[i];
      b->type = big ? T_CASH_BIG : T_CASH;
      b->role = R_DRIFT;
      anim_start(e, b, big ? CASH_BIG_ANIM : CASH_ANIM);
      b->x = (rnd_mod(e, 300) + 10) * 65536;
      b->y = (rnd_mod(e, 180) + 6) * 65536;
      b->v40 = 7;
      b->v42 = (int16_t)rnd(e);
      append(&e->fx, e, i);
      e->payout++;
    }
  }
  for (int k = 0; k < 20; k++) { /* $4A98 */
    int8_t i = alloc(e);
    if (i < 0) break;
    enemy_t *b = &e->pool[i];
    b->type = T_EXPLOSION;
    b->role = R_EXPLOSION;
    b->x = rnd_mod(e, 320) * 65536;
    b->y = rnd_mod(e, 192) * 65536;
    anim_start(e, b, BOOM_BIG_ANIM);
    push(&e->fx, e, i);
  }
  sound(ev, 15, 2);
  sound(ev, 15, 1);
  sound(ev, 15, 0);
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) kill(&e->pool[i]);
  e->won = true;
}

static void hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  switch (a->hit) {
    case H_SINGLE: {
      int16_t before = a->hp;
      a->flags |= F_FLASH;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before <= (uint16_t)damage) destroyed(e, a, ev);
      break;
    }
    case H_CHAIN: {
      enemy_t *head = &e->pool[a->head];
      for (int8_t i = a->head; i >= 0; i = e->pool[i].chain) {
        e->pool[i].flags |= F_FLASH;
      }
      int16_t before = head->hp;
      head->hp = (int16_t)(head->hp - damage);
      if ((uint16_t)before > (uint16_t)damage) break;
      for (int8_t i = head->chain; i >= 0;) {
        int8_t n = e->pool[i].chain;
        if (!(e->pool[i].flags & F_CHAINED)) explode(e, &e->pool[i], head->flags & F_BIG, ev);
        kill(&e->pool[i]);
        i = n;
      }
      destroyed(e, head, ev);
      break;
    }
    case H_CAPSULE: { /* $3EEC: its token */
      uint32_t t = TOKEN_TABLE + 6u * (uint32_t)a->v48;
      uint16_t type = rw(e, t);
      sound(ev, rb(e, TOKEN_ROUTINES + 4u * type) ? 16 : 9, 2);
      int8_t i = drifter(e, type, rl(e, t + 2), a->x, a->y);
      if (i >= 0) e->pool[i].v48 = a->v48;
      kill(a);
      break;
    }
    case H_WALL: { /* $63A8 */
      int16_t before = a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before > (uint16_t)damage) break;
      ev->score += a->worth;
      enemy_t c = *a; /* the explosion at the 2 x 2 cells' centre */
      c.x = ((a->x >> 16) + 16) * 65536;
      c.y = ((a->y >> 16) - e->line + 16) * 65536;
      c.sprite = 0;
      explode(e, &c, true, ev);
      for (int r = 0; r < 2; r++) {
        for (int k = 0; k < 2; k++) {
          int cell = a->cell + r * 20 + k;
          if (cell >= 0 && (uint32_t)cell < e->map_rows * 20u) {
            e->map[cell] = rw(e, a->wreck + 2u * (uint32_t)(r * 2 + k));
          }
        }
      }
      kill(a);
      break;
    }
    case H_BOSS: { /* $503DA: only the eye hurts */
      if (!e->boss_awake) break;
      int16_t y = (int16_t)((a->y >> 16) - e->line + 68);
      const int16_t eye[4] = {152, y, 167, (int16_t)(y + 14)};
      if (!overlap(e->attacker, eye)) break;
      a->flags |= F_FLASH;
      int16_t before = a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before <= (uint16_t)damage) boss_destroyed(e, a, ev);
      break;
    }
    case H_SHIELD: /* $6210: a turret takes the bullet, not the damage */
      a->flags |= F_FLASH;
      break;
    case H_WORM: { /* $4FE2E, $4FE46: a worm's part bursts into pieces */
      a->flags |= F_FLASH;
      int16_t before = a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before > (uint16_t)damage) break;
      int8_t i = alloc(e);
      if (i >= 0) {
        enemy_t *b = &e->pool[i];
        push(&e->fx, e, i);
        b->type = a->type;
        b->role = R_WORM_PIECE;
        b->x = a->x & ~0xFFFF;
        b->y = a->y & ~0xFFFF;
        anim_start(e, b, a->wreck);
        b->dir = (uint8_t)rnd(e); /* the way it flies */
      }
      ev->score += a->worth;
      kill(a);
      break;
    }
    default:
      break;
  }
}

/* ---- the update of each role ---- */

static void path_step(enemies_t *e, enemy_t *a);

static void fire(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (a->type == T_DEAD || a->fire == 0) return;
  uint8_t rate = a->fire;
  while (rate >= 100) rate = (uint8_t)(rate - 100);
  unsigned sum = (unsigned)a->fire_acc + rate;
  a->fire_acc = (uint8_t)sum;
  if (sum < 256) return;
  a->fire_acc = (uint8_t)(rnd(e) & 63u);
  int dir;
  if (a->fire < 100) {
    dir = (int)(rnd(e) & 7u);
  } else {
    dir = dir_to(f->ship_x - (a->x >> 16), f->ship_y - (a->y >> 16));
  }
  shot(e, rl(e, BASE + 0x28), a->x >> 16, a->y >> 16, dir, 6);
}

static void update_path(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (!animate(e, a)) {
    remove_chain(e, a);
    return;
  }
  path_step(e, a);
  if (a->type != T_DEAD) box(e, a);
  fire(e, a, f);
}

static void update_follow(enemies_t *e, enemy_t *a) { /* $613E */
  if (!animate(e, a)) {
    remove_chain(e, a);
    return;
  }
  const enemy_t *h = &e->pool[a->head];
  a->x = (h->x & ~0xFFFF);
  a->y = (h->y & ~0xFFFF);
  box(e, a);
}

/* The arc's steps this frame ($9AEC): the steps left when it ended, or -1
 * when the frame's ran out. */
static int path_arc(enemies_t *e, enemy_t *a, int d7) {
  const uint8_t *sine = raw(e, SINE, 320);
  if (!sine) return -1;
  for (;;) {
    if (--d7 < 0) return -1;
    if (--a->count < 0) return d7 + 1;
    a->y += (int32_t)(int8_t)sine[a->dir] * 1024;
    a->x += (int32_t)(int8_t)sine[a->dir + 64] * 1024;
    uint32_t turned = (((uint32_t)a->dir << 16) | a->frac) + (uint32_t)a->rate;
    a->dir = (uint8_t)(turned >> 16);
    a->frac = (uint16_t)turned;
    a->rate += a->acc;
  }
}

static void path_step(enemies_t *e, enemy_t *a) { /* $9ACA */
  int d7 = a->speed;
  if (d7 == 0) return;
  if (a->count < 0) {
    d7 += a->count;
    if (d7 <= 0) {
      a->count = (int16_t)d7;
      return;
    }
  } else if (a->count > 0) {
    if ((d7 = path_arc(e, a, d7)) < 0) return;
  }
  for (int guard = 0; guard < 64; guard++) { /* the next opcode ($9B66) */
    switch (rw(e, a->pc)) {
      case 2:
        a->dir = (uint8_t)rw(e, a->pc + 2);
        a->frac = 0;
        a->rate = (int32_t)rs(e, a->pc + 4) * 256;
        a->acc = rs(e, a->pc + 6);
        a->count = rs(e, a->pc + 8);
        a->pc += 10;
        if ((d7 = path_arc(e, a, d7)) < 0) return;
        break;
      case 4:
        d7 -= rw(e, a->pc + 2);
        a->pc += 4;
        if (d7 <= 0) {
          a->count = (int16_t)d7;
          return;
        }
        break;
      case 6: {
        int16_t off;
        do {
          off = rs(e, a->pc + 2 + (rnd(e) & 14u));
        } while (off < 0);
        a->pc = a->start + (uint16_t)off;
        break;
      }
      case 8:
        a->pc = a->start + rw(e, a->pc + 2);
        break;
      case 10:
        a->pc += 6;
        break;
      default: /* 0: the end ($9B80) */
        if (a->group) {
          for (int g = 0; g < ENEMIES_GROUPS; g++) {
            if (e->groups[g].id == a->group) {
              e->groups[g].id = 0;
              e->groups[g].left = 0;
            }
          }
        }
        remove_chain(e, a);
        return;
    }
  }
}

static void update_drift(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                         enemies_events_t *ev) { /* $4EA0 */
  if (!animate(e, a)) {
    kill(a);
    return;
  }
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (a->v40 == 0) {
    y += 8;
    a->y = y * 65536;
    if (y >= 200) {
      payout_down(e);
      kill(a);
      return;
    }
  } else {
    if (a->v40 > 0) {
      a->v40 &= 7;
      if (a->v40 == 0) a->v42 = (int16_t)dir_to(160 - x, 100 - y);
      a->v40++;
      int32_t dx = 160 - x, dy = 100 - y;
      if (dx * dx + dy * dy < 400) a->v40 = -34;
    } else {
      a->v40++;
      if ((a->v40 & 3) == 0) a->v42 = (int16_t)((a->v42 + 1) & 7);
    }
    x += drift[a->v42 & 7][0];
    y += drift[a->v42 & 7][1];
    a->x = x * 65536;
    a->y = y * 65536;
  }
  box(e, a);
  if (!f->ship_flying || !overlap(a->box, f->ship_box)) return;
  if (a->type == T_CASH_BIG || a->type == T_CASH) { /* $4F7C */
    ev->cash += a->type == T_CASH_BIG ? 100 : 50;
    sound(ev, 3, 2);
    payout_down(e);
  } else { /* $4FF8: a token */
    sound(ev, rb(e, TOKEN_ROUTINES + 4u * (uint32_t)a->v48) ? 40 : 5, 2);
    if (ev->ntokens < ENEMIES_TOKENS) ev->tokens[ev->ntokens++] = (uint8_t)a->v48;
  }
  kill(a);
}

static void update_shot(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                        enemies_events_t *ev) { /* $4180 */
  if (!animate(e, a)) {
    kill(a);
    return;
  }
  int32_t dx = rs(e, SHOT_DX + 2u * (uint32_t)a->v42);
  int32_t dy = rs(e, SHOT_DY + 2u * (uint32_t)a->v42);
  a->x += dx * a->speed * 4;
  a->y += dy * a->speed * 4;
  a->y += f->step * 65536;
  int32_t x = a->x >> 16, y = a->y >> 16;
  if ((uint32_t)x >= 320u || (uint32_t)y >= 192u) {
    kill(a);
    return;
  }
  box(e, a);
  if (f->ship_flying && overlap(a->box, f->ship_box)) {
    ev->shield_damage += 4;
    ev->ship_flash = true;
    kill(a);
  }
}

/* Level 1's wall objects ($4F4CE, $4F7A2, $4F602): their 2 x 2 cells. */
static void wall_cells(enemies_t *e, const enemy_t *a, uint32_t frame,
                       bool both_rows) {
  for (int r = 0; r < (both_rows ? 2 : 1); r++) {
    for (int k = 0; k < 2; k++) {
      int cell = a->cell + r * 20 + k;
      if (cell >= 0 && (uint32_t)cell < e->map_rows * 20u) {
        e->map[cell] = rw(e, frame + 2u * (uint32_t)(r * 2 + k));
      }
    }
  }
}

static bool wall_gone(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if ((a->y >> 16) > f->back_limit + 208) {
    kill(a);
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
  unsigned sum = (unsigned)a->fire_acc + rb(e, rate_addr);
  a->fire_acc = (uint8_t)sum;
  if (sum < 256) return false;
  a->fire_acc = (uint8_t)(rnd(e) & 63u);
  return true;
}

static void wall_fire(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                      bool left, int dx, int dy, uint32_t shot_addr,
                      uint32_t speed_addr) {
  int32_t x = (a->x >> 16) + dx, y = (a->y >> 16) - f->line + dy;
  if (!left) x -= dx;
  shot(e, shot_sprite(e, shot_addr), x, y, left ? 2 : 6, rw(e, speed_addr));
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
    kill(a);
    return;
  }
  if (!animate(e, a)) {
    kill(a);
    return;
  }
  a->y += f->step * 65536;
  if (a->v40 == 0) return;
  int32_t d = (a->y >> 16) - f->ship_y;
  if (d <= 10 && d >= -30 && (rnd(e) & 255u) < 50u) {
    a->v40 = 0;
    a->hit = H_NONE;
    a->flags |= F_TO_FX;
    anim_start(e, a, a->type == 0xD4 ? 0x4F436u : 0x4F3F8u);
    for (int k = 2; k >= 0; k--) {
      shot(e, shot_sprite(e, L1_SHOT4), a->x >> 16, (a->y >> 16) + 7,
           (a->type == 0xD4 ? 5 : 1) + k, rw(e, L1_SPEED4));
    }
    return;
  }
  a->y += a->v40 * 65536;
  a->v48 = (int16_t)(a->v48 + a->v40);
  if (a->v48 == 0 || a->v48 == a->v42) a->v40 = (int16_t)-a->v40;
  box(e, a);
}

/* ---- level 1's worms ---- */

/* Off a list and freed at once, as $107C does (not at the next sweep). */
static void free_now(enemies_t *e, int8_t *head, int8_t i) {
  for (int8_t *link = head; *link >= 0; link = &e->pool[*link].next) {
    if (*link == i) {
      *link = e->pool[i].next;
      e->pool[i].type = 0;
      e->pool[i].next = e->free;
      e->free = i;
      return;
    }
  }
}

/* The head ($503B4): it follows the path, out of sight; at its end it goes,
 * and the parts behind it go into the end hole one a frame. */
static void update_worm_head(enemies_t *e, int8_t i, const enemies_frame_t *f) {
  enemy_t *a = &e->pool[i];
  a->y += f->step * 65536;
  e->worm_slot[a->v48] = true;
  path_step(e, a);
  if (a->flags & F_DEAD) free_now(e, &e->enemies, i);
}

/* A part ($502C2): where the object ahead of it was last frame, shown once
 * that one had started; behind the end marker, gone into the end hole. */
static void update_worm_part(enemies_t *e, int8_t i, const enemies_frame_t *f) {
  enemy_t *a = &e->pool[i];
  e->worm_slot[a->v48] = true;
  const enemy_t *n = &e->pool[a->next];
  if (n->type == T_WORM_END) {
    if (e->holes[a->v42] == 0) e->holes[a->v42] = rw(e, L1_HOLE_OPEN);
    free_now(e, &e->enemies, i);
    return;
  }
  a->x = n->x & ~0xFFFF;
  a->y = ((n->y >> 16) + f->step) * 65536;
  a->count = n->count;
  a->dir = n->dir;
  a->frac = n->frac;
  if (a->count >= 0) a->flags |= F_DRAW;
  uint32_t t = a->type == T_WORM_BODY ? L1_WORM_BODY : L1_WORM_SPRITES;
  a->sprite = rl(e, t + 4u * (uint32_t)(((a->dir + 16) >> 5) & 7));
  box(e, a);
}

/* The end marker ($4020): with no part left, both markers go. */
static void update_worm_end(enemies_t *e, int8_t i) {
  int8_t mark = e->pool[i].head;
  if (e->pool[mark].next != i) return;
  free_now(e, &e->enemies, i);
  free_now(e, &e->enemies, mark);
}

/* A part's piece ($5025A): a quarter of the sine table's step a frame in
 * its own direction, until it leaves the playfield. */
static void update_worm_piece(enemies_t *e, enemy_t *a) {
  if (!animate(e, a)) {
    kill(a);
    return;
  }
  const uint8_t *sine = raw(e, SINE, 320);
  if (!sine) return;
  int32_t x = (a->x >> 16) + ((int8_t)sine[a->dir + 64] >> 2);
  int32_t y = (a->y >> 16) + ((int8_t)sine[a->dir] >> 2);
  if ((uint32_t)x >= 320u || (uint32_t)y >= 192u) {
    kill(a);
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
  uint32_t parts = rw(e, L1_WORM_PARTS) + 1u;
  int spare = 0;
  for (int8_t i = e->free; i >= 0; i = e->pool[i].next) spare++;
  if ((uint32_t)spare < parts + 3u) return false; /* the original steals instead */
  uint32_t r = e->worm_rng[slot];
  r = r >> 5 | r << 27;
  e->worm_rng[slot] = r;
  uint32_t hole = L1_HOLES + 12u * (r & 15u);
  int32_t x = rs(e, hole), y = rs(e, hole + 2) - step - line;
  uint16_t start = rw(e, hole + 4), end = rw(e, hole + 6);
  if (start < ENEMIES_HOLES && e->holes[start] == 0) {
    e->holes[start] = rw(e, L1_HOLE_OPEN);
  }
  if (end >= ENEMIES_HOLES) end = 0;
  uint32_t path = rl(e, hole + 8);
  int8_t last = alloc(e);
  push(&e->enemies, e, last);
  int8_t h = alloc(e);
  push(&e->enemies, e, h);
  enemy_t *a = &e->pool[h];
  a->type = T_WORM_HEAD;
  a->role = R_WORM_HEAD;
  a->flags = 0;
  a->x = x * 65536;
  a->y = y * 65536;
  a->pc = a->start = path;
  a->speed = rs(e, L1_WORM_SPEED);
  a->v42 = (int16_t)end;
  a->v48 = (int16_t)slot;
  box_off(a);
  int16_t delay = 0;
  for (uint32_t k = 0; k < parts; k++) { /* 7 path steps apart */
    uint32_t rec = L1_WORM_PARTS + 2u + 12u * k;
    int8_t i = alloc(e);
    push(&e->enemies, e, i);
    enemy_t *p = &e->pool[i];
    p->type = rw(e, rec);
    p->role = R_WORM_PART;
    p->hit = H_WORM;
    p->flags = 0; /* shown once it starts */
    p->wreck = rl(e, rl(e, rec + 2) + 18u); /* its hit's lea: the pieces */
    p->sprite = rl(e, rec + 6);
    p->worth = rw(e, rec + 10);
    p->hp = 1;
    p->x = x * 65536;
    p->y = y * 65536;
    p->count = delay;
    p->v42 = (int16_t)end;
    p->v48 = (int16_t)slot;
    box(e, p);
    delay = (int16_t)(delay - 7);
  }
  int8_t mark = alloc(e);
  push(&e->enemies, e, mark);
  e->pool[mark].type = T_WORM_MARK;
  e->pool[mark].role = R_WORM_MARK;
  e->pool[mark].flags = 0;
  box_off(&e->pool[mark]);
  e->pool[last].type = T_WORM_END;
  e->pool[last].role = R_WORM_END;
  e->pool[last].flags = 0;
  e->pool[last].head = mark;
  box_off(&e->pool[last]);
  return true;
}

int32_t enemies_level_frame(enemies_t *e, int32_t line, int32_t step,
                            int32_t ship_y, int32_t *back_limit) {
  if (e->level->level != 1) return -1;
  int32_t jump = -1;
  if (!e->shop_asked && line + ship_y < rw(e, L1_SHOP_AT)) {
    e->shop_asked = true;
    jump = line = rw(e, L1_SHOP_JUMP);
    *back_limit = line;
  }
  int32_t from = rw(e, L1_WORMS_FROM), to = rw(e, L1_WORMS_TO);
  if (line >= from && line <= to) {
    if (*back_limit < to) *back_limit = to;
    for (int s = 0; s < ENEMIES_WORMS; s++) { /* $5002C */
      if (e->worm_slot[s]) continue;
      e->worm_slot[s] = spawn_worm(e, s, line, step); /* busy until its end */
    }
    for (int k = 0; k < ENEMIES_HOLES; k++) { /* the holes' cells */
      if (e->holes[k] == 0) continue;
      uint32_t cell = L1_HOLE_CELLS + 10u * (uint32_t)k;
      uint32_t word = rw(e, cell) / 2u;
      e->holes[k]--;
      if (word < e->map_rows * 20u) {
        e->map[word] = rw(e, cell + 2u + 2u * (e->holes[k] & 3u));
      }
    }
  }
  for (int s = 0; s < ENEMIES_WORMS; s++) e->worm_slot[s] = false;
  return jump;
}

/* ---- level 1's end boss ---- */

/* Its eight turrets ($505C0): the first at the enemy list's head, each next
 * after the one before, each following the one before (the first, the
 * boss). */
static void make_turrets(enemies_t *e, int8_t boss) {
  int8_t prev = boss, last = -1;
  for (int k = 7; k >= 0; k--) {
    int8_t i = alloc(e);
    if (i < 0) return;
    enemy_t *t = &e->pool[i];
    if (last < 0) {
      push(&e->enemies, e, i);
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
    box_off(t);
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
    box_off(a);
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
        if (a->speed == 0) sound(ev, 26, 1);
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
  uint32_t frame = rl(e, L1_BOSS_MOUTH + (e->frame & 12u));
  for (int k = 0; k < 8; k++) {
    e->boss_block[(5 + k / 4) * ENEMIES_BOSS_COLS + 1 + k % 4] =
        rw(e, frame + 2u * (uint32_t)k);
  }
  if (a->v40 == 0) box_off(a);
}

static void update_turret(enemies_t *e, enemy_t *a,
                          const enemies_frame_t *f) { /* $50478 */
  if (!e->boss_awake) {
    kill(a);
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
  path_step(e, a);
  if (a->speed == 0) {
    box_off(a);
    return;
  }
  if (a->type == T_TURRET_AIMS) {
    int d = dir_to(f->ship_x - (a->x >> 16), f->ship_y - (a->y >> 16));
    a->v42 = (int16_t)d;
    a->sprite = rl(e, L1_TURRET_AIM + 4u * (uint32_t)d);
    if (a->speed >= BOSS_OPEN) {
      unsigned sum = (unsigned)a->fire_acc + 15u;
      a->fire_acc = (uint8_t)sum;
      if (sum >= 256) {
        a->fire_acc = (uint8_t)(rnd(e) & 63u);
        int8_t k = shot(e, a->sprite, a->x >> 16, a->y >> 16, d, 4);
        if (k >= 0) anim_start(e, &e->pool[k], L1_TURRET_SHOT);
      }
    }
  }
  box(e, a);
}

/* The boss, as the level's start code makes it ($508A8). */
static void make_boss(enemies_t *e) {
  e->boss = -1;
  if (e->level->level != 1) return;
  int8_t i = alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  push(&e->enemies, e, i);
  a->type = T_BOSS;
  a->role = R_BOSS;
  a->hit = H_BOSS;
  a->flags = F_DRAW | F_KEEP;
  a->x = 160 * 65536;
  a->y = 16 * 65536;
  a->hp = rs(e, L1_BOSS_HP);
  box_off(a);
  for (int k = 0; k < ENEMIES_BOSS_COLS * ENEMIES_BOSS_ROWS; k++) {
    e->boss_block[k] = rw(e, L1_BOSS_BLOCK + 2u * (uint32_t)k);
  }
  e->boss = i;
}

/* ---- the frame ---- */

void enemies_init(enemies_t *e, const x2level_header_t *level,
                  const x2global_header_t *global, uint16_t *map,
                  uint32_t map_rows, int loop, uint32_t seed) {
  memset(e, 0, sizeof(*e));
  e->level = level;
  e->global = global;
  e->map = map;
  e->map_rows = map_rows;
  e->hp_small = (int16_t)loop;
  e->hp_big = (int16_t)(loop * 2);
  rng_seed(e->rng, seed);
  e->id_small = 2; /* 0 is no group */
  e->id_big = 3;
  e->id_next = 4;
  e->free = 0;
  for (int i = 0; i < ENEMIES_POOL; i++) {
    e->pool[i].next = (int8_t)(i + 1 < ENEMIES_POOL ? i + 1 : -1);
  }
  e->enemies = e->fx = -1;
  e->wave_low = e->scenery_low = 32750;
  make_boss(e);
  if (level->level == 1) { /* $508A8: the worm slots' generators */
    for (int s = 0; s < ENEMIES_WORMS; s++) {
      e->worm_rng[s] = rl(e, L1_WORM_SEEDS + 8u * (uint32_t)s);
    }
  }
}

/* $750C: every object freed but those marked to stay. */
static void clear_lists(enemies_t *e) {
  for (int8_t *head = &e->enemies; head; head = head == &e->enemies ? &e->fx : NULL) {
    for (int8_t i = *head; i >= 0; i = e->pool[i].next) {
      if (!(e->pool[i].flags & F_KEEP)) kill(&e->pool[i]);
    }
    sweep(e, head);
  }
  memset(e->groups, 0, sizeof(e->groups));
}

void enemies_restart(enemies_t *e, int32_t line) {
  clear_lists(e);
  e->wave_low = line + 1;
  e->scenery_low = line + 192;
}

/* The next object on the enemy list, past the rest of a chain ($3AF4). */
static int8_t next_enemy(const enemies_t *e, int8_t i) {
  const enemy_t *a = &e->pool[i];
  if (!(a->flags & F_CHAINED)) return a->next;
  int8_t j = e->pool[a->head].next;
  while (j >= 0 && e->pool[j].flags & F_CHAINED && e->pool[j].head == i) {
    j = e->pool[j].next;
  }
  return j;
}

void enemies_frame_start(enemies_t *e) {
  if ((++e->frame & 3u) == 0) {
    e->id_small = e->id_next;
    e->id_big = (uint16_t)(e->id_next + 1);
    e->id_next = (uint16_t)(e->id_next + 2);
  }
  e->count = 0; /* $7006: but the boss's types */
  for (int8_t i = e->enemies; i >= 0; i = next_enemy(e, i)) {
    e->count += e->pool[i].type != T_BOSS_A && e->pool[i].type != T_BOSS_B;
  }
}

void enemies_ship_body(enemies_t *e, const enemies_frame_t *f,
                       enemies_events_t *ev) { /* $38C4, $67BC */
  if (!f->ship_flying) return;
  if (f->shades) { /* $6784: the first thing in the box is hit, not the ship */
    int16_t box[4] = {(int16_t)(f->ship_x - 64), (int16_t)(f->ship_y - 64),
                      (int16_t)(f->ship_x + 64), (int16_t)(f->ship_y + 64)};
    memcpy(e->attacker, box, sizeof(e->attacker));
    for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
      enemy_t *a = &e->pool[i];
      if (!overlap(a->box, box)) continue;
      hit(e, a, 127, ev);
      return;
    }
    return;
  }
  memcpy(e->attacker, f->ship_box, sizeof(e->attacker));
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (!overlap(a->box, f->ship_box)) continue;
    ev->shield_damage += a->flags & F_BIG ? 16 : 8;
    ev->ship_flash = true;
    if (a->type != T_BOSS_A && a->type != T_BOSS_B) hit(e, a, 127, ev);
    return;
  }
}

/* The dead leave their list when its next update would run them (the
 * original's $107C): until then a dying enemy's box still takes bullets. */
void enemies_update(enemies_t *e, const enemies_frame_t *f,
                    enemies_events_t *ev) {
  e->line = f->line;
  ev->back_limit = -1;
  /* The dead go as the walk reaches them ($107C, their own update): until
   * then the objects behind them still see them. */
  for (int8_t i = e->enemies; i >= 0;) {
    enemy_t *a = &e->pool[i];
    int8_t next = a->next;
    if (a->flags & F_DEAD) {
      free_now(e, &e->enemies, i);
      i = next;
      continue;
    }
    switch (a->role) {
      case R_PATH: update_path(e, a, f); break;
      case R_FOLLOW: update_follow(e, a); break;
      case R_WALL1: update_wall1(e, a, f); break;
      case R_WALL2: update_wall2(e, a, f); break;
      case R_WALL3: update_wall3(e, a, f); break;
      case R_BOSS: update_boss(e, i, f, ev); break;
      case R_TURRET: update_turret(e, a, f); break;
      case R_WORM_HEAD: update_worm_head(e, i, f); break;
      case R_WORM_PART: update_worm_part(e, i, f); break;
      case R_WORM_END: update_worm_end(e, i); break;
      case R_CRAWLER:
        update_crawler(e, a, f);
        if (a->flags & F_TO_FX) {
          a->flags &= (uint8_t)~F_TO_FX;
          for (int8_t *link = &e->enemies; *link >= 0;
               link = &e->pool[*link].next) {
            if (*link == i) {
              *link = a->next;
              break;
            }
          }
          push(&e->fx, e, i);
        }
        break;
      default: break;
    }
    i = next;
  }
}

bool enemies_shoot(enemies_t *e, int32_t x, int32_t y, int damage,
                   enemies_events_t *ev) { /* $3950 */
  e->attacker[0] = e->attacker[2] = (int16_t)x;
  e->attacker[1] = e->attacker[3] = (int16_t)y;
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (x < a->box[0] || x > a->box[2] || y < a->box[1] || y > a->box[3]) continue;
    hit(e, a, damage, ev); /* a dying one only stops the bullet */
    return true;
  }
  return false;
}

/* The groups none of whose members is left ($3E9C). */
static void sweep_groups(enemies_t *e) {
  for (int g = 0; g < ENEMIES_GROUPS; g++) {
    if (e->groups[g].id == 0) continue;
    bool found = false;
    for (int8_t i = e->enemies; i >= 0 && !found; i = e->pool[i].next) {
      found = e->pool[i].group == e->groups[g].id;
    }
    if (!found) e->groups[g].id = 0;
  }
}

void enemies_update_fx(enemies_t *e, const enemies_frame_t *f,
                       enemies_events_t *ev) {
  e->line = f->line;
  sweep(e, &e->fx);
  for (int8_t i = e->fx; i >= 0;) {
    enemy_t *a = &e->pool[i];
    int8_t next = a->next;
    if (!(a->flags & F_DEAD)) {
      switch (a->role) {
        case R_EXPLOSION:
          if (!animate(e, a)) kill(a);
          break;
        case R_DRIFT: update_drift(e, a, f, ev); break;
        case R_SHOT: update_shot(e, a, f, ev); break;
        case R_CRAWLER: update_crawler(e, a, f); break;
        case R_WORM_PIECE: update_worm_piece(e, a); break;
        default: break;
      }
    }
    i = next;
  }
  sweep_groups(e);
}

/* ---- spawning ---- */

static void group_add(enemies_t *e, uint16_t id, uint16_t count) {
  for (int g = 0; g < ENEMIES_GROUPS; g++) {
    if (e->groups[g].id == id) {
      e->groups[g].left = (uint16_t)(e->groups[g].left + count);
      return;
    }
  }
  for (int g = 0; g < ENEMIES_GROUPS; g++) {
    if (e->groups[g].id == 0) {
      e->groups[g].id = id;
      e->groups[g].left = count;
      e->id_next++;
      return;
    }
  }
}

/* The generic spawner ($3B18): type record `type`, wave record `rec`. */
static int8_t spawn_wave(enemies_t *e, uint32_t type, uint32_t rec) {
  int count = rs(e, rec + 4), spacing = rs(e, rec + 8);
  int16_t d6 = 0;
  int8_t last = -1;
  uint32_t paths = rl(e, BASE + 0x30);
  uint32_t path = rl(e, paths + 4u * (uint32_t)(rs(e, rec + 6) - 1));
  if (path < rl(e, BASE + 0x34) || path >= rl(e, BASE + 0x38)) return -1;
  for (int m = 0; m < count; m++) {
    int8_t head = -1, prev = -1;
    for (uint32_t part = type; rw(e, part) != 0; part += 24) {
      int8_t i = alloc(e);
      if (i < 0) return last;
      enemy_t *a = &e->pool[i];
      if (prev < 0) {
        push(&e->enemies, e, i);
        head = i;
      } else {
        a->next = e->pool[prev].next;
        e->pool[prev].next = i;
        e->pool[prev].chain = i;
      }
      a->head = head;
      a->type = rw(e, part);
      a->worth = rw(e, part + 2);
      a->role = rl(e, part + 4) == UPDATE_FOLLOW ? R_FOLLOW : R_PATH;
      a->hit = rl(e, part + 12) == HIT_CHAIN ? H_CHAIN : H_SINGLE;
      anim_start(e, a, rl(e, part + 16));
      a->x = (int32_t)d6 * 65536;
      a->count = 0;
      if (spacing < 100) {
        a->count = (int16_t)-d6;
        a->x = 0;
      }
      if (i != head) a->count = (int16_t)(a->count - rs(e, part + 2));
      a->start = path;
      a->pc = path;
      if (rw(e, path) == 10) {
        a->x = (int32_t)rs(e, path + 2) * 65536;
        a->y = (int32_t)rs(e, path + 4) * 65536;
        if (spacing >= 100) a->x += (int32_t)d6 * 65536;
        a->pc = path + 6;
      }
      a->speed = rs(e, rec + 12);
      a->fire = (uint8_t)rw(e, rec + 10);
      a->fire_acc = (uint8_t)rnd(e);
      bool big = rb(e, part + 22) != 0;
      a->hp = big ? e->hp_big : e->hp_small;
      a->group = big ? e->id_big : e->id_small;
      if (big) a->flags |= F_BIG;
      if (rb(e, part + 23)) a->flags |= F_CHAINED;
      box(e, a);
      prev = i;
      last = i;
    }
    int step = spacing;
    while (step >= 100) step -= 100;
    d6 = (int16_t)(d6 + step);
  }
  if (last >= 0) {
    group_add(e, e->pool[last].flags & F_BIG ? e->id_big : e->id_small,
              (uint16_t)count);
  }
  return last;
}

static void spawn_capsule(enemies_t *e, uint32_t rec) { /* $3AC0 */
  int8_t i = alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  push(&e->enemies, e, i);
  a->type = T_CAPSULE;
  a->role = R_PATH;
  a->hit = H_CAPSULE;
  anim_start(e, a, CAPSULE_ANIM);
  uint32_t path = rl(e, rl(e, BASE + 0x30) + 4u * (uint32_t)(rs(e, rec + 6) - 1));
  a->start = a->pc = path;
  if (rw(e, path) == 10) {
    a->x = (int32_t)rs(e, path + 2) * 65536;
    a->y = (int32_t)rs(e, path + 4) * 65536;
    a->pc = path + 6;
  }
  a->v48 = rs(e, rec + 12); /* the token */
  a->speed = 3;
  a->fire = (uint8_t)rw(e, rec + 10);
  a->fire_acc = (uint8_t)rnd(e);
  a->hp = e->hp_small;
  group_add(e, e->id_small, (uint16_t)rs(e, rec + 4));
  box(e, a);
}

static void spawn_type(enemies_t *e, int kind, uint32_t rec) {
  if (kind == 0) {
    spawn_capsule(e, rec);
    return;
  }
  /* the wave handler's jump table: kind k at +10 + 4k, a `lea abs.l,a2` */
  if (kind < 0 || kind > 10) return;
  uint32_t handler = BASE + 0x12u + (uint32_t)(int32_t)rs(e, BASE + 0x12);
  uint32_t stub = rl(e, handler + 10u + 4u * (uint32_t)kind);
  if (rw(e, stub) != 0x45F9) return;
  spawn_wave(e, rl(e, stub + 2), rec);
}

static void spawn_wall(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                       int kind) { /* level 1's $4FBEC, $4FB60, $4FC64 */
  int8_t i = alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  push(&e->enemies, e, i);
  bool right = rw(e, rec + 12) != 0;
  int32_t x = rs(e, rec + 8) - 8, y = rs(e, rec + 10) - 8;
  a->cell = (int16_t)((y >> 4) * 20 + (x >> 4));
  a->x = x * 65536;
  a->y = y * 65536;
  a->hit = H_WALL;
  a->flags = 0; /* drawn in the map */
  a->worth = 100;
  a->box[0] = a->box[1] = a->box[2] = a->box[3] = 1000;
  uint32_t init;
  if (kind == 2) { /* the dispatch at $4FB4C: kind 1 $4FBEC, kind 2 $4FB60 */
    a->type = right ? 0xE0 : 0xE4;
    a->role = R_WALL1;
    a->hp = rs(e, L1_HP1);
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
    a->hp = rs(e, L1_HP2);
    a->tiles = a->type == 0xE8 ? 0x4F8ACu : 0x4F88Cu;
    init = right ? 0x4F8ACu : 0x4F88Cu;
    a->wreck = right ? 0x4F8CCu : 0x4F8D4u;
  } else {
    a->type = right ? 0xD8 : 0xDC;
    a->role = R_WALL3;
    a->hp = rs(e, L1_HP3);
    a->tiles = a->type == 0xD8 ? 0x4F74Au : 0x4F6FAu;
    init = right ? 0x4FCDCu : 0x4FCD4u;
    a->wreck = 0x4F79Au;
  }
  wall_cells(e, a, init, true);
  (void)f;
}

static void spawn_crawler(enemies_t *e, const enemies_frame_t *f,
                          uint32_t rec) { /* level 1's $4FCE4 */
  int8_t i = alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  push(&e->enemies, e, i);
  bool right = rw(e, rec + 12) != 0;
  a->type = right ? 0xD4 : 0xD0;
  a->role = R_CRAWLER;
  a->hit = H_SINGLE;
  anim_start(e, a, right ? 0x4F412u : 0x4F3D4u);
  a->hp = rs(e, L1_HP4);
  a->flags |= F_BIG;
  a->worth = 250;
  int32_t x = rs(e, rec + 8) - 8 + (right ? 16 : 0);
  a->x = x * 65536;
  a->y = (rs(e, rec + 10) - 8 - f->line) * 65536;
  a->v40 = -1;
  a->v42 = a->v48 = (int16_t)(rs(e, rec + 6) << 4);
  box(e, a);
}

static void spawn_scenery(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                          int kind, enemies_events_t *ev) {
  if (kind == 0) { /* a restart point ($3D86) */
    /* Of several passed in one frame (only a jump of the world passes more
     * than one), the last on the way: level 2's table does not list them
     * in order. */
    if (ev->restart && rs(e, rec) > ev->restart_line) return;
    ev->restart = true;
    ev->restart_x = rs(e, rec + 8);
    ev->restart_line = rs(e, rec);
    return;
  }
  if (e->level->level != 1) return; /* other levels' own code: to come */
  if (kind >= 1 && kind <= 3) spawn_wall(e, f, rec, kind);
  if (kind == 4) spawn_crawler(e, f, rec);
}

static void scan(enemies_t *e, const enemies_frame_t *f, uint32_t table,
                 int32_t *low, bool waves, enemies_events_t *ev) { /* $3DC2 */
  for (uint32_t r = table;; r += 14) {
    int16_t line = rs(e, r);
    if (line < 0 || !raw(e, r, 14)) break;
    if (*low <= line || f->line > line) continue;
    if (waves) {
      spawn_type(e, rs(e, r + 2), r);
    } else {
      spawn_scenery(e, f, r, rs(e, r + 2), ev);
    }
  }
  if (f->line < *low) *low = f->line;
}

void enemies_spawn(enemies_t *e, const enemies_frame_t *f,
                   enemies_events_t *ev) {
  scan(e, f, rl(e, BASE + 0x38), &e->wave_low, true, ev);
  scan(e, f, rl(e, BASE + 0x14), &e->scenery_low, false, ev);
}

void enemies_bomb(enemies_t *e, enemies_events_t *ev) { /* $51D2 */
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (a->flags & F_DEAD || a->type == T_BOSS_A || a->type == T_BOSS_B) continue;
    hit(e, a, 127, ev);
  }
}

static void draw_list(enemies_t *e, int8_t head, int32_t line,
                      enemies_draw_fn draw, enemies_block_fn block, void *ctx) {
  for (int8_t i = head; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    bool flash = a->flags & F_FLASH;
    a->flags &= (uint8_t)~F_FLASH; /* a hit shows for one drawing */
    if (!(a->flags & F_DRAW) || a->flags & F_DEAD) continue;
    if (a->role == R_BOSS) { /* $5081A: the block, then the eye */
      if (line >= 640) continue;
      int32_t y = (a->y >> 16) - line;
      block(ctx, e->boss_block, ENEMIES_BOSS_COLS, ENEMIES_BOSS_ROWS,
            ENEMIES_BOSS_X, y, flash);
      if (flash) continue;
      const x2_sprite_t *eye =
          sprite(e, rl(e, L1_BOSS_EYES + ((e->eye >> 4) & 12u)));
      if (eye) {
        draw(ctx, eye, (const uint8_t *)e->level + eye->offset, 152, y + 66,
             false);
      }
      continue;
    }
    const x2_sprite_t *s = sprite(e, a->sprite);
    if (!s) continue;
    const uint8_t *base = a->sprite >= BASE ? (const uint8_t *)e->level
                                            : (const uint8_t *)e->global;
    draw(ctx, s, base + s->offset, a->x >> 16, a->y >> 16, flash);
  }
}

void enemies_draw(enemies_t *e, int32_t line, enemies_draw_fn draw,
                  enemies_block_fn block, void *ctx) {
  draw_list(e, e->enemies, line, draw, block, ctx);
  draw_list(e, e->fx, line, draw, block, ctx);
}

bool enemies_level_won(const enemies_t *e) { return e->won && e->paid; }

void enemies_level_unwon(enemies_t *e) {
  e->won = e->paid = false;
  e->payout = 0;
}

bool enemies_ship_safe(const enemies_t *e) { return e->payout > 0; }

bool enemies_hit_first(enemies_t *e, const int16_t box[4], int damage,
                       enemies_events_t *ev) {
  memcpy(e->attacker, box, sizeof(e->attacker));
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (!overlap(a->box, box)) continue;
    hit(e, a, damage, ev);
    return true;
  }
  return false;
}

void enemies_hit_all(enemies_t *e, const int16_t box[4], int damage,
                     enemies_events_t *ev) {
  memcpy(e->attacker, box, sizeof(e->attacker));
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (!overlap(a->box, box)) continue;
    hit(e, a, damage, ev);
    if (e->payout > 0) return;
  }
}

static bool on_screen(const enemy_t *a) {
  return a->box[2] >= 0 && a->box[0] < 320 && a->box[3] >= 0 && a->box[1] < 192;
}

int enemies_pick_target(enemies_t *e) { /* $2C68 */
  int n = rnd_mod(e, e->count ? e->count : 1);
  for (int pass = 0; pass < 5; pass++) { /* round the list five times */
    for (int8_t i = e->enemies; i >= 0; i = next_enemy(e, i)) {
      if (on_screen(&e->pool[i]) && n-- == 0) return i;
    }
  }
  return -1;
}

bool enemies_target_box(const enemies_t *e, int id, int16_t box[4]) {
  if (id < 0 || id >= ENEMIES_POOL) return false;
  const enemy_t *a = &e->pool[id];
  if (a->type < 200 || a->flags & F_DEAD || !on_screen(a)) return false;
  memcpy(box, a->box, sizeof(a->box));
  return true;
}

void enemies_explosion(enemies_t *e, int32_t x, int32_t y, bool big) {
  int8_t i = alloc(e);
  if (i < 0) return;
  enemy_t *b = &e->pool[i];
  b->type = T_EXPLOSION;
  b->role = R_EXPLOSION;
  b->x = x * 65536;
  b->y = y * 65536;
  anim_start(e, b, big ? BOOM_BIG_ANIM : BOOM_ANIM);
  push(&e->fx, e, i);
}

void enemies_burst(enemies_t *e, int32_t x, int32_t y, int w, int h, int n,
                   bool big, enemies_events_t *ev) { /* $4A98 */
  for (int k = 0; k < n; k++) {
    int32_t ex = rnd_mod(e, w) + x;
    int32_t ey = rnd_mod(e, h) + y;
    enemies_explosion(e, ex, ey, big);
  }
  sound(ev, 15, 2);
  sound(ev, 15, 1);
  sound(ev, 15, 0);
}

uint32_t enemies_random(enemies_t *e) { return rnd(e); }

int enemies_direction(int dx, int dy) { return dir_to(dx, dy); }

int enemies_count(const enemies_t *e) {
  int n = 0;
  for (int i = 0; i < ENEMIES_POOL; i++) n += e->pool[i].type != 0;
  return n;
}
