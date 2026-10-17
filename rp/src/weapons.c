/**
 * File: weapons.c
 * Description: The player's weapons (weapons.h), after the original's code
 *              (addresses in the game image):
 * - the tokens and the shop's makers: forward shot $5332, double $53DE,
 *   flamer $56AA, side $549A, rear $5A3C, bomb $555E, homing $55FC, drone
 *   $579C, electro ball $584A, mines $58E2 / $598E, cannon $5C70, laser
 *   $5BC2, missile launcher $5B16, the power-up $5126; a remade weapon by
 *   its slot's type, $508C;
 * - their updates: $5DC8, $43B8, $4402, $5DE8, $5E18, $5E5A, $5F0C, $454A,
 *   $469A, $4BAC, $4902, $47A8, $49C8; the scatter while the ship dives or
 *   dies, $712E;
 * - the projectiles: bullets $6990 / $6086, flames $5FF2, the bomb $4B12,
 *   homing missiles $4210, the laser's beam $4848, the cannon's shell $4E50
 *   and overlay $44E8, the mines (the layer's copies, $4BAC); the drone's
 *   dots $6A1C;
 * - the death $657E, the restart points' save $3DA2 and the remake $755E.
 * The weapons' numbers and animations are read in the shared image from
 * where the game keeps them; the damages from its table at $7AEA.
 */

#include "weapons.h"

#include <stddef.h>
#include <string.h>

/* Roles. */
enum { W_FORWARD, W_DOUBLE, W_FLAMER, W_SIDE, W_REAR, W_BOMB, W_HOMING,
       W_DRONE, W_ELECTRO, W_MINES, W_CANNON, W_LASER, W_LAUNCHER,
       /* projectiles */
       P_BULLET, P_FLAME, P_BOMB, P_MISSILE, P_BEAM, P_SHELL, P_OVERLAY,
       P_MINE, P_RING };
#define F_DEAD 0x01
#define F_HIDDEN 0x02
#define F_TESTS 0x04 /* the flame of a pair that collides */

/* The slots' types ($C66...). */
#define T_FORWARD 148
#define T_DOUBLE 192
#define T_FLAMER 176
#define T_SIDE 152
#define T_REAR 68
#define T_BOMB 40
#define T_HOMING 72
#define T_DRONE 44
#define T_ELECTRO 76
#define T_MINE 56
#define T_MINE_BIG 60
#define T_CANNON 52
#define T_LASER 32
#define T_LAUNCHER 144
/* Projectiles' types (for "one alive" tests). */
#define T_BULLET 16
#define T_BOMB_SHELL 40
#define T_MISSILE 72

#define SLOT_GUN 0
#define SLOT_POD 1 /* 1-4 */
#define SLOT_UNDER 5
#define SLOT_EXTRA 6

/* The game image */
#define POD_PLACES 0x2BD0u
#define DAMAGES 0x7AEAu
#define DOTS 0x45C8u
#define HOMING_TURN 0x3108u  /* + 30 x direction */
#define HOMING_START 0x3102u /* + 30 x direction */
#define HOMING_DY 0x4330u
#define HOMING_DX 0x4340u
#define LASER_TAILS 0x6CA2u
#define LASER_TIPS 0x6CAEu
#define LASER_PATTERNS 0x6E22u
#define ANIM_DOUBLE 0x2BC3Eu /* a sprite, not a list */
#define ANIM_FLAMER 0x2D0Cu
#define ANIM_FLAME 0x2F86u
#define ANIM_REAR 0x302Au
#define ANIM_DRONE 0x2D8Au
#define ANIM_ELECTRO 0x31F2u
#define ANIM_MINE 0x30A2u
#define ANIM_MINE_BIG 0x30C0u
#define ANIM_CANNON 0x2FFAu
#define ANIM_CANNON_FIRE 0x2FE2u
#define ANIM_CANNON_TOP 0x30DEu
#define ANIM_SHELL 0x307Eu
#define ANIM_LASER 0x2DDEu
#define ANIM_LAUNCHER 0x348Cu
#define ANIM_MISSILE 0x3108u
#define ANIM_BOMB 0x2DAEu
#define ANIM_RING 0x2D6Cu
#define CANNON_REST 0x29B5Au
#define CANNON_RECOIL 0x29CBAu
#define LAUNCHER_REST 0x2AC60u
#define LAUNCHER_FIRE 0x2AD10u
#define BIG_BURST 0x2F2Au

/* The damages ($7AEA...), by index. */
enum { D_HOMING, D_ELECTRO, D_SHELL, D_UNUSED, D_DOT, D_BULLET, D_FLAME,
       D_LASER, D_BOMB, D_MINE };

/* ---- the game image ---- */

static uint16_t gw(const weapons_t *w, uint32_t addr) {
  const uint8_t *p = x2global_raw(w->global, addr, 2);
  return p ? (uint16_t)((p[0] << 8) | p[1]) : 0;
}

static int16_t gs(const weapons_t *w, uint32_t addr) {
  return (int16_t)gw(w, addr);
}

static uint32_t gl(const weapons_t *w, uint32_t addr) {
  return ((uint32_t)gw(w, addr) << 16) | gw(w, addr + 2);
}

static int damage(const weapons_t *w, int which) {
  return gs(w, DAMAGES + 2u * (uint32_t)which);
}

static const x2_sprite_t *sprite(const weapons_t *w, uint32_t source) {
  return x2sprite_find(x2global_sprites(w->global),
                       (int)w->global->sprite_count, source);
}

/* ---- the pool ---- */

static int8_t alloc(weapons_t *w) {
  int8_t i = w->free;
  if (i < 0) return -1;
  w->free = w->pool[i].next;
  memset(&w->pool[i], 0, sizeof(weapon_t));
  w->pool[i].next = -1;
  w->pool[i].slot = -1;
  w->pool[i].owner = -1;
  w->pool[i].target = -1;
  w->pool[i].bitmap = -1;
  w->pool[i].v94 = -1;
  return i;
}

