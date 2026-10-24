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
 * - Scenery records: kind 0 a restart point ($3D86); the others, the
 *   objects the level's own code makes for them (its handler at +$18).
 * - Each level's own code (enemies_level.h: level1.c, ...): its scenery,
 *   its every-frame code (+$24), what its start makes (+$20), its objects'
 *   updates, hits and drawing.
 * Objects marked to stay (the original's +96: a level's bosses) outlive a
 * restart ($750C).
 */

#include "enemies.h"

#include <stddef.h>
#include <string.h>

#include "enemies_level.h"
#include "rng.h"

#define UPDATE_PATH 0xE1Eu  /* the type records' routines (jump vectors) */
#define UPDATE_FOLLOW 0xE36u
#define HIT_CHAIN 0xE30u

/* The game image. */
#define SHOT_DX 0x4140u
#define SHOT_DY 0x4160u
#define TOKEN_TABLE 0x3F96u
#define TOKEN_ROUTINES 0x5040u
#define CAPSULE_ANIM 0x2D30u

/* Each level's own code; none for a level not ported yet. */
static const enemies_level_t k_none;
static const enemies_level_t *code(const enemies_t *e) {
  switch (e->level->level) {
    case 1: return &enemies_level1;
    case 2: return &enemies_level2;
    case 3: return &enemies_level3;
    case 4: return &enemies_level4;
    case 5: return &enemies_level5;
    default: return &k_none;
  }
}

/* Cash and tokens: 2 pixels in one of 8 directions ($4FD8). */
static const int8_t drift[8][2] = {{0, -2}, {2, -2}, {2, 0},  {2, 2},
                                   {0, 2},  {-2, 2}, {-2, 0}, {-2, -2}};

/* ---- the original's memory ---- */

const uint8_t *en_raw(const enemies_t *e, uint32_t addr, uint32_t len) {
  return addr >= BASE ? x2level_raw(e->level, addr, len)
                      : x2global_raw(e->global, addr, len);
}

uint16_t en_rw(const enemies_t *e, uint32_t addr) {
  const uint8_t *p = en_raw(e, addr, 2);
  return p ? (uint16_t)((p[0] << 8) | p[1]) : 0;
}

int16_t en_rs(const enemies_t *e, uint32_t addr) {
  return (int16_t)en_rw(e, addr);
}

uint32_t en_rl(const enemies_t *e, uint32_t addr) {
  return ((uint32_t)en_rw(e, addr) << 16) | en_rw(e, addr + 2);
}

uint8_t en_rb(const enemies_t *e, uint32_t addr) {
  const uint8_t *p = en_raw(e, addr, 1);
  return p ? *p : 0;
}

const x2_sprite_t *en_sprite(const enemies_t *e, uint32_t source) {
  if (source >= BASE) {
    return x2sprite_find(x2level_sprites(e->level), (int)e->level->sprite_count,
                         source);
  }
  return x2sprite_find(x2global_sprites(e->global), (int)e->global->sprite_count,
                       source);
}

/* ---- the game's random generator ($27FE) ---- */

uint32_t en_rnd(enemies_t *e) { return rng_next(e->rng); }

