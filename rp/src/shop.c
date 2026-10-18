/**
 * File: shop.c
 * Description: Crispin's shop (shop.h), ported from the original's $10520.
 *
 * The shop keeps the original's pace on the model of the ST's clock the
 * screens share (screen.h): what it draws costs the 68000 cycles the
 * original's drawing takes, measured in Hatari over whole visits. The
 * original's routines that wait for frames are coroutines (screen.h).
 */

#pragma GCC optimize("O3")

#include "shop.h"

#include <stdlib.h>
#include <string.h>

#include "fb_chunked.h"
#include "screen.h"
#include "sfx.h"

/* Where the original keeps the shop's pictures, texts and tables. */
#define MONITOR_A 0x171E0u    /* a monitor's frame, left and right parts */
#define MONITOR_B 0x17510u
#define BLOCKS 0x10966u       /* the panels: source, offset, groups, lines */
#define PLAYER_2 0x18170u     /* "PLAYER 1" made "2" */
#define PALETTE_SHOP 0x1FB72u
#define PALETTE_ENDING 0x10844u
#define PRICES 0x10F7Cu       /* a word an item, from item 1 */
#define NAMES 0x10EA8u        /* a long an item, from item 0 */
#define LISTS 0x10F10u        /* the items' pictures, from item 1 */
#define CLASSES 0x10E92u      /* the slot an item needs, from item 1 */
#define COVERS 0x120C6u       /* the monitor switching on and off: 9 */
#define SCREENS 0x120EAu      /* a monitor's screen behind its item: 2 */
#define LAMP_ON 0x12CDEu      /* the cursor under a monitor */
#define LAMP_OFF 0x12C74u
#define EXIT_ON 0x15F7Cu
#define BUY_ON 0x161BCu
#define SELL_ON 0x163FCu
#define EXIT_OFF 0x1663Cu
#define BUY_OFF 0x1687Cu
#define SELL_OFF 0x16ABCu
#define HEAD 0x1A862u         /* Crispin: 52 lines from line 9 */
#define CHIN 0x1B3C2u         /* 10 lines from line 95 */
#define MOUTHS 0x1B5F2u       /* 34 lines from line 61, four of them */
#define MOUTH_BYTES 1904u
#define MACHINES 0x124A4u     /* the seven loops around him: x, y, list */
#define LIDS_RIGHT 0x16DA2u
#define LIDS_LEFT 0x16CFCu
#define PUPIL_LEFT 0x16E48u
#define PUPIL_RIGHT 0x16F84u
#define HAND_UP 0x17C50u
#define HAND_TURN 0x179C0u
#define HAND_PRESS 0x17EE0u
#define SHUTTER_TOP 0x1D3B2u
#define SHUTTER_BOTTOM 0x1DC72u
#define SHUTTER_EDGES 0x12640u /* three over the top leaf, three under */
#define BORDER_TOP 0x1939Au   /* the window's borders under the shutter */
#define BORDER_MID 0x195DAu
#define BUBBLE_SHUTTER 0x1E532u
#define LIGHT_A 0x12BA0u
#define LIGHT_B 0x12C0Au
#define FONT 0x1F412u         /* from ' ', 32 bytes a character */
#define DIGITS 0x2C3A2u       /* the status bar's digits */
#define S_SELL 0x1243Cu
#define S_BASEMENT 0x125C4u
#define S_MUST_GO 0x125E8u
#define S_LAST_CHANCE 0x125FBu
#define S_POWER 0x1163Cu
#define S_COST 0x114BCu
#define S_PAY 0x114AEu
#define S_BUY 0x1245Au
#define S_OUT_OF_STOCK 0x1118Cu
#define S_NO_ROOM 0x10FAEu
#define S_CASH 0x111B3u
#define S_ELSE 0x111C8u
#define S_THANKS 0x11314u
#define S_VIEWERS_1 0x10864u
#define S_VIEWERS_2 0x1087Fu
#define S_VIEWERS_3 0x108A6u

/* The level's numbers and the advice the level's start sets. */
#define L_PALETTE 0x4F01Cu    /* a pointer to the level's palette */
#define L_BUDGET_END 0x4F03Cu
#define L_BUDGET_MID 0x4F03Eu
#define L_START_HOOK 0x4F020u /* bra to the level's start routine */
#define LEVEL_BASE 0x4F000u
#define V_ADVICE 0xCF2u       /* where the level's start writes it */
#define ADVICE_LAST 0x77F2u   /* the game's own third advice */
#define END_SHOP_LINE 480
#define ADVICE_LINE 576
#define ENDING_LINE 960

#define ITEM_ADVICE 2
#define ITEM_UNKNOWN 26       /* "?" */
#define ITEM_MORE 27
#define FORWARD_SHOT 148      /* the slot type never sold */
#define CELLS 20
#define TEXT_COLUMN 55        /* x 220, a column is 4 pixels */
#define TEXT_LINE 118
#define TEXT_WRAP 75
#define WINDOW_X 208          /* Crispin's window and the bubble */
#define DRIVER_PLAYING_A 0x2D0C0u /* the driver's bytes while effects play */
#define DRIVER_PLAYING_B 0x2D0C2u

#define FRAME(pt) PT_WAIT(pt, screen_frame(&s->scr))
#define VBL_WAIT(pt) PT_WAIT(pt, screen_vbl(&s->scr))

/* What the shop's drawing costs on the ST, in cycles. */
#define FRAME_CYCLES 8000u    /* each frame besides its drawing */
#define STATIC_CYCLES 8650u
#define SPRITE_CYCLES 270u
#define SPRITE_GROUP_CYCLES 278u /* a line of 16 pixels */
#define BLOCK_GROUP_CYCLES 122u
#define CLEAR_LINE_CYCLES 794u
#define GLYPH_CYCLES 3416u
#define CASH_CYCLES 26185u
#define SCREEN_CYCLES 347000u /* the back screen cleared, copied to the front */

/* ---- state ---- */

typedef struct {
  int16_t state;  /* +0: <= 0 switching on or off, else the item's timer */
  uint16_t id;    /* +6: the item, 0 none */
  uint32_t list;  /* +8: its picture's next frame */
  int8_t slot;    /* +12: the weapon's slot, when selling */
} cell_t;

