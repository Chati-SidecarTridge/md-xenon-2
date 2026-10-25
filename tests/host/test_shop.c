// units: rp/src/shop.c rp/src/screen.c rp/src/weapons.c rp/src/enemies.c rp/src/level1.c rp/src/level2.c rp/src/level3.c rp/src/level4.c rp/src/level5.c rp/src/rng.c rp/src/x2level.c rp/src/x2global.c rp/src/x2sprite.c rp/src/x2screens.c
/* Crispin's shop on synthetic images laid out like the original's (the
 * shop's tables, strings and one small sprite every picture names; a level
 * with its budgets, palette and advice; the shared image's driver bytes for
 * the cash's digits): a mid shop visited with the stick as a player would,
 * the cash raised to its floor on entry, the rear shot sold for half its
 * price, ADVICE and SPEEDUP bought (the token's routine run), SPEEDUP
 * refused once the ship has no room for it, the exit (the forward shot
 * made, the weapons saved, the playfield asked back), and the entry's
 * pace (the fades' VBLs before its first sound). No game data. */
#include <stdlib.h>
#include <string.h>

#include "fb_chunked.h"
#include "palette.h"
#include "sfx.h"
#include "shop.h"
#include "test.h"

#define SHOP_START 0x10520u
#define SHOP_END 0x1FB92u
#define ADVICE_START 0x77F2u
#define SPRITE 0x1F000u
#define LIST 0x1F100u
#define NAME 0x1F200u
#define HINT 0x1F300u

uint8_t fb_chunked_buffer[FB_CHUNKED_SIZE];

static uint16_t shown[16];
void palette_set_entry(uint8_t idx, uint16_t color) { shown[idx & 15] = color; }

static int effects[256], effect_vbl[256], n_effects, vbl;
void sfx_request(int effect, int channel) {
  (void)channel;
  if (n_effects < 256) {
    effect_vbl[n_effects] = vbl;
    effects[n_effects++] = effect;
  }
}
uint8_t sfx_peek(uint32_t addr) {
  (void)addr;
  return 0;
}

static uint8_t game[SHOP_END];
static uint8_t shop_img[sizeof(x2screens_header_t) + (SHOP_END - SHOP_START) + 0x100];
static uint8_t level_img[256 + 0x100];
static uint8_t global_img[sizeof(x2global_header_t) + X2G_DRIVER_SIZE];
static uint16_t map[20 * 20];
static enemies_t en;
static weapons_t wp;
static ship_t ship;
static uint32_t cash;
static uint16_t nashwan, shades;
static int tokens[16], n_tokens, redraws;

static void put16(uint32_t a, uint32_t v) {
  game[a] = (uint8_t)(v >> 8);
  game[a + 1] = (uint8_t)v;
}
static void put32(uint32_t a, uint32_t v) {
  put16(a, v >> 16);
  put16(a + 2, v & 0xFFFFu);
}
static void text(uint32_t a, const char *s) { memcpy(&game[a], s, strlen(s) + 1); }

static void take(void *ctx, int token) {
  (void)ctx;
  if (n_tokens < 16) tokens[n_tokens++] = token;
}
static void stop(void *ctx) { (void)ctx; }
static void sample(void *ctx, bool on) {
  (void)ctx;
  (void)on;
}
static void redraw(void *ctx) {
  (void)ctx;
  redraws++;
}