/* A random number below n ($28A4). */
int en_rnd_mod(enemies_t *e, int n) {
  return (int)(((en_rnd(e) >> 2) & 0xFFFFu) % (uint32_t)n);
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

int en_dir_to(int dx, int dy) {
  if (dy >= 0) return dir_half(dx, dy);
  return (4 - dir_half(dx, -dy)) & 7;
}

/* ---- the lists ---- */

int8_t en_alloc(enemies_t *e) {
  int8_t i = e->free;
  if (i < 0) return -1;
  e->free = e->pool[i].next;
  memset(&e->pool[i], 0, sizeof(enemy_t));
  e->pool[i].next = e->pool[i].head = e->pool[i].chain = -1;
  e->pool[i].flags = F_DRAW;
  return i;
}

void en_push(int8_t *head, enemies_t *e, int8_t i) {
  e->pool[i].next = *head;
  *head = i;
}

/* At a list's end (the original's tail pointers, $BAA). */
void en_append(int8_t *head, enemies_t *e, int8_t i) {
  int8_t *link = head;
  while (*link >= 0) link = &e->pool[*link].next;
  e->pool[i].next = -1;
  *link = i;
}

void en_box_off(enemy_t *a) {
  a->box[0] = a->box[1] = a->box[2] = a->box[3] = 1000;
}

void en_kill(enemy_t *a) {
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

void en_sound(enemies_events_t *ev, int effect, int channel) {
  if (ev->nsounds < ENEMIES_SOUNDS) {
    ev->sounds[ev->nsounds].effect = (uint8_t)effect;
    ev->sounds[ev->nsounds].channel = (uint8_t)channel;
    ev->nsounds++;
  }
}

/* ---- animation ($34C2) ---- */

void en_anim_start(enemies_t *e, enemy_t *a, uint32_t list) {
  a->sprite = en_rl(e, list);
  a->frames = en_rs(e, list + 4);
  a->anim = list + 6;
}

/* False when the animation ended (the object is gone). */
bool en_animate(enemies_t *e, enemy_t *a) {
  if (--a->frames < 0) {
    a->frames = 0;
    return true;
  }
  if (a->frames != 0) return true;
  uint32_t s = en_rl(e, a->anim);
  if (s) {
    a->sprite = s;
    a->frames = en_rs(e, a->anim + 4);
    a->anim += 6;
    return true;
  }
  uint32_t routine = en_rl(e, a->anim + 4);
  if (routine == X2SPRITE_LOOP || routine == X2SPRITE_LOOP_AT) {
    en_anim_start(e, a, en_rl(e, a->anim + 8));
    return true;
  }
  return false; /* $34FA: the end */
}

/* The end of an animation or a path: the object, or its whole chain. */
void en_remove_chain(enemies_t *e, enemy_t *a) {
  if (!(a->flags & F_CHAINED) || a->head < 0) {
    en_kill(a);
    return;
  }
  for (int8_t i = a->head; i >= 0;) {
    int8_t n = e->pool[i].chain;
    en_kill(&e->pool[i]);
    i = n;
  }
}

/* ---- boxes ($3D58) ---- */

bool enemies_box(const enemies_t *e, uint32_t source, int32_t x, int32_t y,
                 int16_t box[4]) {
  const x2_sprite_t *s = en_sprite(e, source);
  if (!s) return false;
  box[0] = (int16_t)(x - s->hot_x + s->box_x);
  box[1] = (int16_t)(y - s->hot_y + s->box_y);
  box[2] = (int16_t)(box[0] + s->box_w - 1);
  box[3] = (int16_t)(box[1] + s->box_h - 1);
  return true;
}

void en_box(const enemies_t *e, enemy_t *a) {
  if (!enemies_box(e, a->sprite, a->x >> 16, a->y >> 16, a->box)) {
    a->box[0] = a->box[1] = 1000;
    a->box[2] = a->box[3] = 1000;
  }
}

bool en_overlap(const int16_t *a, const int16_t *b) {
  return a[2] >= b[0] && a[0] <= b[2] && a[3] >= b[1] && a[1] <= b[3];
}

/* ---- objects made on the way ---- */

/* An explosion at the centre of `a`'s sprite ($6250, $3A5E). */
void en_explode(enemies_t *e, const enemy_t *a, bool big,
                    enemies_events_t *ev) {
  en_sound(ev, big ? 15 : 44, 1);
  const x2_sprite_t *s = en_sprite(e, a->sprite);
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (s) {
    x = (2 * (x - s->hot_x) + s->span) >> 1;
    y = (2 * (y - s->hot_y) + s->height - 1) >> 1;
  }
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *b = &e->pool[i];
  b->type = T_EXPLOSION;
  b->role = R_EXPLOSION;
  b->x = x * 65536;
  b->y = y * 65536;
  en_anim_start(e, b, big ? BOOM_BIG_ANIM : BOOM_ANIM);
  en_push(&e->fx, e, i);
}

/* Cash or a token, drifting from (x, y) ($62AC, $3EEC). */
static int8_t drifter(enemies_t *e, uint16_t type, uint32_t list, int32_t x,
                      int32_t y) {
  int8_t i = en_alloc(e);
  if (i < 0) return -1;
  enemy_t *b = &e->pool[i];
  b->type = type;
  b->role = R_DRIFT;
  b->x = x;
  b->y = y;
  b->v40 = 7;
  b->v42 = (int16_t)en_rnd(e);
  en_anim_start(e, b, list);
  en_push(&e->fx, e, i);
  return i;
}

/* An enemy's shot ($39FE): the sprite at `source`, direction 0-15. */
int8_t en_shot(enemies_t *e, uint32_t source, int32_t x, int32_t y,
                   int dir, int speed) {
  int8_t i = en_alloc(e);
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
  en_push(&e->fx, e, i);
  return i;
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
void en_destroyed(enemies_t *e, enemy_t *a, enemies_events_t *ev) {
  en_explode(e, a, a->flags & F_BIG, ev);
  group_down(e, a, ev);
  ev->score += a->worth;
  en_kill(a);
}

/* $2996: a boss's payout, `pairs` times a 50 and a 100 at random places,
 * at the effects list's end. */
void en_payout(enemies_t *e, int pairs) {
  for (int k = 0; k < pairs; k++) {
    for (int big = 0; big < 2; big++) {
      int8_t i = en_alloc(e);
      if (i < 0) break;
      enemy_t *b = &e->pool[i];
      b->type = big ? T_CASH_BIG : T_CASH;
      b->role = R_DRIFT;
      en_anim_start(e, b, big ? CASH_BIG_ANIM : CASH_ANIM);
      b->x = (en_rnd_mod(e, 300) + 10) * 65536;
      b->y = (en_rnd_mod(e, 180) + 6) * 65536;
      b->v40 = 7;
      b->v42 = (int16_t)en_rnd(e);
      en_append(&e->fx, e, i);
      e->payout++;
    }
  }
}

/* A bubble of the boss's payout taken or gone ($4EE0, $4FA2). */
static void payout_down(enemies_t *e) {
  if (e->payout > 0 && --e->payout == 0) {
    e->paid = true;
    if (!e->won) e->shop_wanted = true; /* a mid boss's: the mid shop */
  }
}


void en_hit(enemies_t *e, enemy_t *a, int damage, enemies_events_t *ev) {
  switch (a->hit) {
    case H_SINGLE: {
      int16_t before = a->hp;
      a->flags |= F_FLASH;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before <= (uint16_t)damage) en_destroyed(e, a, ev);
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
        if (!(e->pool[i].flags & F_CHAINED)) en_explode(e, &e->pool[i], head->flags & F_BIG, ev);
        en_kill(&e->pool[i]);
        i = n;
      }
      en_destroyed(e, head, ev);
      break;
    }
    case H_CAPSULE: { /* $3EEC: its token */
      uint32_t t = TOKEN_TABLE + 6u * (uint32_t)a->v48;
      uint16_t type = en_rw(e, t);
      en_sound(ev, en_rb(e, TOKEN_ROUTINES + 4u * type) ? 16 : 9, 2);
      int8_t i = drifter(e, type, en_rl(e, t + 2), a->x, a->y);
      if (i >= 0) e->pool[i].v48 = a->v48;
      en_kill(a);
      break;
    }
    case H_WALL: { /* $63A8 */
      int16_t before = a->hp;
      a->hp = (int16_t)(a->hp - damage);
      if ((uint16_t)before > (uint16_t)damage) break;
      ev->score += a->worth;
      enemy_t c = *a; /* the explosion at the cells' centre */
      c.x = ((a->x >> 16) + 8 * a->cols) * 65536;
      c.y = ((a->y >> 16) - e->line + 8 * a->rows) * 65536;
      c.sprite = 0;
      en_explode(e, &c, true, ev);
      uint32_t w = a->wreck; /* the wreck's words, row by row */
      for (int r = 0; r < a->rows; r++) {
        for (int k = 0; k < a->cols; k++, w += 2) {
          int cell = a->cell + r * 20 + k;
          if (cell >= 0 && (uint32_t)cell < e->map_rows * 20u) e->map[cell] = en_rw(e, w);
        }
      }
      en_kill(a);
      break;
    }
    case H_SHIELD: /* $6210: a turret takes the bullet, not the damage */
      a->flags |= F_FLASH;
      e->attacker_gone = true;
      break;
    default:
      if (a->hit >= H_LEVEL && code(e)->hit) code(e)->hit(e, a, damage, ev);
      break;
  }
}

/* ---- the update of each role ---- */

void en_fire(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (a->type == T_DEAD || a->fire == 0) return;
  uint8_t rate = a->fire;
  while (rate >= 100) rate = (uint8_t)(rate - 100);
  unsigned sum = (unsigned)a->fire_acc + rate;
  a->fire_acc = (uint8_t)sum;
  if (sum < 256) return;
  a->fire_acc = (uint8_t)(en_rnd(e) & 63u);
  int dir;
  if (a->fire < 100) {
    dir = (int)(en_rnd(e) & 7u);
  } else {
    dir = en_dir_to(f->ship_x - (a->x >> 16), f->ship_y - (a->y >> 16));
  }
  en_shot(e, en_rl(e, BASE + 0x28), a->x >> 16, a->y >> 16, dir, 6);
}

void en_update_path(enemies_t *e, enemy_t *a, const enemies_frame_t *f) {
  if (!en_animate(e, a)) {
    en_remove_chain(e, a);
    return;
  }
  en_path_step(e, a);
  if (a->type != T_DEAD) en_box(e, a);
  en_fire(e, a, f);
}

static void update_follow(enemies_t *e, enemy_t *a) { /* $613E */
  if (!en_animate(e, a)) {
    en_remove_chain(e, a);
    return;
  }
  const enemy_t *h = &e->pool[a->head];
  a->x = (h->x & ~0xFFFF);
  a->y = (h->y & ~0xFFFF);
  en_box(e, a);
}

/* The arc's steps this frame ($9AEC): the steps left when it ended, or -1
 * when the frame's ran out. */
static int path_arc(enemies_t *e, enemy_t *a, int d7) {
  const uint8_t *sine = en_raw(e, SINE, 320);
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

void en_path_step(enemies_t *e, enemy_t *a) { /* $9ACA */
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
    switch (en_rw(e, a->pc)) {
      case 2:
        a->dir = (uint8_t)en_rw(e, a->pc + 2);
        a->frac = 0;
        a->rate = (int32_t)en_rs(e, a->pc + 4) * 256;
        a->acc = en_rs(e, a->pc + 6);
        a->count = en_rs(e, a->pc + 8);
        a->pc += 10;
        if ((d7 = path_arc(e, a, d7)) < 0) return;
        break;
      case 4:
        d7 -= en_rw(e, a->pc + 2);
        a->pc += 4;
        if (d7 <= 0) {
          a->count = (int16_t)d7;
          return;
        }
        break;
      case 6: {
        int16_t off;
        do {
          off = en_rs(e, a->pc + 2 + (en_rnd(e) & 14u));
        } while (off < 0);
        a->pc = a->start + (uint16_t)off;
        break;
      }
      case 8:
        a->pc = a->start + en_rw(e, a->pc + 2);
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
        en_remove_chain(e, a);
        return;
    }
  }
}

static void update_drift(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                         enemies_events_t *ev) { /* $4EA0 */
  if (!en_animate(e, a)) {
    en_kill(a);
    return;
  }
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (a->v40 == 0) {
    y += 8;
    a->y = y * 65536;
    if (y >= 200) {
      payout_down(e);
      en_kill(a);
      return;
    }
  } else {
    if (a->v40 > 0) {
      a->v40 &= 7;
      if (a->v40 == 0) a->v42 = (int16_t)en_dir_to(160 - x, 100 - y);
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
  en_box(e, a);
  if (!f->ship_flying || !en_overlap(a->box, f->ship_box)) return;
  if (a->type == T_CASH_BIG || a->type == T_CASH) { /* $4F7C */
    ev->cash += a->type == T_CASH_BIG ? 100 : 50;
    en_sound(ev, 3, 2);
    payout_down(e);
  } else { /* $4FF8: a token */
    en_sound(ev, en_rb(e, TOKEN_ROUTINES + 4u * (uint32_t)a->v48) ? 40 : 5, 2);
    if (ev->ntokens < ENEMIES_TOKENS) ev->tokens[ev->ntokens++] = (uint8_t)a->v48;
  }
  en_kill(a);
}

void en_update_shot(enemies_t *e, enemy_t *a, const enemies_frame_t *f,
                        enemies_events_t *ev) { /* $4180 */
  if (!en_animate(e, a)) {
    en_kill(a);
    return;
  }
  int32_t dx = en_rs(e, SHOT_DX + 2u * (uint32_t)a->v42);
  int32_t dy = en_rs(e, SHOT_DY + 2u * (uint32_t)a->v42);
  a->x += dx * a->speed * 4;
  a->y += dy * a->speed * 4;
  a->y += f->step * 65536;
  int32_t x = a->x >> 16, y = a->y >> 16;
  if ((uint32_t)x >= 320u || (uint32_t)y >= 192u) {
    en_kill(a);
    return;
  }
  en_box(e, a);
  if (f->ship_flying && en_overlap(a->box, f->ship_box)) {
    ev->shield_damage += 4;
    ev->ship_flash = true;
    en_kill(a);
  }
}


/* ---- level 1's worms ---- */

/* Off a list and freed at once, as $107C does (not at the next sweep). */
void en_free_now(enemies_t *e, int8_t *head, int8_t i) {
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
  e->enemies = e->fx = e->cells = -1;
  e->wave_low = e->scenery_low = 32750;
  e->boss = -1;
  if (code(e)->start) code(e)->start(e); /* $4F020 */
}

int32_t enemies_level_frame(enemies_t *e, int32_t line, int32_t step,
                            int32_t ship_y, int32_t *back_limit) { /* $4F024 */
  if (!code(e)->frame) return -1;
  return code(e)->frame(e, line, step, ship_y, back_limit);
}

/* $750C: every object freed but those marked to stay. */
static void clear_lists(enemies_t *e) {
  int8_t *lists[] = {&e->enemies, &e->fx, &e->cells};
  for (int l = 0; l < 3; l++) {
    int8_t *head = lists[l];
    for (int8_t i = *head; i >= 0; i = e->pool[i].next) {
      if (!(e->pool[i].flags & F_KEEP)) en_kill(&e->pool[i]);
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
      if (!en_overlap(a->box, box)) continue;
      en_hit(e, a, 127, ev);
      return;
    }
    if (code(e)->cells_hit) code(e)->cells_hit(e, box, f->line, ev);
    return;
  }
  memcpy(e->attacker, f->ship_box, sizeof(e->attacker));
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (!en_overlap(a->box, f->ship_box)) continue;
    ev->shield_damage += a->flags & F_BIG ? 16 : 8;
    ev->ship_flash = true;
    if (a->type != T_BOSS_A && a->type != T_BOSS_B) en_hit(e, a, 127, ev);
    return;
  }
  if (code(e)->cells_hit && code(e)->cells_hit(e, f->ship_box, f->line, ev)) {
    ev->shield_damage += 8; /* $4F00C's stand-in object is small */
    ev->ship_flash = true;
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
      en_free_now(e, &e->enemies, i);
      i = next;
      continue;
    }
    switch (a->role) {
      case R_PATH: en_update_path(e, a, f); break;
      case R_FOLLOW: update_follow(e, a); break;
      default:
        if (a->role >= R_LEVEL && code(e)->update) code(e)->update(e, i, f, ev);
        if (a->flags & F_TO_FX) { /* moved to the other list */
          a->flags &= (uint8_t)~F_TO_FX;
          for (int8_t *link = &e->enemies; *link >= 0;
               link = &e->pool[*link].next) {
            if (*link == i) {
              *link = a->next;
              break;
            }
          }
          en_push(&e->fx, e, i);
        }
        break;
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
    en_hit(e, a, damage, ev); /* a dying one only stops the bullet */
    return true;
  }
  if (!code(e)->cells_hit) return false; /* $399C */
  const int16_t point[4] = {(int16_t)x, (int16_t)y, (int16_t)x, (int16_t)y};
  return code(e)->cells_hit(e, point, e->line, ev);
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
          if (!en_animate(e, a)) en_kill(a);
          break;
        case R_DRIFT: update_drift(e, a, f, ev); break;
        case R_SHOT: en_update_shot(e, a, f, ev); break;
        default:
          if (a->role >= R_LEVEL && code(e)->update_fx) code(e)->update_fx(e, i, f, ev);
          break;
      }
    }
    i = next;
  }
  /* $C10: the level's objects in the map, freed as the walk reaches them */
  for (int8_t i = e->cells; i >= 0;) {
    enemy_t *a = &e->pool[i];
    int8_t next = a->next;
    if (a->flags & F_DEAD) {
      en_free_now(e, &e->cells, i);
    } else if (a->role >= R_LEVEL && code(e)->update_cells) {
      code(e)->update_cells(e, i, f, ev);
    }
    i = next;
  }
  sweep_groups(e);
}