typedef struct {
  shop_game_t g;
  screen_t scr;
  int16_t effect;          /* $414: -1 none */
  uint8_t stick;           /* $A00 */
  bool fire;               /* $A01: latched, cleared where it is read */
  int16_t budget;          /* $9FA */
  cell_t cells[CELLS];     /* $120F2 */
  int16_t col, row;        /* $4F2, $4F4: row 4 is the buttons */
  int16_t shutter, bubble; /* $4EC, $4EE */
  bool talking;            /* $4F0 */
  int16_t text_col, text_line; /* $4F6, $4F8 */
  bool selling, listening, ending; /* $11788, $11789, $1178C */
  bool end_shop;           /* $A07 */
  uint8_t wink;            /* $1178A */
  int8_t glance;           /* $1178B */
  int16_t page;            /* $10B16 */
  int16_t last_col, last_row; /* $10C84 */
  int16_t price;           /* $114A8: -1 none on offer */
  int cell;                /* $114AA */
  int16_t repeat;          /* $11644 */
  int16_t hand_x, hand_y;  /* $11B6C */
  uint32_t hand;
  /* the coroutines' places and their variables */
  pt_t pt_main, pt_close, pt_loop, pt_name, pt_type, pt_number, pt_input,
      pt_frames, pt_hands, pt_shut, pt_on_off, pt_hand, pt_advice;
  uint32_t text;           /* the string being typed */
  uint8_t digits[6];
  int n_digits, digit;
  int frames_left, hands_left, on_off_n, hand_n;
  uint16_t to[16];         /* a palette faded in */
  int16_t pay;             /* the payment counted down */
} shop_t;

static shop_t *s;
static uint32_t advice[6]; /* $CF2: the mid queue; $CFE: the end queue */
static uint32_t machines[7]; /* the loops' places, kept between visits */
static bool machines_set;

/* The items that are tokens' routines. */
static const struct {
  uint8_t id, token;
} k_tokens[] = {{3, TOKEN_SPEEDUP}, {4, TOKEN_HEALTH1},  {5, TOKEN_AUTOFIRE},
                {7, TOKEN_HEALTH2}, {8, TOKEN_REAR},     {9, TOKEN_MINE},
                {10, TOKEN_SIDE},   {11, TOKEN_ELECTRO}, {12, TOKEN_POWERUP},
                {15, TOKEN_CANNON}, {16, TOKEN_DIVE},    {17, TOKEN_LAUNCHER},
                {18, TOKEN_LASER},  {19, TOKEN_DRONE},   {20, TOKEN_FLAMER},
                {21, TOKEN_BOMB},   {23, TOKEN_HOMING}};

/* A slot's weapon as the shop's item number ($68 of the object). */
static const struct {
  uint16_t type;
  uint8_t id;
} k_ids[] = {{148, 1},  {68, 8},   {56, 9},   {152, 10}, {76, 11},
             {60, 13},  {192, 14}, {52, 15},  {144, 17}, {32, 18},
             {44, 19},  {176, 20}, {40, 21},  {72, 23}};

/* ---- the original's memory, read only ---- */

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return be16(p) << 16 | be16(p + 2); }

static const uint8_t *at(uint32_t addr, uint32_t len) {
  return screen_at(&s->scr, addr, len);
}
static uint32_t word_at(uint32_t addr) { return screen_word(&s->scr, addr); }
static uint32_t long_at(uint32_t addr) { return screen_long(&s->scr, addr); }
static uint8_t byte_at(uint32_t addr) { return screen_byte(&s->scr, addr); }

static void palette_at(uint32_t addr, uint16_t to[16]) {
  const uint8_t *p = at(addr, 32);
  for (int i = 0; i < 16; i++) to[i] = p ? (uint16_t)be16(p + 2 * i) : 0;
}

static uint16_t rnd(void) { return (uint16_t)enemies_random(s->g.enemies); }

static int16_t price_of(uint16_t id) { /* halved in the bargain basement */
  uint16_t p = (uint16_t)word_at(PRICES + 2u * (id - 1u));
  return (int16_t)(s->g.level_number == 5 ? p >> 1 : p);
}

static int cell_x(int k) { return 4 + 41 * (k % 5); }
static int cell_y(int k) { return 4 + 41 * (k / 5); }

/* ---- drawing, as the original's routines draw into a screen ---- */

/* 16 pixels of the four planes at (x, y): where `keep` has a bit the
 * screen's pixel stays, ORed with the planes. */
static void spend(uint32_t cycles) { screen_spend(&s->scr, cycles); }

/* A masked sprite with its hotspot at (x, y), clipped to lines 0 to the
 * clip line ($10AE): four words (hotspot x, y, width, lines - 1), then
 * each line plane by plane and the mask. */
static void sprite(uint32_t addr, int x, int y) {
  const uint8_t *h = at(addr, 8);
  if (!h) return;
  int left = x - (int16_t)be16(h), top = y - (int16_t)be16(h + 2);
  int span = (int)be16(h + 4), lines = (int)be16(h + 6) + 1;
  int groups = (span + 15) / 16;
  if (left >= FB_CHUNKED_W || left + span <= 0 || groups == 0) return;
  const uint8_t *d = at(addr + 8u, (uint32_t)(groups * 10 * lines));
  if (!d) return;
  spend(SPRITE_CYCLES);
  for (int l = 0; l < lines; l++) {
    int y0 = top + l;
    if (y0 > s->scr.clip || y0 >= FB_CHUNKED_H) break;
    if (y0 < 0) continue;
    const uint8_t *r = d + l * groups * 10;
    spend(SPRITE_GROUP_CYCLES * (uint32_t)groups);
    for (int g = 0; g < groups; g++) {
      screen_group(left + 16 * g, y0, be16(r + 2 * g), be16(r + 2 * (groups + g)),
            be16(r + 2 * (2 * groups + g)), be16(r + 2 * (3 * groups + g)),
            be16(r + 2 * (4 * groups + g)));
    }
  }
}

/* Lines of screen groups (four plane words each) from `addr`, `stride`
 * bytes apart, at (x, y); the first and last groups keep the screen where
 * `keep_first` / `keep_last` have bits ($1094C, $11F6E). */
static void blocks(uint32_t addr, int x, int y, int groups, int lines,
                   int stride, uint32_t keep_first, uint32_t keep_last) {
  const uint8_t *d = at(addr, (uint32_t)(stride * (lines - 1) + 8 * groups));
  if (!d) return;
  for (int l = 0; l < lines; l++, d += stride) {
    if (y + l < 0 || y + l >= FB_CHUNKED_H) continue;
    spend(BLOCK_GROUP_CYCLES * (uint32_t)groups);
    for (int g = 0; g < groups; g++) {
      const uint8_t *p = d + 8 * g;
      uint32_t keep = g == 0 ? keep_first : g == groups - 1 ? keep_last : 0;
      screen_group(x + 16 * g, y + l, be16(p), be16(p + 2), be16(p + 4), be16(p + 6),
            keep);
    }
  }
}

/* Lines of Crispin's window: 112 pixels from x 208, the first 7 and the
 * last 9 of the frame's border kept ($11F6E). */