static void push(int8_t *head, weapons_t *w, int8_t i) {
  w->pool[i].next = *head;
  *head = i;
}

static void append(int8_t *head, weapons_t *w, int8_t i) {
  int8_t *link = head;
  while (*link >= 0) link = &w->pool[*link].next;
  w->pool[i].next = -1;
  *link = i;
}

static void kill(weapon_t *a) { a->flags |= F_DEAD; }

static void sweep(weapons_t *w, int8_t *head) {
  int8_t *link = head;
  while (*link >= 0) {
    weapon_t *a = &w->pool[*link];
    if (a->flags & F_DEAD) {
      int8_t i = *link;
      *link = a->next;
      a->type = 0;
      a->next = w->free;
      w->free = i;
    } else {
      link = &a->next;
    }
  }
}

/* ---- animation, boxes ($34C2, $3D58) ---- */

static void anim_start(weapons_t *w, weapon_t *a, uint32_t list) {
  a->sprite = gl(w, list);
  a->frames = gs(w, list + 4);
  a->anim = list + 6;
}

static bool animate(weapons_t *w, weapon_t *a) {
  if (--a->frames < 0) {
    a->frames = 0;
    return true;
  }
  if (a->frames != 0) return true;
  uint32_t s = gl(w, a->anim);
  if (s) {
    a->sprite = s;
    a->frames = gs(w, a->anim + 4);
    a->anim += 6;
    return true;
  }
  uint32_t routine = gl(w, a->anim + 4);
  if (routine == X2SPRITE_LOOP || routine == X2SPRITE_LOOP_AT) {
    anim_start(w, a, gl(w, a->anim + 8));
    return true;
  }
  return false;
}

static void box(const weapons_t *w, weapon_t *a) {
  const x2_sprite_t *s = sprite(w, a->sprite);
  int32_t x = a->x >> 16, y = a->y >> 16;
  if (!s) {
    a->box[0] = a->box[1] = a->box[2] = a->box[3] = 1000;
    return;
  }
  a->box[0] = (int16_t)(x - s->hot_x + s->box_x);
  a->box[1] = (int16_t)(y - s->hot_y + s->box_y);
  a->box[2] = (int16_t)(a->box[0] + s->box_w - 1);
  a->box[3] = (int16_t)(a->box[1] + s->box_h - 1);
}

static void sound(enemies_events_t *ev, int effect, int channel) {
  if (ev->nsounds < ENEMIES_SOUNDS) {
    ev->sounds[ev->nsounds].effect = (uint8_t)effect;
    ev->sounds[ev->nsounds].channel = (uint8_t)channel;
    ev->nsounds++;
  }
}

/* ---- the slots ---- */

static weapon_t *slot_obj(weapons_t *w, int slot) {
  int8_t i = w->slots[slot].obj;
  return w->slots[slot].type && i >= 0 ? &w->pool[i] : NULL;
}

/* A weapon freed, its slot emptied. */
static void free_slot(weapons_t *w, int slot) {
  weapon_t *a = slot_obj(w, slot);
  if (a) kill(a);
  w->slots[slot].type = 0;
  w->slots[slot].obj = -1;
}

/* A new weapon object in `slot` at the weapons list's head. */
static weapon_t *make(weapons_t *w, int slot, uint16_t slot_type,
                      uint16_t type, int role, int16_t max) {
  int8_t i = alloc(w);
  if (i < 0) return NULL;
  weapon_t *a = &w->pool[i];
  push(&w->weapons, w, i);
  w->slots[slot].type = slot_type;
  w->slots[slot].obj = i;
  a->type = type;
  a->role = (uint8_t)role;
  a->slot = (int8_t)slot;
  a->max = max;
  a->x = w->ship_x * 65536;
  a->y = w->ship_y * 65536;
  return a;
}

/* Power + 1 up to its max ($53CA). */
static void power_up(weapon_t *a) {
  if (a && a->power < a->max) a->power++;
}

static void make_forward(weapons_t *w) { /* $5332 */
  free_slot(w, SLOT_GUN);
  make(w, SLOT_GUN, T_FORWARD, T_FORWARD, W_FORWARD, 2);
  w->fire_period = 8;
}

/* A main gun: the same one powers up, another replaces it. */
static void main_gun(weapons_t *w, uint16_t type, int role, uint32_t anim) {
  if (w->slots[SLOT_GUN].type == type) {
    power_up(slot_obj(w, SLOT_GUN));
    return;
  }
  free_slot(w, SLOT_GUN);
  weapon_t *a = make(w, SLOT_GUN, type, type, role, 2);
  w->fire_period = 8;
  if (!a) return;
  if (anim == ANIM_DOUBLE) {
    a->sprite = ANIM_DOUBLE;
  } else {
    anim_start(w, a, anim);
  }
}

/* The weapon under the ship (slot 5): the same powers up, another is
 * replaced ($5836, $576C), made `below` lines under the ship ($5812). */
static weapon_t *under(weapons_t *w, uint16_t type, int role, uint32_t anim,
                       int16_t max, int16_t below) {
  uint16_t now = w->slots[SLOT_UNDER].type;
  bool same = now == type || ((now == T_MINE || now == T_MINE_BIG) &&
                              (type == T_MINE || type == T_MINE_BIG));
  if (same) {
    power_up(slot_obj(w, SLOT_UNDER));
    return NULL;
  }
  free_slot(w, SLOT_UNDER);
  weapon_t *a = make(w, SLOT_UNDER, type, type, role, max);
  if (!a) return NULL;
  a->y = (w->ship_y + below) * 65536;
  anim_start(w, a, anim);
  return a;
}