/* ---- spawning ---- */

/* A group of one for an enemy made on its own ($CEC, $43C): its id, kept
 * by the enemy even when the groups are all taken. */
uint16_t en_group_new(enemies_t *e) {
  uint16_t id = e->id_next++;
  for (int g = 0; g < ENEMIES_GROUPS; g++) {
    if (e->groups[g].id == 0) {
      e->groups[g].id = id;
      e->groups[g].left = 1;
      return id;
    }
  }
  return id;
}

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
  int count = en_rs(e, rec + 4), spacing = en_rs(e, rec + 8);
  int16_t d6 = 0;
  int8_t last = -1;
  uint32_t paths = en_rl(e, BASE + 0x30);
  uint32_t path = en_rl(e, paths + 4u * (uint32_t)(en_rs(e, rec + 6) - 1));
  if (path < en_rl(e, BASE + 0x34) || path >= en_rl(e, BASE + 0x38)) return -1;
  for (int m = 0; m < count; m++) {
    int8_t head = -1, prev = -1;
    for (uint32_t part = type; en_rw(e, part) != 0; part += 24) {
      int8_t i = en_alloc(e);
      if (i < 0) return last;
      enemy_t *a = &e->pool[i];
      if (prev < 0) {
        en_push(&e->enemies, e, i);
        head = i;
      } else {
        a->next = e->pool[prev].next;
        e->pool[prev].next = i;
        e->pool[prev].chain = i;
      }
      a->head = head;
      a->type = en_rw(e, part);
      a->worth = en_rw(e, part + 2);
      uint32_t update = en_rl(e, part + 4);
      uint8_t own = e->wave_role ? e->wave_role(update) : 0;
      a->role = own ? own : update == UPDATE_FOLLOW ? R_FOLLOW : R_PATH;
      a->hit = en_rl(e, part + 12) == HIT_CHAIN ? H_CHAIN : H_SINGLE;
      en_anim_start(e, a, en_rl(e, part + 16));
      a->x = (int32_t)d6 * 65536;
      a->count = 0;
      if (spacing < 100) {
        a->count = (int16_t)-d6;
        a->x = 0;
      }
      if (i != head) a->count = (int16_t)(a->count - en_rs(e, part + 2));
      a->start = path;
      a->pc = path;
      if (en_rw(e, path) == 10) {
        a->x = (int32_t)en_rs(e, path + 2) * 65536;
        a->y = (int32_t)en_rs(e, path + 4) * 65536;
        if (spacing >= 100) a->x += (int32_t)d6 * 65536;
        a->pc = path + 6;
      }
      a->speed = en_rs(e, rec + 12);
      a->fire = (uint8_t)en_rw(e, rec + 10);
      a->fire_acc = (uint8_t)en_rnd(e);
      bool big = en_rb(e, part + 22) != 0;
      a->hp = big ? e->hp_big : e->hp_small;
      a->group = big ? e->id_big : e->id_small;
      if (big) a->flags |= F_BIG;
      if (en_rb(e, part + 23)) a->flags |= F_CHAINED;
      en_box(e, a);
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
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *a = &e->pool[i];
  en_push(&e->enemies, e, i);
  a->type = T_CAPSULE;
  a->role = R_PATH;
  a->hit = H_CAPSULE;
  en_anim_start(e, a, CAPSULE_ANIM);
  uint32_t path = en_rl(e, en_rl(e, BASE + 0x30) + 4u * (uint32_t)(en_rs(e, rec + 6) - 1));
  a->start = a->pc = path;
  if (en_rw(e, path) == 10) {
    a->x = (int32_t)en_rs(e, path + 2) * 65536;
    a->y = (int32_t)en_rs(e, path + 4) * 65536;
    a->pc = path + 6;
  }
  a->v48 = en_rs(e, rec + 12); /* the token */
  a->speed = 3;
  a->fire = (uint8_t)en_rw(e, rec + 10);
  a->fire_acc = (uint8_t)en_rnd(e);
  a->hp = e->hp_small;
  group_add(e, e->id_small, (uint16_t)en_rs(e, rec + 4));
  en_box(e, a);
}

static void spawn_type(enemies_t *e, int kind, uint32_t rec) {
  if (kind == 0) {
    spawn_capsule(e, rec);
    return;
  }
  /* the wave handler's jump table: kind k at +10 + 4k, a `lea abs.l,a2` */
  if (kind < 0 || kind > 10) return;
  uint32_t handler = BASE + 0x12u + (uint32_t)(int32_t)en_rs(e, BASE + 0x12);
  uint32_t stub = en_rl(e, handler + 10u + 4u * (uint32_t)kind);
  /* A kind that sets the big enemies' hit points around its spawn
   * (`move.w $426,-(sp); move.w abs.l,$426; lea; jsr $E72; move.w
   * (sp)+,$426`): the spawn with them. */
  int16_t big = e->hp_big;
  bool around = en_rw(e, stub) == 0x3F38 && en_rw(e, stub + 4) == 0x31F9 &&
                en_rw(e, stub + 10) == 0x0426;
  if (around) {
    e->hp_big = en_rs(e, en_rl(e, stub + 6));
    stub += 12;
  }
  if (en_rw(e, stub) == 0x45F9) spawn_wave(e, en_rl(e, stub + 2), rec);
  e->hp_big = big;
}


static void spawn_scenery(enemies_t *e, const enemies_frame_t *f, uint32_t rec,
                          int kind, enemies_events_t *ev) {
  if (kind == 0) { /* a restart point ($3D86) */
    /* Of several passed in one frame (only a jump of the world passes more
     * than one), the last on the way: level 2's table does not list them
     * in order. */
    if (ev->restart && en_rs(e, rec) > ev->restart_line) return;
    ev->restart = true;
    ev->restart_x = en_rs(e, rec + 8);
    ev->restart_line = en_rs(e, rec);
    return;
  }
  if (code(e)->scenery) code(e)->scenery(e, f, rec, kind, ev); /* $4F018 */
}

static void scan(enemies_t *e, const enemies_frame_t *f, uint32_t table,
                 int32_t *low, bool waves, enemies_events_t *ev) { /* $3DC2 */
  for (uint32_t r = table;; r += 14) {
    int16_t line = en_rs(e, r);
    if (line < 0 || !en_raw(e, r, 14)) break;
    if (*low <= line || f->line > line) continue;
    if (waves) {
      spawn_type(e, en_rs(e, r + 2), r);
    } else {
      spawn_scenery(e, f, r, en_rs(e, r + 2), ev);
    }
  }
  if (f->line < *low) *low = f->line;
}

void enemies_spawn(enemies_t *e, const enemies_frame_t *f,
                   enemies_events_t *ev) {
  if (e->tables_closed) return;
  scan(e, f, en_rl(e, BASE + 0x38), &e->wave_low, true, ev);
  scan(e, f, en_rl(e, BASE + 0x14), &e->scenery_low, false, ev);
}

void enemies_bomb(enemies_t *e, enemies_events_t *ev) { /* $51D2 */
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (a->flags & F_DEAD || a->type == T_BOSS_A || a->type == T_BOSS_B) continue;
    en_hit(e, a, 127, ev);
  }
}