static void window_rows(uint32_t addr, int y, int lines) {
  if (lines > 0) blocks(addr, WINDOW_X, y, 7, lines, 56, 0xFE00u, 0x01FFu);
}

/* The bubble's lines cleared between the border's edges ($10AB4). */
static void clear_rows(int y, int lines) {
  spend(CLEAR_LINE_CYCLES * (uint32_t)lines);
  for (int l = 0; l < lines; l++) {
    memset(&fb_chunked_buffer[(y + l) * FB_CHUNKED_W + WINDOW_X + 7], 0, 96);
  }
}

/* A monitor's static ($12232): 28 lines of 32 pixels in colours 0 and 7
 * from the monitor's corner, each line a word of a generator seeded from
 * the game's. */
static void tv_static(int x, int y) {
  spend(STATIC_CYCLES);
  uint16_t d0 = rnd(), s0 = (uint16_t)(x & 15);
  for (int l = 0; l < 28; l++) {
    uint16_t d1 = d0;
    d0 = (uint16_t)((d0 << 8) | 10u);
    d0 = (uint16_t)(d0 - d1);
    d0 = (uint16_t)(d0 * 4u + d1 + 1u);
    d1 = (uint16_t)((d0 >> 8) ^ d0);
    uint8_t *row = &fb_chunked_buffer[(y + l) * FB_CHUNKED_W + x];
    for (int i = 0; i < 32; i++) {
      row[i] = (d1 & (0x8000u >> ((s0 + i) & 15))) ? 7 : 0;
    }
  }
}

/* Eight pixels of a 4-plane font line: the planes' bytes. */
static uint8_t font_pixel(const uint8_t *f, int i) {
  uint8_t bit = (uint8_t)(0x80u >> i);
  return (uint8_t)(((f[0] & bit) ? 1 : 0) | ((f[1] & bit) ? 2 : 0) |
                   ((f[2] & bit) ? 4 : 0) | ((f[3] & bit) ? 8 : 0));
}

/* A character of the bubble's font at the text's column ($12384): even
 * columns write the byte's 8 pixels, odd ones OR the glyph's 4 into the
 * right half. */
static void glyph(uint8_t c) {
  spend(GLYPH_CYCLES);
  const uint8_t *f = at(FONT + ((uint32_t)(c - 32u) & 0xFFu) * 32u, 32);
  int x = 4 * s->text_col;
  for (int l = 0; f && l < 8; l++, f += 4) {
    uint8_t *row = &fb_chunked_buffer[(s->text_line + l) * FB_CHUNKED_W + x];
    if (s->text_col & 1) {
      for (int i = 0; i < 4; i++) row[i] |= font_pixel(f, i);
    } else {
      for (int i = 0; i < 8; i++) row[i] = font_pixel(f, i);
    }
  }
  s->text_col++;
}

/* The cash, seven digits from (136, 172) ($122C2 through $1B5A). */
static void cash(void) {
  static const int32_t k_powers[] = {1000000, 100000, 10000, 1000, 100, 10, 1};
  int32_t v = (int32_t)*s->g.cash;
  spend(CASH_CYCLES);
  for (int k = 0; k < 7; k++) {
    uint32_t n = 0;
    while ((v -= k_powers[k]) >= 0) n++;
    v += k_powers[k];
    const uint8_t *f = at(DIGITS + n * 32u, 32);
    for (int l = 0; f && l < 8; l++, f += 4) {
      uint8_t *row = &fb_chunked_buffer[(172 + l) * FB_CHUNKED_W + 136 + 8 * k];
      for (int i = 0; i < 8; i++) row[i] = font_pixel(f, i);
    }
  }
}

/* ---- the shop's screen, piece by piece ---- */

/* The seven loops around Crispin, a frame each a call ($1247A). */
static void machinery(void) {
  for (uint32_t k = 0; k < 7; k++) {
    uint32_t list = machines[k], frame = long_at(list);
    list += 4;
    if (!frame) {
      list = long_at(list);
      frame = long_at(list);
      list += 4;
    }
    machines[k] = list;
    sprite(frame, (int16_t)word_at(MACHINES + 8 * k),
           (int16_t)word_at(MACHINES + 8 * k + 2));
  }
}

/* His eyes ($11EA4): closed while he listens, a wink after ADVICE, a blink
 * now and then; his pupils, and a glance now and then. */
static void eyes(void) {
  bool both = s->listening;
  if (!both && !s->wink) both = (rnd() & 0x2654u) == 0;
  if (both || s->wink) {
    if (both) sprite(LIDS_RIGHT, 263, 41);
    sprite(LIDS_LEFT, 247, 41);
    if (s->wink) s->wink--;
  }
  if (s->listening) return;
  if (s->glance == 0) {
    if (rnd() & 0x2654u) return;
    s->glance = 8;
  } else if (s->glance > 0) {
    s->glance--;
  }
  if (s->glance >= 0) sprite(PUPIL_RIGHT, 263, 35);
  sprite(PUPIL_LEFT, 245, 35);
}

/* The window's two shutters ($11D6E). */
static void shutters(void) {
  static const uint16_t k_edges[6] = {0x000, 0x100, 0x1B0, 0x2B0, 0x3B0, 0x460};
  int sh = s->shutter;
  if (sh > 8) {
    window_rows(SHUTTER_TOP + (uint32_t)(48 - sh) * 56u, 9, sh - 8);
    window_rows(SHUTTER_BOTTOM, 113 - sh, sh - 8);
  }
  for (int k = 0; k < 6; k++) {
    sprite(SHUTTER_EDGES + k_edges[k], 215 + 32 * (k % 3),
           k < 3 ? sh + 1 : 112 - sh);
  }
  if (sh < 8) { /* the borders back where the leaves were */
    blocks(BORDER_TOP, WINDOW_X, 0, 7, 9, 64, 0, 0);
    blocks(BORDER_MID, WINDOW_X, 105, 7, 9, 64, 0, 0);
  }
}

/* Crispin, every frame ($11D12). */
static void crispin(void) {
  window_rows(HEAD, 9, 52);
  window_rows(CHIN, 95, 10);
  uint32_t mouth = MOUTHS;
  if (s->talking) {
    uint16_t r = rnd();
    for (int k = 0; k < 3 && !(r & 1u); k++, r >>= 1) mouth += MOUTH_BYTES;
  }
  window_rows(mouth, 61, 34);
  machinery();
  eyes();
  if (s->shutter) shutters();
}

/* The bubble's shutter ($11F42). */
static void bubble_shutter(void) {
  if (!s->bubble) return;
  clear_rows(114, 68 - s->bubble);
  window_rows(BUBBLE_SHUTTER, 114 + 68 - s->bubble, s->bubble);
}