/* A pod in the first free slot 1-4 ($2BBC), placed by the ship ($2BD0). */
static weapon_t *pod(weapons_t *w, uint16_t type, int role, uint32_t anim,
                     int16_t max) {
  for (int k = 0; k < 4; k++) {
    int slot = SLOT_POD + k;
    if (w->slots[slot].type) continue;
    weapon_t *a = make(w, slot, type, type, role, max);
    if (!a) return NULL;
    uint32_t place = POD_PLACES + 4u * (uint32_t)(3 - k);
    a->ox = gs(w, place);
    a->oy = gs(w, place + 2);
    a->x = (w->ship_x + a->ox) * 65536;
    a->y = (w->ship_y + a->oy) * 65536;
    anim_start(w, a, anim);
    return a;
  }
  return NULL;
}

static void make_weapon(weapons_t *w, uint16_t type) { /* $508C */
  weapon_t *a;
  switch (type) {
    case T_FORWARD: make_forward(w); break;
    case T_DOUBLE: main_gun(w, T_DOUBLE, W_DOUBLE, ANIM_DOUBLE); break;
    case T_FLAMER: main_gun(w, T_FLAMER, W_FLAMER, ANIM_FLAMER); break;
    case T_REAR: /* $5A3C: a side shot goes */
      if (w->slots[SLOT_EXTRA].type == T_SIDE) free_slot(w, SLOT_EXTRA);
      under(w, T_REAR, W_REAR, ANIM_REAR, 2, 0);
      break;
    case T_SIDE: /* $549A */
      if (w->slots[SLOT_EXTRA].type == T_SIDE) {
        power_up(slot_obj(w, SLOT_EXTRA));
        break;
      }
      if (w->slots[SLOT_EXTRA].type) break; /* a bomb or homing holds it */
      if (w->slots[SLOT_UNDER].type == T_REAR) free_slot(w, SLOT_UNDER);
      make(w, SLOT_EXTRA, T_SIDE, T_SIDE, W_SIDE, 2);
      w->fire_period = 8;
      break;
    case T_BOMB: /* $555E */
      if (w->slots[SLOT_EXTRA].type == T_BOMB) {
        power_up(slot_obj(w, SLOT_EXTRA));
        break;
      }
      free_slot(w, SLOT_EXTRA);
      make(w, SLOT_EXTRA, T_BOMB, 0x9C, W_BOMB, 0);
      w->fire_period = 12;
      break;
    case T_HOMING: { /* $55FC */
      if (w->slots[SLOT_UNDER].type == T_HOMING) {
        power_up(slot_obj(w, SLOT_UNDER));
        break;
      }
      if (w->slots[SLOT_EXTRA].type == T_HOMING) {
        power_up(slot_obj(w, SLOT_EXTRA));
        break;
      }
      int slot = !w->slots[SLOT_UNDER].type   ? SLOT_UNDER
                 : !w->slots[SLOT_EXTRA].type ? SLOT_EXTRA
                                              : SLOT_EXTRA;
      free_slot(w, slot);
      make(w, slot, T_HOMING, 0xA0, W_HOMING, 0);
      w->fire_period = 30;
      break;
    }
    case T_DRONE: under(w, T_DRONE, W_DRONE, ANIM_DRONE, 2, 25); break;
    case T_ELECTRO: under(w, T_ELECTRO, W_ELECTRO, ANIM_ELECTRO, 0, 25); break;
    case T_MINE:
    case T_MINE_BIG:
      a = under(w, type, W_MINES, type == T_MINE ? ANIM_MINE : ANIM_MINE_BIG, 1,
                25);
      if (a && type == T_MINE_BIG) a->power = 1;
      w->drop_x = w->drop_y = -100;
      break;
    case T_CANNON:
      a = pod(w, T_CANNON, W_CANNON, ANIM_CANNON, 0);
      if (a && a->ox) { /* its top, on the effects list's tail ($5D1A) */
        int8_t i = alloc(w);
        if (i >= 0) {
          weapon_t *t = &w->pool[i];
          t->type = 0x40;
          t->role = P_OVERLAY;
          t->owner = (int8_t)(a - w->pool);
          anim_start(w, t, ANIM_CANNON_TOP);
          append(&w->shots, w, i);
        }
      }
      break;
    case T_LASER: pod(w, T_LASER, W_LASER, ANIM_LASER, 2); break;
    case T_LAUNCHER: pod(w, T_LAUNCHER, W_LAUNCHER, ANIM_LAUNCHER, 0); break;
    default: return;
  }
  if (!w->slots[SLOT_GUN].type) make_forward(w); /* $5D76 */
}

void weapons_make(weapons_t *w, uint16_t type) { make_weapon(w, type); }

void weapons_take(weapons_t *w, int token) {
  static const uint16_t types[TOKEN_COUNT] = {
      [TOKEN_REAR] = T_REAR,       [TOKEN_SIDE] = T_SIDE,
      [TOKEN_CANNON] = T_CANNON,   [TOKEN_DRONE] = T_DRONE,
      [TOKEN_LASER] = T_LASER,     [TOKEN_ELECTRO] = T_ELECTRO,
      [TOKEN_MINE] = T_MINE,       [TOKEN_LAUNCHER] = T_LAUNCHER,
      [TOKEN_HOMING] = T_HOMING,   [TOKEN_FLAMER] = T_FLAMER,
      [TOKEN_BOMB] = T_BOMB};
  if (token == TOKEN_INVULNERABLE) { /* $52CA: the ring, if none */
    if (w->invulnerable == 0) {
      int8_t i = alloc(w);
      if (i >= 0) {
        weapon_t *r = &w->pool[i];
        r->type = 0x5C;
        r->role = P_RING;
        anim_start(w, r, ANIM_RING);
        push(&w->shots, w, i);
      }
    }
    w->invulnerable = (int16_t)(w->invulnerable + WEAPONS_INVULNERABLE);
    return;
  }
  if (token == TOKEN_POWERUP) { /* $5126: the weakest below its max */
    weapon_t *best = NULL;
    for (int s = 0; s < WEAPONS_SLOTS; s++) {
      weapon_t *a = slot_obj(w, s);
      if (a && a->power < a->max && (!best || a->power < best->power)) best = a;
    }
    power_up(best);
    return;
  }
  if (token >= 0 && token < TOKEN_COUNT && types[token]) {
    make_weapon(w, types[token]);
  }
}