static void build(void) {
  /* One sprite every picture names: 16 pixels, one line, the first in
   * colour 1, the rest masked; one list that shows it and loops. */
  put16(SPRITE, 0);
  put16(SPRITE + 2, 0);
  put16(SPRITE + 4, 16);
  put16(SPRITE + 6, 0);
  put16(SPRITE + 8, 0x8000);
  put16(SPRITE + 16, 0x7FFF);
  put32(LIST, SPRITE);
  put32(LIST + 4, 0);
  put32(LIST + 8, LIST);
  for (uint32_t id = 1; id <= 27; id++) {
    put32(0x10F10 + 4 * (id - 1), LIST); /* the items' pictures */
    put32(0x10EA8 + 4 * id, NAME);       /* their names */
  }
  static const uint16_t prices[25] = {0,    200,  500,  500,  500,  600,  1000,
                                      1000, 1000, 1000, 1200, 2000, 3000, 3000,
                                      4000, 4000, 4000, 4000, 4500, 5000, 5500,
                                      6000, 6000, 6000, 6000};
  for (uint32_t id = 1; id <= 25; id++) put16(0x10F7C + 2 * (id - 1), prices[id - 1]);
  game[0x10E92 + 8 - 1] = 4; /* REAR SHOT: slot 5 */
  for (uint32_t k = 0; k < 9; k++) put32(0x120C6 + 4 * k, SPRITE); /* covers */
  for (uint32_t k = 0; k < 2; k++) put32(0x120EA + 4 * k, SPRITE); /* screens */
  for (uint32_t k = 0; k < 7; k++) { /* the machinery */
    put16(0x124A4 + 8 * k, 220);
    put16(0x124A6 + 8 * k, 20);
    put32(0x124A8 + 8 * k, LIST);
  }
  put32(0x10966, 0); /* no panels */
  text(NAME, "ITEM");
  text(HINT, "HINT");
  static const uint32_t strings[] = {0x1243C, 0x1245A, 0x114BC, 0x114AE,
                                     0x111B3, 0x111C8, 0x11314, 0x10FAE,
                                     0x1118C, 0x1163C};
  for (size_t k = 0; k < sizeof(strings) / sizeof(strings[0]); k++) text(strings[k], "OK");
  text(ADVICE_START, "ENOUGH");
  for (uint32_t k = 0; k < 16; k++) put16(0x1FB72 + 2 * k, 0x111 * (k & 7));

  x2screens_header_t *sh = (x2screens_header_t *)shop_img;
  sh->magic = X2SCREENS_MAGIC;
  sh->version = X2SCREENS_VERSION;
  sh->windows = 2;
  sh->window[0] = (x2screens_window_t){ADVICE_START, 0x10, sizeof(*sh)};
  memcpy(shop_img + sizeof(*sh), &game[ADVICE_START], 0x10);
  sh->window[1] = (x2screens_window_t){SHOP_START, SHOP_END - SHOP_START, sizeof(*sh) + 0x10};
  memcpy(shop_img + sizeof(*sh) + 0x10, &game[SHOP_START], SHOP_END - SHOP_START);

  /* The level: its budgets, its palette's pointer, its start routine
   * writing the first mid advice (move.l #HINT, $CF2). */
  x2level_header_t *lh = (x2level_header_t *)level_img;
  lh->magic = X2LEVEL_MAGIC;
  lh->level = 1;
  lh->raw_offset = 256;
  lh->raw_size = 0x100;
  uint8_t *L = level_img + 256;
  L[0x14] = 0x00; /* the wave and scenery tables: one end word each */
  L[0x15] = 0x04;
  L[0x16] = 0xF8;
  L[0x17] = 0x00;
  L[0x38] = 0x00;
  L[0x39] = 0x04;
  L[0x3A] = 0xF8;
  L[0x3B] = 0x00;
  L[0x1C] = 0x00; /* $4F01C: the palette at $4F080 */
  L[0x1D] = 0x04;
  L[0x1E] = 0xF0;
  L[0x1F] = 0x80;
  L[0x20] = 0x60; /* $4F020: bra $4F0A0 */
  L[0x21] = 0x00;
  L[0x22] = 0x00;
  L[0x23] = 0x7E;
  static const uint8_t move[8] = {0x21, 0xFC, 0x00, 0x01, 0xF3, 0x00, 0x0C, 0xF2};
  memcpy(L + 0xA0, move, sizeof(move));
  L[0x3C] = 3000 >> 8; /* $4F03C: the end shop's budget, $4F03E the mid's */
  L[0x3D] = 3000 & 0xFF;
  L[0x3E] = 600 >> 8;
  L[0x3F] = 600 & 0xFF;
  for (int k = 0; k < 16; k++) lh->palette[k] = (uint16_t)(0x100 * (k & 7));

  x2global_header_t *gh = (x2global_header_t *)global_img;
  gh->magic = X2GLOBAL_MAGIC;
  gh->driver_offset = sizeof(*gh);
  gh->sprites_offset = sizeof(*gh);
}