/* The bubble emptied ($10A9A), and the text back to its start ($122B0). */
static void clear_bubble(void) { clear_rows(114, 68); }

static void clear_text(void) {
  clear_bubble();
  s->text_col = TEXT_COLUMN;
  s->text_line = TEXT_LINE;
}

/* The monitors' timers ($11FA8): an item drops to static every 20 to 275
 * frames; an empty monitor shows static only. */
static void timers(void) {
  for (int k = 0; k < CELLS; k++) {
    cell_t *c = &s->cells[k];
    if (!c->id) {
      c->state = 0;
    } else if (--c->state <= 0) {
      c->state = (int16_t)(20 + (rnd() & 255u));
    }
  }
}

/* The monitors ($11FDC). */
static void monitors(void) {
  for (int k = 0; k < CELLS; k++) {
    cell_t *c = &s->cells[k];
    int x = cell_x(k), y = cell_y(k);
    if (c->state <= 0) {
      if (c->state != -8) {
        tv_static(x, y);
        if (c->state == 0) continue;
      }
      sprite(long_at(COVERS + 4u * (uint32_t)(c->state + 9)), x, y);
    } else if (c->state <= 2) {
      if (c->state == 2 && s->effect < 0 && !sfx_peek(DRIVER_PLAYING_A) &&
          !sfx_peek(DRIVER_PLAYING_B)) {
        s->effect = 1; /* its crackle */
      }
      tv_static(x, y);
    } else {
      sprite(long_at(SCREENS + 4u * (uint32_t)((CELLS - k) & 1)), x, y);
      uint32_t list = c->list, frame = long_at(list);
      list += 4;
      if (!frame) {
        list = long_at(list);
        frame = long_at(list);
        list += 4;
      }
      c->list = list;
      const uint8_t *h = at(frame, 8);
      if (!h) continue;
      int dx = (int)(be16(h + 4) >> 1) - 16;
      int dy = (int)((be16(h + 6) + 1u) >> 1) - 14;
      sprite(frame, x + (int16_t)be16(h) - dx, y + (int16_t)be16(h + 2) - dy);
    }
  }
}

/* The hand, inside the window only ($11B74). */
static void hand(void) {
  int16_t clip = s->scr.clip;
  s->scr.clip = 104;
  sprite(s->hand, s->hand_x, s->hand_y);
  s->scr.clip = clip;
}

/* The three lights over or under the window ($10A46, $10A5E). */
static void lights(int y, int16_t counter) {
  uint32_t a = (counter & 4) ? LIGHT_B : LIGHT_A;
  sprite(a, 226, y);
  sprite(a, 258, y);
  sprite(a, 290, y);
}

/* The cursor: a lamp under a monitor, or a button ($11726, $1178E). */
static void cursor(bool lit) {
  if (s->row != 4) {
    int k = s->row * 5 + s->col;
    sprite(lit ? LAMP_ON : LAMP_OFF, cell_x(k) + 10, cell_y(k) + 31);
  } else if (s->col == 0) {
    sprite(lit ? EXIT_ON : EXIT_OFF, 4, 168);
  } else if (s->selling) {
    sprite(lit ? SELL_ON : SELL_OFF, 45, 168);
  } else {
    sprite(lit ? BUY_ON : BUY_OFF, 45, 168);
  }
}

/* ---- the offer ---- */

static void show(cell_t *c, uint16_t id) {
  c->id = id;
  c->list = long_at(LISTS + 4u * (id - 1u));
}

static bool offered(uint16_t id) {
  int16_t p = price_of(id);
  return p <= s->budget && (int32_t)p <= (int32_t)*s->g.cash;
}

/* What is for sale ($10B18): items 2-20 and MORE, or 21-25, each one the
 * budget and the cash allow, else "?". */
static void buy_list(void) {
  if ((int32_t)price_of((uint16_t)(s->page + 2)) > (int32_t)*s->g.cash) s->page = 0;
  uint16_t id = (uint16_t)(s->page + 2);
  int k = 0;
  for (;;) {
    show(&s->cells[k], offered(id) ? id : ITEM_UNKNOWN);
    id++;
    k++;
    if (id == ITEM_UNKNOWN) {
      while (k != CELLS - 1) s->cells[k++].id = 0;
      if (s->page) {
        show(&s->cells[k], ITEM_MORE);
      } else {
        s->cells[k].id = 0;
      }
      return;
    }
    if (k < CELLS - 1) continue;
    show(&s->cells[k], s->page || offered(id) ? ITEM_MORE : ITEM_UNKNOWN);
    return;
  }
}

/* After a purchase ($10C28): what the cash no longer pays turns to "?". */
static void remark(void) {
  for (int k = 0; k < CELLS; k++) {
    cell_t *c = &s->cells[k];
    if (!c->id) continue;
    if (c->id == ITEM_MORE) {
      if (k > 0 && s->cells[k - 1].id == ITEM_UNKNOWN) show(c, ITEM_UNKNOWN);
      continue;
    }
    if (c->id >= ITEM_UNKNOWN) continue;
    if ((int32_t)*s->g.cash < (int32_t)price_of(c->id)) show(c, ITEM_UNKNOWN);
  }
}

static uint8_t item_id(uint16_t type) {
  for (size_t k = 0; k < sizeof(k_ids) / sizeof(k_ids[0]); k++) {
    if (k_ids[k].type == type) return k_ids[k].id;
  }
  return 0;
}

/* What can be sold ($111E2): the weapons, newest first, but the forward
 * shot. */
static void sell_list(void) {
  const weapons_t *w = s->g.weapons;
  int k = 0;
  for (int8_t i = w->weapons; i >= 0 && k < CELLS; i = w->pool[i].next) {
    const weapon_t *a = &w->pool[i];
    if (a->flags & 1u || a->slot < 0) continue; /* dead, or not a slot's */
    uint16_t type = w->slots[a->slot].type;
    if (type == FORWARD_SHOT) continue;
    show(&s->cells[k], item_id(type));
    s->cells[k++].slot = a->slot;
  }
  while (k < CELLS) s->cells[k++].id = 0;
}

static void unknown_all(void) { /* the ending: nothing for sale ($10C06) */
  for (int k = 0; k < CELLS; k++) show(&s->cells[k], ITEM_UNKNOWN);
}

/* ---- room on the ship ($11024) ---- */

static uint16_t slot_type(int slot) { return weapons_slot_type(s->g.weapons, slot); }

static bool below_max(int slot) {
  const weapons_t *w = s->g.weapons;
  const weapon_t *a = &w->pool[w->slots[slot].obj];
  return a->power != a->max;
}

/* The weapon in `slot` is item `id` and can still power up. */
static bool same_below_max(int slot, uint16_t id) {
  return slot_type(slot) && item_id(slot_type(slot)) == id && below_max(slot);
}