/* ---- the game, deaths and restart points ---- */

void weapons_init(weapons_t *w, const x2global_header_t *global,
                  enemies_t *enemies) {
  memset(w, 0, sizeof(*w));
  w->global = global;
  w->enemies = enemies;
  for (int i = 0; i < WEAPONS_POOL; i++) {
    w->pool[i].next = (int8_t)(i + 1 < WEAPONS_POOL ? i + 1 : -1);
  }
  w->free = 0;
  w->weapons = w->shots = -1;
  for (int s = 0; s < WEAPONS_SLOTS; s++) w->slots[s].obj = -1;
  for (int d = 0; d < WEAPONS_DOTS; d++) w->dots[d].x = -1;
  w->fire_period = 8;
}

void weapons_game_start(weapons_t *w) {
  make_forward(w);
  weapons_save(w);
}

int16_t weapons_invulnerable(const weapons_t *w) { return w->invulnerable; }

void weapons_save(weapons_t *w) { /* $3DA2 */
  for (int s = 0; s < WEAPONS_SLOTS; s++) {
    weapon_t *a = slot_obj(w, s);
    w->saved.type[s] = w->slots[s].type;
    w->saved.power[s] = a ? a->power : 0;
  }
}

void weapons_death(weapons_t *w) { /* $657E */
  for (int8_t i = w->weapons; i >= 0; i = w->pool[i].next) {
    weapon_t *a = &w->pool[i];
    if (a->flags & F_DEAD) continue;
    if (a->type == T_FORWARD && a->power == 0) continue;
    if (a->slot >= 0) free_slot(w, a->slot);
    return;
  }
}

void weapons_restore(weapons_t *w) { /* $750C, $755E */
  for (int8_t *head = &w->weapons; head; head = head == &w->weapons ? &w->shots : NULL) {
    for (int8_t i = *head; i >= 0; i = w->pool[i].next) kill(&w->pool[i]);
    sweep(w, head);
  }
  for (int s = 0; s < WEAPONS_SLOTS; s++) {
    w->slots[s].type = 0;
    w->slots[s].obj = -1;
  }
  for (int d = 0; d < WEAPONS_DOTS; d++) w->dots[d].x = -1;
  w->mines = 0;
  w->flaming = false;
  /* $CC8 is kept but its ring, which counts it down, is gone: crushed while
   * invulnerable, the ship stays so for the rest of the game, as in the
   * original (only a new game clears it, $73B4). */
  for (int s = 0; s < WEAPONS_SLOTS; s++) {
    if (!w->saved.type[s]) continue;
    make_weapon(w, w->saved.type[s]);
    /* the power back on the weapon just made (the head of the list) */
    if (w->weapons >= 0) w->pool[w->weapons].power = w->saved.power[s];
  }
}

int16_t weapons_fire_period(const weapons_t *w) { return w->fire_period; }

uint16_t weapons_slot_type(const weapons_t *w, int slot) {
  return slot >= 0 && slot < WEAPONS_SLOTS ? w->slots[slot].type : 0;
}

int weapons_slot_power(const weapons_t *w, int slot) {
  if (slot < 0 || slot >= WEAPONS_SLOTS || !w->slots[slot].type) return 0;
  return w->pool[w->slots[slot].obj].power;
}

/* ---- projectiles ---- */

/* A bullet ($6990): (dx, dy) a frame from the ship + (ox, oy), its sprite
 * by power from one of the four tables; effect 2 if nothing waits on
 * channel 2 (the caller's sfx_request_if_free). */
static void bullet(weapons_t *w, const weapon_t *src, const weapons_frame_t *f,
                   int dx, int dy, int ox, int oy, int first_id,
                   enemies_events_t *ev) {
  bool asked = false;
  for (int k = 0; k < ev->nsounds; k++) asked |= ev->sounds[k].channel == 2;
  if (!asked) sound(ev, 2, 2);
  int8_t i = alloc(w);
  if (i < 0) return;
  weapon_t *b = &w->pool[i];
  b->type = T_BULLET;
  b->role = P_BULLET;
  b->dx = dx * 65536;
  b->dy = dy * 65536;
  b->x = (f->x + ox) * 65536;
  b->y = (f->y + oy) * 65536;
  b->power = src->power;
  int p = src->power > 2 ? 2 : src->power;
  b->bitmap = (int8_t)(first_id + p);
  push(&w->shots, w, i);
}

static bool alive_of(const weapons_t *w, uint16_t type) {
  for (int8_t i = w->shots; i >= 0; i = w->pool[i].next) {
    if (!(w->pool[i].flags & F_DEAD) && w->pool[i].type == type) return true;
  }
  return false;
}

/* ---- the weapons' updates ---- */

/* While the ship dives or dies ($712E): flung out from its centre, hidden
 * at 16. */