static void draw_list(enemies_t *e, int8_t head, int32_t line,
                      enemies_draw_fn draw, enemies_draw_fn behind,
                      enemies_block_fn block, void *ctx) {
  for (int8_t i = head; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    bool flash = a->flags & F_FLASH;
    a->flags &= (uint8_t)~F_FLASH; /* a hit shows for one drawing */
    if (!(a->flags & F_DRAW) || a->flags & F_DEAD) continue;
    if (a->role >= R_LEVEL && code(e)->draw &&
        code(e)->draw(e, a, line, flash, draw, block, ctx)) {
      continue;
    }
    const x2_sprite_t *s = en_sprite(e, a->sprite);
    if (!s) continue;
    const uint8_t *base = a->sprite >= BASE ? (const uint8_t *)e->level
                                            : (const uint8_t *)e->global;
    enemies_draw_fn fn = a->flags & F_BEHIND && behind ? behind : draw;
    fn(ctx, s, base + s->offset, a->x >> 16, a->y >> 16, flash);
  }
}

void enemies_draw(enemies_t *e, int32_t line, enemies_draw_fn draw,
                  enemies_draw_fn behind, enemies_block_fn block, void *ctx) {
  draw_list(e, e->enemies, line, draw, behind, block, ctx);
  draw_list(e, e->fx, line, draw, behind, block, ctx);
  draw_list(e, e->cells, line, draw, behind, block, ctx);
}