static bool room(uint16_t id) {
  const ship_t *sh = s->g.ship;
  switch (id) {
    case 3: return sh->speed_ups != 2;
    case 4:
    case 7: return sh->shield != SHIP_SHIELD_FULL;
    case 5: return sh->fire_rate != 3;
    case 6: return *s->g.nashwan == 0;
    case 12:
      for (int k = 0; k < WEAPONS_SLOTS; k++) {
        if (slot_type(k) && below_max(k)) return true;
      }
      return false;
    case 16: return sh->dives == 0;
    case 22: return sh->lives != 9;
    case 24: return !sh->armour;
    case 25: return *s->g.shades == 0;
    default: break;
  }
  uint8_t cls = byte_at(CLASSES + id - 1u);
  if (cls & 1u) { /* the forward slot */
    if (slot_type(0) && item_id(slot_type(0)) == id) return below_max(0);
    return !slot_type(0) || slot_type(0) == FORWARD_SHOT;
  }
  if (cls & 2u) { /* a pod ($2BBC) */
    for (int k = 1; k <= 4; k++) {
      if (!slot_type(k)) return true;
    }
    return false;
  }
  if (cls & 4u) return !slot_type(5) || same_below_max(5, id);
  if (cls & 16u) return !slot_type(6) || !slot_type(5);
  if (cls & 8u) return !slot_type(6) || same_below_max(6, id);
  return true;
}

/* An item bought ($10E2E); ADVICE is a coroutine of its own. */
static void buy_item(uint16_t id) {
  shop_game_t *g = &s->g;
  switch (id) {
    case 6: *g->nashwan = (uint16_t)(*g->nashwan + 170u); break;
    case 13: weapons_make(g->weapons, 60); break;
    case 14: weapons_make(g->weapons, 192); break;
    case 22: g->ship->lives++; break;
    case 24: g->ship->armour = true; break;
    case 25: *g->shades = 220; break;
    default:
      for (size_t k = 0; k < sizeof(k_tokens) / sizeof(k_tokens[0]); k++) {
        if (k_tokens[k].id == id) g->hooks.take_token(g->hooks.ctx, k_tokens[k].token);
      }
      break;
  }
}

/* ---- waits ---- */

/* n frames of Crispin and the monitors ($10912). */
static bool frames(int n) {
  pt_t *pt = &s->pt_frames;
  PT_BEGIN(pt);
  s->frames_left = n;
  do {
    crispin();
    timers();
    monitors();
    FRAME(pt);
  } while (--s->frames_left);
  PT_END(pt);
}

/* n frames with the hand ($11B9A). */
static bool hand_frames(int n) {
  pt_t *pt = &s->pt_hands;
  PT_BEGIN(pt);
  s->hands_left = n;
  do {
    FRAME(pt);
    timers();
    monitors();
    crispin();
    hand();
  } while (--s->hands_left);
  PT_END(pt);
}

/* ---- text ---- */

static int word_length(uint32_t a) {
  int n = 0;
  for (uint8_t c; (c = byte_at(a)) && c != ' ' && c != '\r'; a++) n++;
  return n;
}

static void new_line(void) { /* $12430 */
  s->text_col = TEXT_COLUMN;
  s->text_line = (int16_t)(s->text_line + 7);
}

/* A string typed into the bubble ($122D0), a frame every second column,
 * his mouth moving. */
static bool type(uint32_t text) {
  pt_t *pt = &s->pt_type;
  PT_BEGIN(pt);
  s->text = text;
  for (;;) {
    s->talking = true;
    if (word_length(s->text) + s->text_col > TEXT_WRAP) new_line();
    for (;;) {
      uint8_t c = byte_at(s->text++);
      if (!c) break;
      if (c == ' ') {
        s->text_col++;
        while ((c = byte_at(s->text++)) == ' ') s->text_col++;
        if (!c) break;
        s->text--;
        break;
      }
      if (c == '\r') {
        new_line();
        break;
      }
      glyph(c);
      if (s->text_col & 1) {
        timers();
        monitors();
        crispin();
        FRAME(pt);
      }
    }
    if (!byte_at(s->text - 1)) break; /* the string's end */
  }
  s->talking = false;
  PT_END(pt);
}

/* A number typed, a digit a frame ($1234E). */
static bool number(uint16_t v) {
  pt_t *pt = &s->pt_number;
  PT_BEGIN(pt);
  s->n_digits = 0;
  do {
    s->digits[s->n_digits++] = (uint8_t)(v % 10u);
    v = (uint16_t)(v / 10u);
  } while (v);
  s->talking = true;
  for (s->digit = s->n_digits - 1; s->digit >= 0; s->digit--) {
    glyph((uint8_t)('0' + s->digits[s->digit]));
    FRAME(pt);
    timers();
    monitors();
    crispin();
    s->talking = false;
  }
  PT_END(pt);
}

/* The chosen cell's name, and when selling its power ($114CE). */
static bool name(void) {
  static const uint8_t k_powered[] = {1, 19, 18, 8, 10, 14, 9, 13};
  pt_t *pt = &s->pt_name;
  PT_BEGIN(pt);
  clear_text();
  PT_CALL(pt, &s->pt_type, type(long_at(NAMES + 4u * s->cells[s->cell].id)));
  if (s->selling && memchr(k_powered, s->cells[s->cell].id, sizeof(k_powered))) {
    PT_CALL(pt, &s->pt_type, type(S_POWER));
    PT_CALL(pt, &s->pt_number,
            number((uint16_t)(weapons_slot_power(s->g.weapons,
                                                 s->cells[s->cell].slot) + 1)));
  }
  PT_END(pt);
}

/* ADVICE ($10FE0): the level's queue, its first string, the rest moved up;
 * the third stays for ever. */
static bool give_advice(void) {
  pt_t *pt = &s->pt_advice;
  PT_BEGIN(pt);
  s->wink = 12;
  clear_text();
  {
    uint32_t *q = s->g.line > ADVICE_LINE ? &advice[0] : &advice[3];
    s->text = q[0];
    q[0] = q[1];
    q[1] = q[2];
  }
  PT_CALL(pt, &s->pt_type, type(s->text));
  s->wink = 0;
  PT_END(pt);
}

/* ---- input ---- */