static void scatter(weapons_t *w, weapon_t *a, const weapons_frame_t *f) {
  if (f->scatter >= 16) {
    a->flags |= F_HIDDEN;
    return;
  }
  a->flags &= (uint8_t)~F_HIDDEN;
  const x2_sprite_t *s = sprite(w, a->sprite);
  if (!s) return;
  int32_t cx = (2 * ((a->x >> 16) - s->hot_x) + s->span) >> 1;
  int32_t cy = (2 * ((a->y >> 16) - s->hot_y) + s->height - 1) >> 1;
  cx = f->ship_centre_x + (cx - f->ship_centre_x) * f->scatter;
  cy = f->ship_centre_y + (cy - f->ship_centre_y) * f->scatter;
  a->x = cx * 65536;
  a->y = cy * 65536;
}

static void at_ship(weapon_t *a, const weapons_frame_t *f, int ox, int oy) {
  a->x = (f->x + ox) * 65536;
  a->y = (f->y + oy) * 65536;
}

static void update_weapon(weapons_t *w, weapon_t *a, const weapons_frame_t *f,
                          enemies_events_t *ev) {
  switch (a->role) {
    case W_FORWARD: /* $5DC8 */
      if (!f->diving && f->shot) {
        bullet(w, a, f, 0, -9, 0, -6, X2G_BULLET0, ev);
      }
      return;
    case W_SIDE: /* $5DE8 */
      if (!f->diving && f->shot) {
        bullet(w, a, f, -9, 0, -2, -3, X2G_BULLET_LEFT0, ev);
        bullet(w, a, f, 9, 0, 2, -3, X2G_BULLET_RIGHT0, ev);
      }
      return;
    case W_DOUBLE: /* $43B8 */
      at_ship(a, f, 0, 0);
      if (f->scatter) break;
      if (f->shot) {
        bullet(w, a, f, 0, -9, -5, -12, X2G_BULLET0, ev);
        bullet(w, a, f, 0, -9, 6, -12, X2G_BULLET0, ev);
      }
      return;
    case W_REAR: /* $5E18 */
      animate(w, a);
      at_ship(a, f, 0, 0);
      if (f->scatter) break;
      if (f->shot) {
        a->frames = 1; /* the recoil */
        bullet(w, a, f, 0, 9, 0, 18, X2G_BULLET_DOWN0, ev);
      }
      return;
    case W_FLAMER: { /* $4402 */
      animate(w, a);
      at_ship(a, f, 0, 0);
      if (f->scatter) break;
      if (!f->held) {
        if (a->v40) {
          w->flame_stop = true; /* the driver's $2CF88 */
          a->v40 = 0;
          w->flaming = false;
        }
        return;
      }
      if (a->v40 == 0 || f->flame_sound_done) {
        sound(ev, 25, 2);
        a->v40 = 1;
        w->flaming = true;
      }
      for (int k = 1; k >= 0; k--) {
        int8_t i = alloc(w);
        if (i < 0) return;
        weapon_t *fl = &w->pool[i];
        push(&w->shots, w, i);
        fl->type = 0xB4;
        fl->role = P_FLAME;
        fl->x = (a->x & ~0xFFFF) + 0x8000;
        int32_t y = (a->y >> 16) + (int32_t)(enemies_random(w->enemies) & 3u) - 16;
        if (k == 0) y -= 6;
        fl->y = y * 65536;
        anim_start(w, fl, ANIM_FLAME);
        fl->power = a->power;
        if (k) fl->flags |= F_TESTS;
        fl->dx = ((int32_t)(int8_t)enemies_random(w->enemies) * 65536) >> 6;
      }
      return;
    }
    case W_BOMB: /* $5E5A */
      if (f->diving) return;
      if (a->v94 >= 0) a->v94--;
      if (!f->shot || a->v94 >= 0 || alive_of(w, T_BOMB_SHELL)) return;
      a->v94 = 15;
      {
        int8_t i = alloc(w);
        if (i < 0) return;
        weapon_t *b = &w->pool[i];
        push(&w->shots, w, i);
        b->type = T_BOMB_SHELL;
        b->role = P_BOMB;
        anim_start(w, b, ANIM_BOMB);
        int32_t y = f->y - 12;
        b->x = f->x * 65536;
        b->y = y * 65536;
        b->v40 = (int16_t)-((((uint16_t)y) >> 4) + 4);
      }
      return;
    case W_HOMING: /* $5F0C */
      if (f->diving) return;
      if (a->v94 >= 0) a->v94--;
      if (!f->shot || a->v94 >= 0 || alive_of(w, T_MISSILE)) return;
      a->v94 = 18;
      sound(ev, 4, 2);
      for (int d = 7; d >= 1; d -= 2) {
        int8_t i = alloc(w);
        if (i < 0) return;
        weapon_t *m = &w->pool[i];
        push(&w->shots, w, i);
        m->type = T_MISSILE;
        m->role = P_MISSILE;
        anim_start(w, m, HOMING_START + 30u * (uint32_t)d);
        m->x = f->x * 65536;
        m->y = f->y * 65536;
        m->dir = (uint8_t)d;
        m->v40 = 13;
      }
      return;
    case W_DRONE: /* $454A */
      animate(w, a);
      a->x = f->trail_x * 65536;
      a->y = (f->trail_y + 25) * 65536;
      if (f->scatter) break;
      if (a->v40) a->v40--;
      if (!f->held || a->v40) return;
      a->v40 = 6;
      {
        uint32_t t = gl(w, DOTS + 4u * (uint32_t)(a->power > 2 ? 2 : a->power));
        int n = gs(w, t) + 1;
        int d = 0;
        for (int k = 0; k < n; k++) {
          while (d < WEAPONS_DOTS && w->dots[d].x >= 0) d++;
          if (d == WEAPONS_DOTS) return;
          uint32_t e = t + 2u + 8u * (uint32_t)k;
          w->dots[d].x = (int16_t)((a->x >> 16) + gs(w, e));
          w->dots[d].y = (int16_t)((a->y >> 16) + gs(w, e + 2));
          w->dots[d].dx = gs(w, e + 4);
          w->dots[d].dy = gs(w, e + 6);
        }
      }
      return;
    case W_ELECTRO: /* $469A */
      animate(w, a);
      if (f->scatter) break;
      if (a->v40 == 0) {
        if (!f->held) {
          a->x = f->trail_x * 65536;
          a->y = (f->trail_y + 25) * 65536;
          return;
        }
        a->v40 = 1;
      } else if (a->v40 > 0) {
        if (!f->held) {
          a->v40 = -1;
          return;
        }
        int32_t x = (f->x - f->before_x) * 4 + (a->x >> 16);
        if (x < 0) x = 0;
        if (x > 319) x = 319;
        int32_t y = (f->y - f->before_y) * 4 + (a->y >> 16);
        if (y < 0) y = 0;
        if (y > 191) y = 191;
        a->x = x * 65536;
        a->y = y * 65536;
      } else { /* returning, 8 an axis at most */
        int32_t tx = f->trail_x, ty = f->trail_y + 25;
        int32_t dx = tx - (a->x >> 16), dy = ty - (a->y >> 16);
        dx = dx > 8 ? 8 : dx < -8 ? -8 : dx;
        dy = dy > 8 ? 8 : dy < -8 ? -8 : dy;
        a->x += dx * 65536;
        a->y += dy * 65536;
        if ((a->x >> 16) == tx && (a->y >> 16) == ty) a->v40 = 0;
        return;
      }
      box(w, a);
      if (enemies_hit_first(w->enemies, a->box, damage(w, D_ELECTRO), ev)) {
        sound(ev, 18, 2);
      }
      return;
    case W_MINES: /* $4BAC: the layer */
      animate(w, a);
      if (f->scatter) break;
      if (a->v40 == 0) {
        at_ship(a, f, 0, 25);
        if (f->held) a->v40 = 1;
      } else if (a->v40 == 1) {
        if (!f->held) {
          a->v40 = 3;
        } else if ((a->y >> 16) > 16) {
          a->y -= 12 * 65536;
        } else {
          a->v40 = 2;
        }
      } else if (a->v40 == 2) {
        if (!f->held) {
          a->v40 = 3;
          return;
        }
        int32_t x = a->x >> 16, y = a->y >> 16;
        if (f->stick & 4) x = x - 8 < 0 ? 0 : x - 8;
        if (f->stick & 8) x = x + 8 > 319 ? 319 : x + 8;
        if (f->stick & 1) y = y - 8 < 0 ? 0 : y - 8;
        if (f->stick & 2) y = y + 8 > 191 ? 191 : y + 8;
        a->x = x * 65536;
        a->y = y * 65536;
        int32_t ax = w->drop_x - x, ay = w->drop_y - y;
        if (w->mines == 10) return;
        if ((ax < 0 ? -ax : ax) < 32 && (ay < 0 ? -ay : ay) < 32) return;
        w->drop_x = (int16_t)x;
        w->drop_y = (int16_t)y;
        int8_t i = alloc(w);
        if (i < 0) return;
        weapon_t *m = &w->pool[i];
        push(&w->shots, w, i);
        m->type = a->type;
        m->role = P_MINE;
        m->sprite = a->sprite;
        m->anim = a->anim;
        m->frames = a->frames;
        m->x = a->x;
        m->y = a->y;
        m->power = a->power;
        w->mines++;
        m->v40 = (int16_t)(-4 * w->mines);
      } else { /* back to the ship */
        int32_t tx = f->x, ty = f->y + 25;
        int32_t dx = tx - (a->x >> 16), dy = ty - (a->y >> 16);
        dx = dx > 8 ? 8 : dx < -8 ? -8 : dx;
        dy = dy > 8 ? 8 : dy < -8 ? -8 : dy;
        a->x += dx * 65536;
        a->y += dy * 65536;
        if ((a->x >> 16) == tx && (a->y >> 16) == ty) a->v40 = 0;
      }
      return;
    case W_CANNON: /* $4902 */
      animate(w, a);
      at_ship(a, f, a->ox, a->oy);
      if (f->scatter) break;
      if (a->sprite == CANNON_RECOIL) {
        int8_t i = alloc(w);
        if (i < 0) return;
        weapon_t *s = &w->pool[i];
        push(&w->shots, w, i);
        s->type = 0x30;
        s->role = P_SHELL;
        anim_start(w, s, ANIM_SHELL);
        s->x = a->x & ~0xFFFF;
        s->y = ((a->y >> 16) - 11) * 65536;
        return;
      }
      if (f->shot && a->sprite == CANNON_REST) {
        sound(ev, 36, 2);
        anim_start(w, a, ANIM_CANNON_FIRE);
      }
      return;
    case W_LASER: /* $47A8 */
      animate(w, a);
      at_ship(a, f, a->ox, a->oy);
      if (f->scatter) break;
      if (a->v94 >= 0) a->v94--;
      if (!f->shot || a->v94 >= 0) return;
      sound(ev, 24, 2);
      {
        int8_t i = alloc(w);
        if (i < 0) return;
        weapon_t *b = &w->pool[i];
        push(&w->shots, w, i);
        b->type = 0x24;
        b->role = P_BEAM;
        b->v40 = -32;
        b->owner = (int8_t)(a - w->pool);
        b->power = a->power;
      }
      a->v94 = 6;
      return;
    case W_LAUNCHER: /* $49C8 */
      animate(w, a);
      at_ship(a, f, a->ox, a->oy);
      if (f->scatter) break;
      if (a->sprite == LAUNCHER_FIRE) {
        for (int k = 0, ox = -3; k < 2; k++, ox = 5) {
          int8_t i = alloc(w);
          if (i < 0) return;
          weapon_t *m = &w->pool[i];
          push(&w->shots, w, i);
          m->type = T_BULLET;
          m->role = P_BULLET;
          m->dy = -9 * 65536;
          m->x = ((a->x >> 16) + ox) * 65536;
          m->y = ((a->y >> 16) - 16) * 65536;
          anim_start(w, m, ANIM_MISSILE);
          m->power = a->power;
        }
        return;
      }
      if (f->shot && a->sprite == LAUNCHER_REST) a->frames = 1;
      return;
    default:
      return;
  }
  scatter(w, a, f);
}

