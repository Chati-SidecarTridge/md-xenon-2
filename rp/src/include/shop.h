/**
 * File: shop.h
 * Description: Crispin's shop, ported from the original's $10520: its
 *              screen, its animations, the sell and buy phases and their
 *              rules, run between the level's frames. The pictures, texts,
 *              prices and tables are the original's, read from the store's
 *              shop section (x2screens.h); its planar sprites and blocks are
 *              drawn into the chunky framebuffer as the original's own
 *              routines draw them. Plain C, no SDK.
 *
 * It steps a VBL at a time and keeps the original's pace on a model of the
 * ST's clock (shop.c), whatever pace the game itself runs at. Its
 * sound requests go to the driver (sfx.h) as the original's VBL handler
 * passes them on; what it changes in the game (the cash, the ship's
 * numbers, the weapons) is the port's own state.
 */

#ifndef SHOP_H
#define SHOP_H

#include <stdbool.h>
#include <stdint.h>

#include "enemies.h"
#include "ship.h"
#include "weapons.h"
#include "x2global.h"
#include "x2level.h"
#include "x2screens.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the shop does to the game outside its own state. */
typedef struct {
  void *ctx;
  void (*take_token)(void *ctx, int token); /* a token's routine */
  void (*sound_stop)(void *ctx);            /* the driver's stop ($2CF5E) */
  void (*sample)(void *ctx, bool on);       /* sample 0 ($2CF7A, $2CF88) */
  void (*redraw)(void *ctx);                /* the playfield back ($1DD0) */
} shop_hooks_t;

/* The game the shop sees and changes. */
typedef struct {
  const x2screens_header_t *image;
  const x2global_header_t *global;
  const x2level_header_t *level;
  ship_t *ship;
  weapons_t *weapons;
  enemies_t *enemies;   /* the game's random generator */
  uint32_t *cash;       /* $C9A */
  uint32_t level_number; /* $E1A */
  int32_t line;         /* the world line ($CD0): end shop at 480 or less */
  int player;           /* $428: 0 or 1 */
  int players;          /* $422 */
  uint16_t *nashwan;    /* $CA2: frames of Super Nashwan */
  uint16_t *shades;     /* $C90: frames of Bitmap Shades */
  const uint16_t *palette; /* the palette shown when the shop is entered */
  shop_hooks_t hooks;
} shop_game_t;

/* The level's advice, three strings for the mid shop and three for the end
 * shop ($CF2, $CFE), as the level's start sets them; ADVICE rotates them. */
void shop_level_start(const x2level_header_t *level);

/* The advice queues read and set (two players keep their own, $CF2 being
 * in the original's per-player block). */
void shop_advice_get(uint32_t queues[6]);
void shop_advice_set(const uint32_t queues[6]);

/* Enter the shop as the level loop does ($7CF8): the driver stopped, the
 * budget and the cash's floor from the level, PROTECTION off. False when
 * the shop's image or memory is missing. */
bool shop_start(const shop_game_t *game);

/* One VBL in the shop: the stick (up 1, down 2, left 4, right 8, fire
 * $80) and a fire pressed since the last call. True while the shop runs,
 * false once it is over. */
bool shop_vbl(uint8_t stick, bool fire);

#ifdef __cplusplus
}
#endif

#endif /* SHOP_H */