/* The stick and the fire button until fire ($11646). */
static bool input(void) {
  pt_t *pt = &s->pt_input;
  PT_BEGIN(pt);
  for (;;) {
    if (s->repeat) {
      s->repeat--;
    } else if (s->fire) {
      s->fire = false;
      s->effect = 13;
      break;
    } else if (s->stick) {
      s->effect = 11;
      s->repeat = 4;
      cursor(false);
      if (s->stick & 4u) {
        if (s->row == 4) {
          s->col = s->col == 0;
        } else if (--s->col < 0) {
          s->col = 4;
        }
      } else if (s->stick & 8u) {
        if (s->row == 4) {
          s->col = s->col == 0;
        } else if (++s->col > 4) {
          s->col = 0;
        }
      } else if (s->stick & 1u) {
        if (--s->row < 0) s->row = 4;
      } else if (s->stick & 2u) {
        if (++s->row > 4) s->row = 0;
      }
      cursor(true);
    }
    crispin();
    timers();
    monitors();
    FRAME(pt);
  }
  PT_END(pt);
}

/* ---- Crispin's hand ---- */

/* He reaches to his headphones and switches his music off ($11BB6). */
static bool greeting(void) {
  pt_t *pt = &s->pt_hand;
  PT_BEGIN(pt);
  s->hand_x = 262;
  s->hand_y = 136;
  s->hand = HAND_UP;
  for (;;) {
    FRAME(pt);
    timers();
    monitors();
    crispin();
    hand();
    if (s->hand_x == 284) break;
    s->hand_x = (int16_t)(s->hand_x + 2);
    s->hand_y = (int16_t)(s->hand_y - 2);
  }
  s->hand = HAND_PRESS;
  PT_CALL(pt, &s->pt_hands, hand_frames(6));
  s->g.hooks.sound_stop(s->g.hooks.ctx);
  s->scr.timer = false;
  s->listening = false;
  s->hand = HAND_UP;
  PT_CALL(pt, &s->pt_hands, hand_frames(6));
  do {
    FRAME(pt);
    timers();
    monitors();
    crispin();
    hand();
    s->hand_x = (int16_t)(s->hand_x - 2);
    s->hand_y = (int16_t)(s->hand_y + 2);
  } while (s->hand_x > 262);
  PT_END(pt);
}

/* He prices what the player sells ($11380). */
static bool pricing(void) {
  static const uint32_t k_hands[9] = {HAND_UP, HAND_TURN, HAND_UP,
                                      HAND_PRESS, HAND_UP, HAND_TURN,
                                      HAND_UP, HAND_PRESS, HAND_UP};
  pt_t *pt = &s->pt_hand;
  PT_BEGIN(pt);
  s->hand_x = 247;
  s->hand_y = 120;
  s->hand = HAND_UP;
  do {
    FRAME(pt);
    crispin();
    timers();
    monitors();
    hand();
    s->hand_y = (int16_t)(s->hand_y - 2);
  } while (s->hand_y > 104);
  for (s->hand_n = 0; s->hand_n < 9; s->hand_n++) {
    s->hand = k_hands[s->hand_n];
    PT_CALL(pt, &s->pt_hands, hand_frames(3));
  }
  do {
    FRAME(pt);
    crispin();
    timers();
    monitors();
    hand();
    s->hand_y = (int16_t)(s->hand_y + 2);
  } while (s->hand_y < 128);
  PT_CALL(pt, &s->pt_type, type(S_PAY));
  { /* half its price and 2000 a power, as a word */
    const cell_t *c = &s->cells[s->cell];
    uint16_t p = (uint16_t)word_at(PRICES + 2u * (c->id - 1u));
    uint16_t power = (uint16_t)weapons_slot_power(s->g.weapons, c->slot);
    s->price = (int16_t)((uint16_t)(p + (uint16_t)(power * 2000u)) >> 1);
  }
  PT_CALL(pt, &s->pt_number, number((uint16_t)s->price));
  PT_END(pt);
}

/* ---- the monitors and shutters ---- */

/* The monitors switch on ($11C50). */
static bool monitors_on(void) {
  pt_t *pt = &s->pt_on_off;
  PT_BEGIN(pt);
  for (;;) {
    FRAME(pt);
    for (int k = 0; k < CELLS; k++) s->cells[k].state++;
    if (s->cells[0].state == 0) break;
    monitors();
    crispin();
  }
  for (int k = 0; k < CELLS; k++) s->cells[k].state = 1;
  for (s->on_off_n = 4;;) {
    monitors();
    crispin();
    if (!--s->on_off_n) break;
    FRAME(pt);
  }
  PT_END(pt);
}

/* The monitors switch off ($11CAA). */
static bool monitors_off(void) {
  pt_t *pt = &s->pt_on_off;
  PT_BEGIN(pt);
  for (int k = 0; k < CELLS; k++) s->cells[k].state = 0;
  for (;;) {
    FRAME(pt);
    for (int k = 0; k < CELLS; k++) s->cells[k].state--;
    if (s->cells[0].state == -9) break;
    monitors();
    crispin();
  }
  for (s->on_off_n = 10;;) {
    monitors();
    crispin();
    if (!--s->on_off_n) break;
    FRAME(pt);
  }
  PT_END(pt);
}

/* The window and the bubble open, a frame a step, sample 0 as the window
 * opens ($109A6). */
static bool shutters_open(void) {
  pt_t *pt = &s->pt_shut;
  PT_BEGIN(pt);
  for (;;) {
    FRAME(pt);
    if (s->shutter) {
      s->shutter = (int16_t)(s->shutter - 4);
      if (s->shutter <= 0) {
        s->g.hooks.sample(s->g.hooks.ctx, true);
        s->scr.timer = true;
        s->shutter = 0;
      }
    }
    if (s->bubble) {
      s->bubble = (int16_t)(s->bubble - 4);
      if (s->bubble < 0) s->bubble = 0;
    }
    monitors();
    crispin();
    bubble_shutter();
    lights(4, s->shutter);
    lights(109, s->bubble);
    if (!s->shutter && !s->bubble) break;
  }
  clear_bubble();
  PT_END(pt);
}

/* They close, a frame a step ($109F4). */
static bool shutters_close(void) {
  pt_t *pt = &s->pt_shut;
  PT_BEGIN(pt);
  clear_bubble();
  do {
    FRAME(pt);
    if (s->bubble != 68) s->bubble = (int16_t)(s->bubble + 4 > 68 ? 68 : s->bubble + 4);
    if (s->shutter != 48) s->shutter = (int16_t)(s->shutter + 4 > 48 ? 48 : s->shutter + 4);
    monitors();
    crispin();
    bubble_shutter();
    lights(109, s->bubble);
    lights(4, s->shutter);
  } while (s->bubble != 68);
  PT_END(pt);
}

/* ---- the two phases ---- */

/* The monitor chosen again (its offer accepted). */
static bool chosen_twice(void) {
  return s->col == s->last_col && s->row == s->last_row;
}