void weapons_update(weapons_t *w, const weapons_frame_t *f,
                    enemies_events_t *ev) {
  w->ship_x = (int16_t)f->x;
  w->ship_y = (int16_t)f->y;
  sweep(w, &w->weapons);
  for (int8_t i = w->weapons; i >= 0;) {
    weapon_t *a = &w->pool[i];
    int8_t next = a->next;
    if (!(a->flags & F_DEAD)) {
      if (!f->scatter) a->flags &= (uint8_t)~F_HIDDEN;
      update_weapon(w, a, f, ev);
    }
    i = next;
  }
}

/* ---- the projectiles' updates ---- */

static void update_shot(weapons_t *w, weapon_t *a, const weapons_frame_t *f,
                        enemies_events_t *ev) {
  enemies_t *e = w->enemies;
  if (f->dying && a->role != P_OVERLAY && a->role != P_RING) {
    kill(a);
    return;
  }
  switch (a->role) {
    case P_BULLET: { /* $6086 */
      if (a->bitmap < 0 && !animate(w, a)) {
        kill(a);
        return;
      }
      a->x += a->dx;
      int32_t x = a->x >> 16;
      if ((uint32_t)x >= 312u) {
        kill(a);
        return;
      }
      a->y += a->dy;
      int32_t y = a->y >> 16;
      if ((uint32_t)y >= 192u) {
        kill(a);
        return;
      }
      if (enemies_shoot(e, x, y, a->power + damage(w, D_BULLET), ev)) kill(a);
      return;
    }
    case P_FLAME: /* $5FF2 */
      if (!animate(w, a)) {
        kill(a);
        return;
      }
      a->x += a->dx;
      if ((uint32_t)(a->x >> 16) >= 320u) {
        kill(a);
        return;
      }
      a->y -= 12 * 65536;
      if ((a->y >> 16) < 0) {
        kill(a);
        return;
      }
      if (!(a->flags & F_TESTS)) return;
      box(w, a);
      if (enemies_hit_first(e, a->box, a->power + damage(w, D_FLAME), ev)) {
        kill(a);
      }
      return;
    case P_BOMB: /* $4B12 */
      animate(w, a);
      if (a->v40 < 0) {
        a->v40++;
        a->y -= 6 * 65536;
        return;
      }
      if (a->v40 == 0) {
        a->v40 = 3;
        return;
      }
      if (--a->v40) return;
      sound(ev, 17, 2);
      {
        int32_t x = a->x >> 16, y = a->y >> 16;
        enemies_burst(e, x - 20, y - 20, 40, 40, 4, true, ev);
        kill(a);
        const int16_t blast[4] = {(int16_t)(x - 32), (int16_t)(y - 32),
                                  (int16_t)(x + 31), (int16_t)(y + 31)};
        enemies_hit_all(e, blast, damage(w, D_BOMB), ev);
      }
      return;
    case P_MISSILE: { /* $4210 */
      animate(w, a);
      int32_t x = a->x >> 16, y = a->y >> 16;
      if (a->v40 > 0) {
        a->v40--;
        if (a->v40 < 8) { /* standing: the point test only */
          if (enemies_shoot(e, x, y, damage(w, D_HOMING), ev)) kill(a);
          return;
        }
      } else {
        int16_t tb[4];
        if (!enemies_target_box(e, a->target, tb)) {
          a->target = (int16_t)enemies_pick_target(e);
          if (a->target < 0) {
            enemies_explosion(e, x, y, false); /* nothing to chase */
            kill(a);
            return;
          }
          /* steered at at once, whatever it is ($4250) */
          memcpy(tb, e->pool[a->target].box, sizeof(tb));
        }
        if (a->v40 == 0 || ++a->v40 == 0) {
          int d = enemies_direction(tb[0] + 2 - x, tb[1] + 2 - y);
          int diff = d - a->dir;
          if (diff) {
            if (diff < 0) diff += 8;
            a->dir = (uint8_t)((diff >= 4 ? a->dir - 1 : a->dir + 1) & 7);
            a->v40 = -3;
            anim_start(w, a, HOMING_TURN + 30u * a->dir);
          }
        }
      }
      y += gs(w, HOMING_DY + 2u * a->dir);
      if ((uint32_t)y >= 192u) {
        kill(a);
        return;
      }
      x += gs(w, HOMING_DX + 2u * a->dir);
      if ((uint32_t)x >= 320u) {
        kill(a);
        return;
      }
      a->x = x * 65536;
      a->y = y * 65536;
      if (enemies_shoot(e, x, y, damage(w, D_HOMING), ev)) kill(a);
      return;
    }
    case P_BEAM: { /* $4848 */
      if (a->v40 != 65) {
        const weapon_t *p = &w->pool[a->owner];
        a->x = ((p->x >> 16) - 7) * 65536;
        a->y = ((p->y >> 16) - 24) * 65536;
        a->v40 += 32;
        if (a->v40 == 96) a->v40 = 65;
      }
      if (a->v40 == 65) {
        a->y -= 32 * 65536;
        if ((a->y >> 16) < 0) {
          kill(a);
          return;
        }
      }
      int32_t x0 = (a->x >> 16) + 4 - 2 * a->power;
      int32_t y0 = (a->y >> 16) - a->v40;
      int32_t y1 = y0 + 32;
      if (y1 < 0) return;
      if (y0 < 0) y0 = 0;
      const int16_t b[4] = {(int16_t)x0, (int16_t)y0,
                            (int16_t)(x0 + 4 * a->power + 7), (int16_t)y1};
      enemies_hit_all(e, b, (a->power + 1) * damage(w, D_LASER), ev);
      return;
    }
    case P_SHELL: /* $4E50 */
      animate(w, a);
      a->y -= 10 * 65536;
      if ((a->y >> 16) < 0) {
        kill(a);
        return;
      }
      box(w, a);
      enemies_hit_first(e, a->box, damage(w, D_SHELL), ev); /* it goes on */
      return;
    case P_OVERLAY: { /* $44E8: the cannon's top */
      animate(w, a);
      const weapon_t *p = a->owner >= 0 ? &w->pool[a->owner] : NULL;
      if (!p || p->type != T_CANNON || p->flags & F_DEAD) {
        kill(a);
        return;
      }
      a->x = (f->x + p->ox) * 65536;
      a->y = (f->y + p->oy) * 65536;
      if (f->scatter) scatter(w, a, f);
      return;
    }
    case P_RING: /* $40D6: around the ship, the count down */
      animate(w, a);
      a->x = f->x * 65536;
      a->y = f->y * 65536;
      if (w->invulnerable > 0) w->invulnerable--;
      if (w->invulnerable == 0) kill(a);
      return;
    case P_MINE: /* $4BF6 */
      animate(w, a);
      if (f->held) return;
      if (++a->v40) return;
      {
        int32_t x = a->x >> 16, y = a->y >> 16;
        bool big = a->power != 0;
        if (big) {
          enemies_burst(e, x - 18, y - 18, 20, 20, 3, true, ev);
        } else {
          enemies_burst(e, x - 8, y - 8, 16, 16, 3, false, ev);
        }
        int r0 = big ? 24 : 16, r1 = big ? 23 : 16;
        const int16_t blast[4] = {(int16_t)(x - r0), (int16_t)(y - r0),
                                  (int16_t)(x + r1), (int16_t)(y + r1)};
        enemies_hit_all(e, blast, a->power * 2 + damage(w, D_MINE), ev);
        kill(a);
        w->mines--;
      }
      return;
    default:
      return;
  }
}