static void run(int vbls, uint8_t stick, bool fire) {
  for (int i = 0; i < vbls; i++, vbl++) {
    CHECK(shop_vbl(i < 3 ? stick : 0, fire && i == 0));
  }
}

int main(void) {
  build();
  const x2level_header_t *lh = (const x2level_header_t *)level_img;
  const x2global_header_t *gh = (const x2global_header_t *)global_img;
  enemies_init(&en, lh, gh, map, 20, 1, 77);
  weapons_init(&wp, gh, &en);
  weapons_game_start(&wp);
  weapons_make(&wp, 68); /* the rear shot: slot 5 */
  ship.lives = 3;
  ship.shield = SHIP_SHIELD_FULL;
  ship.fire_rate = 1;
  shop_level_start(lh);
  shop_game_t g = {(const x2screens_header_t *)shop_img, gh, lh, &ship, &wp, &en,
                   &cash, 1, 2511, 0, 1, &nashwan, &shades, lh->palette,
                   {NULL, take, stop, sample, redraw}};
  CHECK(shop_start(&g));

  /* The mid shop's floor: its budget, plus 0-150 at random. */
  CHECK(cash >= 600 && cash <= 750 && cash % 50 == 0);
  uint32_t floor = cash;

  /* The entry: the fade out (8 steps of 3 VBLs), the screen drawn, the
   * fade in (7 of 2) before the shutters' sound. */
  run(1500, 0, false);
  CHECK(n_effects >= 2);
  CHECK_EQ(effects[0], 22);
  CHECK(effect_vbl[0] >= 24 + 14);
  CHECK_EQ(effects[1], 23);
  CHECK_EQ(shown[1], 0x111);

  /* Selling: the rear shot chosen, Crispin prices it, chosen again: half
   * its price (power 0), its slot freed. */
  run(600, 0, true);
  CHECK_EQ(cash, floor);
  run(400, 0, true);
  CHECK_EQ(cash, floor + 500);
  CHECK_EQ(weapons_slot_type(&wp, 5), 0);

  /* EXIT (up to the buttons, the first is EXIT), then buying from the
   * first monitor: ADVICE, chosen twice. */
  run(60, 1, false);
  run(900, 0, true);
  run(400, 0, true);
  run(600, 0, true);
  CHECK_EQ(cash, floor + 500 - 200);

  /* SPEEDUP (the next monitor): its token's routine. */
  run(60, 8, false);
  run(400, 0, true);
  run(600, 0, true);
  CHECK_EQ(cash, floor + 500 - 200 - 500);
  CHECK_EQ(n_tokens, 1);
  CHECK_EQ(tokens[0], TOKEN_SPEEDUP);

  /* No room for another: refused, nothing paid. */
  ship.speed_ups = 2;
  run(400, 0, true);
  run(600, 0, true);
  CHECK_EQ(cash, floor + 500 - 200 - 500);
  CHECK_EQ(n_tokens, 1);

  /* EXIT (up to BUY under the second monitor, left to EXIT): the shop
   * closes; the forward shot is there, the weapons saved, the playfield
   * asked back for the fade in. */
  run(60, 1, false);
  run(60, 4, false);
  bool on = true;
  for (int i = 0; i < 3000 && on; i++, vbl++) on = shop_vbl(0, i == 0);
  CHECK(!on);
  CHECK_EQ(redraws, 1);
  CHECK_EQ(weapons_slot_type(&wp, 0), 148);
  CHECK_EQ(wp.saved.type[0], 148);
  CHECK_EQ(wp.saved.type[5], 0);
  TEST_END();
}