/* Selling ($1122E): EXIT goes on to buying. */
static bool sell_loop(void) {
  pt_t *pt = &s->pt_loop;
  PT_BEGIN(pt);
  s->col = s->row = 0;
  s->last_col = -1;
  cursor(true);
  s->price = -1;
  for (;;) {
    s->fire = false;
    PT_CALL(pt, &s->pt_input, input());
    if (s->row == 4 && s->col == 0) {
      cursor(false);
      break;
    }
    if (s->row != 4 && !chosen_twice()) {
      int k = s->row * 5 + s->col;
      if (!s->cells[k].id) continue;
      s->last_col = s->col;
      s->last_row = s->row;
      s->cell = k;
      s->glance = -1;
      PT_CALL(pt, &s->pt_name, name());
      PT_CALL(pt, &s->pt_hand, pricing());
      s->glance = 0;
      continue;
    }
    s->last_col = -1; /* SELL, or the same monitor again */
    if (s->price < 0) continue;
    *s->g.cash += (uint32_t)s->price;
    s->price = -1;
    cash();
    s->cells[s->cell].id = 0;
    weapons_remove(s->g.weapons, s->cells[s->cell].slot); /* $6474 */
    clear_text();
    PT_CALL(pt, &s->pt_type, type(S_THANKS));
  }
  PT_END(pt);
}

/* Buying ($10C88): EXIT leaves the shop. */
static bool buy_loop(void) {
  pt_t *pt = &s->pt_loop;
  PT_BEGIN(pt);
  s->col = s->row = 0;
  s->last_col = -1;
  cursor(true);
  s->price = -1;
  for (;;) {
    s->fire = false;
    PT_CALL(pt, &s->pt_input, input());
    if (s->row == 4 && s->col == 0) {
      cursor(false);
      break;
    }
    if (s->row != 4 && !chosen_twice()) {
      int k = s->row * 5 + s->col;
      if (!s->cells[k].id) continue;
      if (s->cells[k].id == ITEM_UNKNOWN) {
        clear_text();
        PT_CALL(pt, &s->pt_type, type(S_OUT_OF_STOCK));
        s->price = -1;
        continue;
      }
      if (s->cells[k].id == ITEM_MORE) {
        s->page = (int16_t)(s->page + 19 >= 25 ? 0 : s->page + 19);
        buy_list();
        s->price = -1;
        continue;
      }
      s->last_col = s->col;
      s->last_row = s->row;
      s->cell = k;
      PT_CALL(pt, &s->pt_name, name());
      PT_CALL(pt, &s->pt_type, type(S_COST)); /* $11346 */
      s->price = price_of(s->cells[s->cell].id);
      PT_CALL(pt, &s->pt_number, number((uint16_t)s->price));
      continue;
    }
    s->last_col = -1; /* BUY, or the same monitor again */
    if (s->price < 0) continue;
    if (!room(s->cells[s->cell].id)) {
      clear_text();
      PT_CALL(pt, &s->pt_type, type(S_NO_ROOM));
      s->price = -1;
      continue;
    }
    clear_text();
    PT_CALL(pt, &s->pt_type, type(S_CASH));
    for (s->pay = s->price; s->pay >= 100; s->pay = (int16_t)(s->pay - 100)) {
      s->effect = 13;
      *s->g.cash -= 100u;
      cash();
      crispin();
      timers();
      monitors();
      FRAME(pt);
    }
    *s->g.cash -= (uint32_t)s->pay;
    if (s->cells[s->cell].id == ITEM_ADVICE) {
      PT_CALL(pt, &s->pt_advice, give_advice());
    } else {
      buy_item(s->cells[s->cell].id);
    }
    s->price = -1;
    cash();
    remark();
    if (s->cells[s->cell].id != ITEM_ADVICE) {
      PT_CALL(pt, &s->pt_type, type(S_ELSE));
    }
  }
  PT_END(pt);
}

/* The window shut and the screen faded ($106F6, $107C6). */
static bool close_up(void) {
  pt_t *pt = &s->pt_close;
  PT_BEGIN(pt);
  s->effect = 23;
  PT_CALL(pt, &s->pt_shut, shutters_close());
  s->effect = 22;
  FRAME(pt);
  VBL_WAIT(pt); /* the effect taken */
  while (sfx_peek(DRIVER_PLAYING_B)) VBL_WAIT(pt);
  monitors();
  crispin();
  bubble_shutter();
  FRAME(pt);
  monitors();
  crispin();
  bubble_shutter();
  PT_CALL(pt, &s->scr.pt_fade, screen_fade_out(&s->scr, 2));
  PT_END(pt);
}

/* The screen as the shop is entered: the monitors' frames and the
 * panels. */
static void backdrop(void) {
  memset(fb_chunked_buffer, 0, FB_CHUNKED_SIZE);
  spend(SCREEN_CYCLES);
  for (int y = 0; y < 164; y += 41) {
    for (int x = 0; x < 205; x += 41) {
      sprite(MONITOR_A, x, y);
      sprite(MONITOR_B, x, y);
    }
  }
  for (uint32_t a = BLOCKS; long_at(a); a += 10) {
    uint32_t off = word_at(a + 4);
    int groups = (int)word_at(a + 6) + 1;
    blocks(long_at(a), (int)(off % 160u / 8u * 16u), (int)(off / 160u), groups,
           (int)word_at(a + 8) + 1, 8 * groups, 0, 0);
  }
  if (s->g.player) sprite(PLAYER_2, 193, 188);
}

/* The set switched off at the game's end ($108C4): a dot in the middle. */
static void set_off(void) {
  memset(fb_chunked_buffer, 0, FB_CHUNKED_SIZE);
  for (int l = 0; l < 3; l++) {
    uint8_t *row = &fb_chunked_buffer[(100 + l) * FB_CHUNKED_W + 172];
    row[0] = row[3] = l == 1 ? 7 : 5;
    row[1] = row[2] = 7;
  }
}