void weapons_update_shots(weapons_t *w, const weapons_frame_t *f,
                          enemies_events_t *ev) {
  sweep(w, &w->shots);
  for (int8_t i = w->shots; i >= 0;) {
    weapon_t *a = &w->pool[i];
    int8_t next = a->next;
    if (!(a->flags & F_DEAD)) update_shot(w, a, f, ev);
    i = next;
  }
  for (int d = 0; d < WEAPONS_DOTS; d++) { /* $6A1C */
    weapon_dot_t *t = &w->dots[d];
    if (t->x < 0) continue;
    int16_t x = (int16_t)(t->x + t->dx), y = (int16_t)(t->y + t->dy);
    if ((uint16_t)x >= 316u || (uint16_t)y >= 188u) {
      t->x = -1;
      continue;
    }
    t->x = x;
    t->y = y;
    if (enemies_shoot(w->enemies, x, y, damage(w, D_DOT), ev)) t->x = -1;
  }
}

/* ---- drawing ---- */

static void draw_sprite(weapons_t *w, const weapon_t *a, enemies_draw_fn draw,
                        void *ctx) {
  const x2_sprite_t *s = sprite(w, a->sprite);
  if (s) {
    draw(ctx, s, (const uint8_t *)w->global + s->offset, a->x >> 16,
         a->y >> 16, false);
  }
}