void enemies_set_strip(enemies_t *e, enemies_strip_fn strip) {
  e->strip = strip;
}

void en_strip(const enemies_t *e, void *ctx, int32_t x, int32_t y,
              int32_t height, const uint8_t *colors, int width) {
  if (e->strip) e->strip(ctx, x, y, height, colors, width);
}

bool enemies_shop_wanted(enemies_t *e) {
  bool w = e->shop_wanted;
  e->shop_wanted = false;
  return w;
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
  e->attacker_gone = false;
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (!en_overlap(a->box, box)) continue;
    en_hit(e, a, damage, ev);
    return true;
  }
  return code(e)->cells_hit && code(e)->cells_hit(e, box, e->line, ev); /* $3942 */
}

bool enemies_hit_all(enemies_t *e, const int16_t box[4], int damage,
                     enemies_events_t *ev) {
  memcpy(e->attacker, box, sizeof(e->attacker));
  e->attacker_gone = false;
  for (int8_t i = e->enemies; i >= 0; i = e->pool[i].next) {
    enemy_t *a = &e->pool[i];
    if (!en_overlap(a->box, box)) continue;
    en_hit(e, a, damage, ev);
    if (e->attacker_gone) return true;
    if (e->payout > 0) return false;
  }
  if (code(e)->cells_hit) code(e)->cells_hit(e, box, e->line, ev); /* $3942 */
  return false;
}