/* The whole visit ($10520). */
static bool visit(void) {
  pt_t *pt = &s->pt_main;
  PT_BEGIN(pt);
  s->scr.clip = 199;
  PT_CALL(pt, &s->scr.pt_fade, screen_fade_out(&s->scr, 2));
  backdrop();
  s->selling = s->listening = true;
  s->wink = 0;
  s->page = 0;
  for (int k = 0; k < CELLS; k++) s->cells[k].state = -9;
  s->shutter = 48;
  s->bubble = 68;
  s->talking = false;
  s->col = 1;
  s->row = 4;
  cursor(false);
  cash();
  monitors();
  crispin();
  bubble_shutter();
  FRAME(pt);
  monitors();
  crispin();
  bubble_shutter();
  palette_at(PALETTE_SHOP, s->to);
  PT_CALL(pt, &s->scr.pt_fade, screen_fade_in(&s->scr, s->to));
  s->effect = 22;
  VBL_WAIT(pt); /* the effect taken */
  while (sfx_peek(DRIVER_PLAYING_B)) VBL_WAIT(pt);
  s->effect = 23;
  PT_CALL(pt, &s->pt_shut, shutters_open());
  if (s->ending) {
    unknown_all();
  } else {
    sell_list();
  }
  PT_CALL(pt, &s->pt_on_off, monitors_on());
  PT_CALL(pt, &s->pt_hand, greeting());
  clear_text();
  if (s->ending) { /* $10792 */
    PT_CALL(pt, &s->pt_type, type(S_VIEWERS_1));
    PT_CALL(pt, &s->pt_frames, frames(30));
    PT_CALL(pt, &s->pt_type, type(S_VIEWERS_2));
    PT_CALL(pt, &s->pt_frames, frames(30));
    clear_text();
    PT_CALL(pt, &s->pt_type, type(S_VIEWERS_3));
    PT_CALL(pt, &s->pt_frames, frames(17));
    PT_CALL(pt, &s->pt_on_off, monitors_off());
    PT_CALL(pt, &s->pt_close, close_up());
    s->effect = 46;
    for (s->frames_left = 150; s->frames_left; s->frames_left--) {
      set_off();
      FRAME(pt);
      palette_at(PALETTE_ENDING, s->to);
      screen_palette(&s->scr, s->to);
    }
    PT_CALL(pt, &s->scr.pt_fade, screen_fade_out(&s->scr, 4));
    s->g.hooks.sample(s->g.hooks.ctx, false);
    s->scr.timer = false;
    for (s->frames_left = 80; s->frames_left; s->frames_left--) FRAME(pt);
  } else {
    if (s->g.level_number == 5) {
      PT_CALL(pt, &s->pt_type, type(S_BASEMENT));
      PT_CALL(pt, &s->pt_hands, hand_frames(17));
      clear_text();
      PT_CALL(pt, &s->pt_type, type(S_MUST_GO));
      PT_CALL(pt, &s->pt_hands, hand_frames(17));
      clear_text();
      PT_CALL(pt, &s->pt_type, type(S_LAST_CHANCE));
    } else {
      PT_CALL(pt, &s->pt_type, type(S_SELL));
    }
    PT_CALL(pt, &s->pt_loop, sell_loop());
    PT_CALL(pt, &s->pt_on_off, monitors_off());
    s->selling = false;
    s->col = 1;
    s->row = 4;
    cursor(false);
    buy_list();
    PT_CALL(pt, &s->pt_on_off, monitors_on());
    clear_text();
    PT_CALL(pt, &s->pt_type, type(S_BUY));
    PT_CALL(pt, &s->pt_loop, buy_loop());
    PT_CALL(pt, &s->pt_on_off, monitors_off());
    PT_CALL(pt, &s->pt_close, close_up());
  }
  /* $1072C: out of the shop */
  s->scr.clip = 191;
  if (!weapons_slot_type(s->g.weapons, 0)) weapons_make(s->g.weapons, FORWARD_SHOT);
  weapons_save(s->g.weapons); /* $3DA2: no cash saved */
  if (s->end_shop) return true;
  { /* the level's palette, halved a long at a time in Bitmap Shades */
    uint32_t p = long_at(L_PALETTE);
    for (int i = 0; i < 16; i += 2) {
      uint32_t v = long_at(p + 2u * (uint32_t)i);
      if (*s->g.shades) v >>= 1;
      s->to[i] = (uint16_t)(v >> 16);
      s->to[i + 1] = (uint16_t)v;
    }
  }
  VBL_WAIT(pt);
  s->g.hooks.redraw(s->g.hooks.ctx);
  FRAME(pt);
  PT_CALL(pt, &s->scr.pt_fade, screen_fade_in(&s->scr, s->to));
  PT_END(pt);
}

/* ---- the game's side ---- */

void shop_level_start(const x2level_header_t *level) {
  memset(advice, 0, sizeof(advice));
  advice[2] = advice[5] = ADVICE_LAST;
  const uint8_t *hook = x2level_raw(level, L_START_HOOK, 4);
  if (!hook || hook[0] != 0x60 || hook[1] != 0x00) return;
  uint32_t start = L_START_HOOK + 2u + (uint32_t)(int16_t)(hook[2] << 8 | hook[3]);
  for (uint32_t a = start; a < start + 0x200u; a += 2) {
    const uint8_t *p = x2level_raw(level, a, 8);
    if (!p || p[0] != 0x21 || p[1] != 0xFC) continue; /* move.l #imm,abs.w */
    uint32_t imm = be32(p + 2), to = be16(p + 6);
    if (to >= V_ADVICE && to < V_ADVICE + 24u && (to - V_ADVICE) % 4u == 0) {
      advice[(to - V_ADVICE) / 4u] = imm;
    }
  }
}

void shop_advice_get(uint32_t queues[6]) { memcpy(queues, advice, sizeof(advice)); }

void shop_advice_set(const uint32_t queues[6]) { memcpy(advice, queues, sizeof(advice)); }

bool shop_start(const shop_game_t *game) {
  if (!game->image) return false;
  free(s);
  s = calloc(1, sizeof(shop_t));
  if (!s) return false;
  s->g = *game;
  shop_game_t *g = &s->g;
  screen_start(&s->scr, g->image, g->global, g->level, g->palette);
  s->scr.frame_cycles = FRAME_CYCLES;
  if (!machines_set) {
    for (uint32_t k = 0; k < 7; k++) machines[k] = long_at(MACHINES + 8 * k + 4);
    machines_set = true;
  }

  /* $7CF8: the driver stopped, the budget and the cash's floor */
  g->hooks.sound_stop(g->hooks.ctx);
  bool end = g->line <= END_SHOP_LINE;
  s->budget = (int16_t)word_at(end ? L_BUDGET_END : L_BUDGET_MID);
  int32_t floor = s->budget + (end ? 400 : 0);
  if (floor > (int32_t)*g->cash) {
    *g->cash = (uint32_t)floor + 50u * (enemies_random(g->enemies) & 3u);
  }
  g->ship->armour = false; /* PROTECTION lasts until the next shop */
  s->ending = g->level_number == 5 && g->line <= ENDING_LINE;
  s->end_shop = end;
  s->effect = -1;
  return true;
}

bool shop_vbl(uint8_t stick, bool fire) {
  if (!s) return false;
  screen_tick(&s->scr);
  s->stick = stick;
  if (fire) s->fire = true;
  bool on = !visit();
  if (s->effect >= 0) { /* the next VBL's handler takes the request */
    sfx_request(s->effect, SFX_CHANNEL_A);
    s->effect = -1;
  }
  if (on) return true;
  free(s);
  s = NULL;
  return false;
}