void weapons_draw(weapons_t *w, enemies_draw_fn draw, void *ctx) {
  for (int8_t i = w->weapons; i >= 0; i = w->pool[i].next) {
    const weapon_t *a = &w->pool[i];
    if (a->flags & (F_DEAD | F_HIDDEN) || !a->sprite) continue;
    draw_sprite(w, a, draw, ctx);
  }
}

void weapons_draw_shots(weapons_t *w, enemies_draw_fn draw,
                        weapons_bitmap_fn bitmap, weapons_beam_fn beam,
                        weapons_dot_fn dot, void *ctx) {
  for (int8_t i = w->shots; i >= 0; i = w->pool[i].next) {
    const weapon_t *a = &w->pool[i];
    if (a->flags & (F_DEAD | F_HIDDEN)) continue;
    if (a->role == P_BULLET && a->bitmap >= 0) {
      bitmap(ctx, a->bitmap, a->x >> 16, a->y >> 16);
    } else if (a->role == P_BEAM) { /* $6CBA */
      int p = a->power > 2 ? 2 : a->power;
      int32_t x = (a->x >> 16) + 4 - 2 * p, y = a->y >> 16;
      if (x <= -16 || x >= 320) continue;
      if (a->v40 >= 65) {
        const x2_sprite_t *t = sprite(w, gl(w, LASER_TAILS + 4u * (uint32_t)p));
        if (t) draw(ctx, t, (const uint8_t *)w->global + t->offset, x, y + 1, false);
      }
      const x2_sprite_t *tip = sprite(w, gl(w, LASER_TIPS + 4u * (uint32_t)p));
      if (tip) {
        draw(ctx, tip, (const uint8_t *)w->global + tip->offset, x,
             y - a->v40, false);
      }
      const uint8_t *pattern = x2global_raw(w->global, LASER_PATTERNS + 20u * (uint32_t)p, 20);
      if (pattern && y >= 0 && a->v40 > 0) beam(ctx, pattern, x, y, a->v40);
    } else if (a->sprite) {
      draw_sprite(w, a, draw, ctx);
    }
  }
  for (int d = 0; d < WEAPONS_DOTS; d++) {
    if (w->dots[d].x >= 0) dot(ctx, w->dots[d].x, w->dots[d].y);
  }
}