static bool on_screen(const enemy_t *a) {
  return a->box[2] >= 0 && a->box[0] < 320 && a->box[3] >= 0 && a->box[1] < 192;
}

int enemies_pick_target(enemies_t *e) { /* $2C68 */
  int n = en_rnd_mod(e, e->count ? e->count : 1);
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
  int8_t i = en_alloc(e);
  if (i < 0) return;
  enemy_t *b = &e->pool[i];
  b->type = T_EXPLOSION;
  b->role = R_EXPLOSION;
  b->x = x * 65536;
  b->y = y * 65536;
  en_anim_start(e, b, big ? BOOM_BIG_ANIM : BOOM_ANIM);
  en_push(&e->fx, e, i);
}

void enemies_burst(enemies_t *e, int32_t x, int32_t y, int w, int h, int n,
                   bool big, enemies_events_t *ev) { /* $4A98 */
  for (int k = 0; k < n; k++) {
    int32_t ex = en_rnd_mod(e, w) + x;
    int32_t ey = en_rnd_mod(e, h) + y;
    enemies_explosion(e, ex, ey, big);
  }
  en_sound(ev, 15, 2);
  en_sound(ev, 15, 1);
  en_sound(ev, 15, 0);
}

uint32_t enemies_random(enemies_t *e) { return en_rnd(e); }

int enemies_direction(int dx, int dy) { return en_dir_to(dx, dy); }

int enemies_count(const enemies_t *e) {
  int n = 0;
  for (int i = 0; i < ENEMIES_POOL; i++) n += e->pool[i].type != 0;
  return n;
}
