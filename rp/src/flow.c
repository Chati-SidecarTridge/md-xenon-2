/**
 * File: flow.c
 * Description: The game's screens outside play and the shop (flow.h),
 *              ported from the original's $7F44-$8F88.
 *
 * Every screen draws over the same 3-D stars and with the same two text
 * routines: a line of twenty 16 x 22 characters drawn as it is ($7FF2) or
 * zoomed ($803A: the lines and pixels a scale's 16-bit pattern keeps of
 * each character, packed), and the logo zoomed the same way ($8152). The
 * messages run a script ($8530): a table of scales, the text moving from
 * the middle to its place as it grows, the step 17 waiting for fire.
 */

#pragma GCC optimize("O3")

#include "flow.h"

#include <string.h>

#include "fb_chunked.h"
#include "palette.h"
#include "rng.h"
#include "screen.h"

/* Where the original keeps the screens' tables, texts and pictures. */
#define CHARMAP 0x871Au      /* the big font's characters, NUL ended */
#define LETTERS 0x8744u      /* the small font's, and the initials' order */
#define MASKS 0x876Cu        /* a word a scale: the lines and pixels kept */
#define CREDITS 0x878Eu      /* six pairs of 20-character lines */
#define CREDITS_END 0x887Eu
#define GET_READY 0x887Eu    /* " GET READY PLAYER 1 " */
#define PLAYER_DIGIT 18
#define GAME_OVER 0x8892u
#define PALETTE 0x86FAu
#define STAR_COLOURS 0x867Cu /* by depth, eight */
#define ZOOM_CREDITS 0x83D4u /* the credits' three phases, each ended by -1 */
#define ZOOM_MESSAGE 0x83FEu /* in, held (17: fire when asked), out */
#define ZOOM_IN 0x8BECu
#define ZOOM_OUT 0x8C0Eu
#define HIGH_SCORE 0x8C32u
#define SELECT_TEXT 0x8DC8u
#define ONE_PLAYER 0x8DDCu   /* each line, then itself selected 20 later */
#define TWO_PLAYERS 0x8E04u
#define MUSIC_ON 0x8E2Cu
#define MUSIC_OFF 0x8E54u
#define CONTINUE_TEXT 0x8F22u
#define COUNTDOWN 0x8F36u    /* "         9          " */
#define CREDITS_TEXT 0x8F6Au /* "CREDITS 3" */
#define SCORES_TEXT 0x8966u  /* the empty table's lines */
#define SCORE_POWERS 0x8BCCu /* 1000000 ... 1, 0 */
#define FONT_BIG 0x9DC0u     /* 176 bytes a character: 22 lines of 16 */
#define FONT_SMALL 0xB9F0u   /* 128 bytes: 16 lines of 16 */
#define LOGO 0xCCF0u         /* 13 groups, 54 lines */

#define STARS 48
#define STAR_STEP (512u * 256u) /* a frame's depth step, in 256ths */
#define LINE_BYTES 160
#define BIG_LINES 22
#define LOGO_GROUPS 13
#define LOGO_LINES 54
#define NO_NAME ':'

/* What the screens' drawing costs on the ST, in cycles (fitted to Hatari's
 * timings of the attract: tools/re/scripts/flow-pace.exp). */
#define FRAME_CYCLES 24440u     /* each frame besides its drawing */
#define CLEAR_CYCLES 94400u     /* the screen cleared ($7F96) */
#define LOGO_LINE_CYCLES 561u   /* a line of the logo drawn as it is */
#define CHAR_CYCLES 1642u       /* a character looked up */
#define GLYPH_LINE_CYCLES 13u   /* a line of 16 pixels copied */
#define ZOOM_LINE_CYCLES 1368u  /* a character's line zoomed */
#define ZOOM_SKIP_CYCLES 0u     /* a line the zoom leaves out */
#define LOGO_ZOOM_LINE_CYCLES 17352u /* a line of the logo zoomed */
#define LOGO_ZOOM_SKIP_CYCLES 0u
#define STAR_CYCLES 31u         /* a star moved and drawn */

#define FRAME(pt) PT_WAIT(pt, frame())
#define SCENE_ITEMS 11 /* the table: its title, ten lines */

typedef enum { K_ATTRACT, K_SELECT, K_MESSAGE, K_CONTINUE, K_SCORE } kind_t;

typedef struct {
  int16_t x, y;
  uint32_t z; /* in 256ths */
} star_t;

typedef enum { I_LOGO, I_TEXT, I_SMALL } item_kind_t;

/* Something a frame draws: the logo or a line of the big font at a scale
 * (sixteenths), on a line (or moving with its scale from the middle to
 * it); a line of the small font, `scale` characters at (centre, y). */
typedef struct {
  const uint8_t *text;
  int16_t scale, centre, y;
  uint8_t kind;
  bool moves;
} item_t;

typedef struct {
  int n;
  item_t item[SCENE_ITEMS];
} scene_t;

typedef struct {
  screen_t scr;
  flow_hooks_t hooks;
  flow_scores_t *scores;
  uint32_t rng[2];
  star_t stars[STARS];
  scene_t scene, shown; /* the frame recorded or shown, the one before */
  bool recording;       /* a frame begun, not yet shown */
  uint32_t from, steps;       /* the VBL the scene was recorded, its VBLs */
  uint32_t moved;             /* the stars' depth step taken so far */
  bool moving;
  uint8_t credits_text[10];
  kind_t kind;
  int result;
  uint8_t a00;        /* the stick as its last report left it ($A00) */
  uint8_t last_stick;
  bool fire;          /* the fire latch ($A01) */
  uint16_t saved[16]; /* the palette before ($99CA) */
  /* the coroutines' places and their variables */
  pt_t pt_main, pt_script, pt_logo, pt_table;
  uint8_t text[20];   /* a message being shown */
  const uint8_t *script_text;
  int16_t script_centre;
  uint32_t script_table;
  int script_index;
  bool script_wait;   /* $84C2: the step 17 waits for fire */
  uint32_t credit;    /* $7F38 */
  int phase, index;   /* $7F40, $7F42 */
  int16_t scale, line; /* $7F3C, $7F3E */
  int n, sel, credits, letter, repeat;
  int8_t step;
  uint8_t held;
  bool *music;
  int slot;
  uint32_t score;
} flow_t;

static flow_t f;

/* ---- the original's memory ---- */

static uint32_t word_at(uint32_t a) { return screen_word(&f.scr, a); }
static uint32_t long_at(uint32_t a) { return screen_long(&f.scr, a); }
static uint8_t byte_at(uint32_t a) { return screen_byte(&f.scr, a); }
static const uint8_t *text_at(uint32_t a) {
  static const uint8_t k_blank[20] = "                   ";
  const uint8_t *p = screen_at(&f.scr, a, 20);
  return p ? p : k_blank;
}
static uint16_t rnd(void) { return (uint16_t)rng_next(f.rng); }
static void spend(uint32_t cycles) { screen_spend(&f.scr, cycles); }

/* A character's place in a font's table ($7FD8), -1 if it has none. */
static int lookup(uint8_t c, uint32_t table) {
  for (int i = 0;; i++) {
    uint8_t t = byte_at(table + (uint32_t)i);
    if (!t) return -1;
    if (t == c) return i;
  }
}

/* ---- the picture ----
 *
 * Each of the original's frames is recorded as a scene, a list of what it
 * draws (the logo, a line of the big font at a scale, a line of the small
 * font), its drawing costed on the ST's clock as the original's takes. The
 * scene is shown at 50 frames a second: over the VBLs until the original
 * would show it, the scales of what both frames hold move from the last
 * scene's to the new one's, and what zooms in or out grows from or shrinks
 * to nothing; what is drawn as it is changes at the frame. A scale below 16
 * is drawn scaled pixel for pixel, 16 by the original's routines. */

static void clear(void) { /* $7F96 */
  if (!f.recording) f.shown = f.scene; /* a frame left unshown is redone */
  f.recording = true;
  f.scene.n = 0;
  spend(CLEAR_CYCLES);
}

static void record(uint8_t kind, const uint8_t *text, int scale, int centre,
                   int y, bool moves) {
  if (f.scene.n >= SCENE_ITEMS) return;
  f.scene.item[f.scene.n++] = (item_t){text, (int16_t)scale, (int16_t)centre,
                                     (int16_t)y, kind, moves};
}

/* The lines of `lines` a scale's pattern keeps, rotating ($803A, $8152). */
static int rows_kept(int scale, int lines) {
  uint32_t rows = word_at(MASKS + 2u * (uint32_t)scale);
  int n = 0;
  for (int l = 0; l < lines; l++) {
    n += (rows & 0x8000u) != 0;
    rows = ((rows << 1) | (rows >> 15)) & 0xFFFFu;
  }
  return n;
}

/* The logo as it is, at (48, 20), the screen cleared first ($7F44). */
static void logo(void) {
  clear();
  record(I_LOGO, NULL, 16, 0, 0, false);
  spend(LOGO_LINE_CYCLES * LOGO_LINES);
}

/* A line of the big font zoomed to `scale` sixteenths ($803A; 16: as it
 * is, $7FF2) on `line`, or, when it `moves`, on the line its scale puts
 * it between the screen's middle and `line` ($8530). */
static void text_line(const uint8_t *text, int scale, int line, bool moves) {
  record(I_TEXT, text, scale, line, 0, moves);
  int kept = scale >= 16 ? BIG_LINES : rows_kept(scale, BIG_LINES);
  uint32_t per = scale >= 16 ? GLYPH_LINE_CYCLES : ZOOM_LINE_CYCLES;
  for (int c = 0; c < 20; c++) {
    spend(CHAR_CYCLES);
    if (lookup(text[c], CHARMAP) >= 0) spend(per * (uint32_t)kept);
  }
}

static void text_static(const uint8_t *text, int line) { text_line(text, 16, line, false); }
static void text_zoom(const uint8_t *text, int scale, int line) { text_line(text, scale, line, false); }

/* The logo zoomed to `scale` sixteenths about the screen's middle ($8152). */
static void logo_zoom(int scale) {
  record(I_LOGO, NULL, scale, 0, 0, false);
  spend(LOGO_ZOOM_LINE_CYCLES * (uint32_t)rows_kept(scale, LOGO_LINES));
}

/* A line of the small font from (x, y) ($1B1E, $8B8A): spaces skipped. */
static void small_text(const uint8_t *text, int n, int x, int y) {
  record(I_SMALL, text, n, x, y, false);
  for (int c = 0; c < n && text[c]; c++) {
    spend(CHAR_CYCLES);
    if (text[c] != ' ' && lookup(text[c], LETTERS) >= 0) spend(GLYPH_LINE_CYCLES * 16);
  }
}

/* "CREDITS n" at the screen's foot ($8F4A). */
static void credits_left(void) {
  const uint8_t *src = screen_at(&f.scr, CREDITS_TEXT, 9);
  if (!src) return;
  memcpy(f.credits_text, src, 9);
  f.credits_text[8] = (uint8_t)('0' + f.credits);
  f.credits_text[9] = 0;
  small_text(f.credits_text, 9, 176, 184);
}

/* The stars come closer ($85B4); costed here, moved and drawn a VBL at a
 * time. */
static void stars(void) { spend(STAR_CYCLES * STARS); }

/* ---- drawing a scene ---- */

/* 16 pixels written over the screen's: four plane words. */
static void put(int x, int y, const uint8_t *p) {
  screen_group(x, y, (uint32_t)p[0] << 8 | p[1], (uint32_t)p[2] << 8 | p[3],
               (uint32_t)p[4] << 8 | p[5], (uint32_t)p[6] << 8 | p[7], 0);
}

/* A pixel of a planar picture `width` groups across. */
static uint8_t planar_pixel(const uint8_t *d, int groups, int u, int v) {
  const uint8_t *g = d + (v * groups + u / 16) * 8;
  uint32_t bit = 0x8000u >> (u & 15);
  return (uint8_t)((((uint32_t)g[0] << 8 | g[1]) & bit ? 1 : 0) |
                   (((uint32_t)g[2] << 8 | g[3]) & bit ? 2 : 0) |
                   (((uint32_t)g[4] << 8 | g[5]) & bit ? 4 : 0) |
                   (((uint32_t)g[6] << 8 | g[7]) & bit ? 8 : 0));
}

/* The logo at `s` 256ths of a sixteenth, from (160 - 7s, 100 - 5s). */
static void draw_logo(int s) {
  const uint8_t *d = screen_at(&f.scr, LOGO, LOGO_GROUPS * 8 * LOGO_LINES);
  if (!d || s <= 0) return;
  if (s >= 4096) {
    for (int l = 0; l < LOGO_LINES; l++) {
      for (int g = 0; g < LOGO_GROUPS; g++) put(48 + 16 * g, 20 + l, d + (l * LOGO_GROUPS + g) * 8);
    }
    return;
  }
  int x0 = 160 * 256 - 7 * s, y0 = 100 * 256 - 5 * s;
  for (int y = y0 >> 8; y <= (y0 + LOGO_LINES * s / 16) >> 8; y++) {
    int v = ((y * 256 + 128 - y0) * 16) / s;
    if (y < 0 || y >= FB_CHUNKED_H || v < 0 || v >= LOGO_LINES) continue;
    uint8_t *row = &fb_chunked_buffer[y * FB_CHUNKED_W];
    for (int x = x0 >> 8; x <= (x0 + LOGO_GROUPS * s) >> 8; x++) {
      int u = ((x * 256 + 128 - x0) * 16) / s;
      if (x < 0 || x >= FB_CHUNKED_W || u < 0 || u >= LOGO_GROUPS * 16) continue;
      row[x] = planar_pixel(d, LOGO_GROUPS, u, v);
    }
  }
}

/* A line of the big font at `s` 256ths of a sixteenth on `line`, from
 * x = 160 - 10s: its pixels ORed into the screen's. */
static void draw_text(const uint8_t *text, int s, int line) {
  if (s <= 0) return;
  if (s >= 4096) {
    for (int c = 0; c < 20; c++) {
      int i = lookup(text[c], CHARMAP);
      const uint8_t *g = i < 0 ? NULL : screen_at(&f.scr, FONT_BIG + (uint32_t)i * 176u, 176);
      if (!g) continue;
      for (int l = 0; l < BIG_LINES; l++) put(16 * c, line - 8 + l, g + 8 * l);
    }
    return;
  }
  const uint8_t *glyph[20];
  for (int c = 0; c < 20; c++) {
    int i = lookup(text[c], CHARMAP);
    glyph[c] = i < 0 ? NULL : screen_at(&f.scr, FONT_BIG + (uint32_t)i * 176u, 176);
  }
  int x0 = 160 * 256 - 10 * s, y0 = line * 256 - s / 2;
  for (int y = y0 >> 8; y <= (y0 + BIG_LINES * s / 16) >> 8; y++) {
    int v = ((y * 256 + 128 - y0) * 16) / s;
    if (y < 0 || y >= FB_CHUNKED_H || v < 0 || v >= BIG_LINES) continue;
    uint8_t *row = &fb_chunked_buffer[y * FB_CHUNKED_W];
    for (int x = x0 >> 8; x <= (x0 + 20 * s) >> 8; x++) {
      int u = ((x * 256 + 128 - x0) * 16) / s;
      if (x < 0 || x >= FB_CHUNKED_W || u < 0 || u >= 320) continue;
      const uint8_t *g = glyph[u / 16];
      if (g) row[x] |= planar_pixel(g, 1, u & 15, v);
    }
  }
}

static void draw_small(const uint8_t *text, int n, int x, int y) {
  for (int c = 0; c < n && text[c]; c++, x += 16) {
    if (text[c] == ' ') continue;
    int i = lookup(text[c], LETTERS);
    const uint8_t *g = i < 0 ? NULL : screen_at(&f.scr, FONT_SMALL + (uint32_t)i * 128u, 128);
    if (!g) continue;
    for (int l = 0; l < 16; l++) put(x, y + l, g + 8 * l);
  }
}

/* An item at `s` 256ths of a sixteenth. */
static void draw_item(const item_t *it, int s) {
  switch (it->kind) {
    case I_LOGO:
      draw_logo(s);
      break;
    case I_TEXT:
      draw_text(it->text, s,
                it->moves ? (((it->centre - 100) * s) >> 12) + 100 : it->centre);
      break;
    case I_SMALL:
      draw_small(it->text, it->scale, it->centre, it->y);
      break;
  }
}

static bool same(const item_t *a, const item_t *b) {
  return a->kind == b->kind && a->text == b->text &&
         (a->kind != I_SMALL || (a->centre == b->centre && a->y == b->y));
}

static const item_t *find(const scene_t *sc, const item_t *it) {
  for (int i = 0; i < sc->n; i++) {
    if (same(&sc->item[i], it)) return &sc->item[i];
  }
  return NULL;
}

/* What zooms: an item that grows from or shrinks to nothing. */
static bool zooms(const item_t *it) { return it->kind != I_SMALL && it->scale < 16; }

/* The scene `j` VBLs of `w` from the last. */
static void draw_scene(uint32_t j, uint32_t w) {
  memset(fb_chunked_buffer, 0, FB_CHUNKED_SIZE);
  bool at_end = j >= w;
  for (int i = 0; i < f.shown.n; i++) { /* what goes */
    const item_t *it = &f.shown.item[i];
    if (find(&f.scene, it)) continue;
    if (at_end) continue;
    if (zooms(it)) {
      draw_item(it, (int)((uint32_t)it->scale * 256u * (w - j) / w));
    } else {
      draw_item(it, it->scale * 256);
    }
  }
  for (int i = 0; i < f.scene.n; i++) { /* what stays or comes */
    const item_t *it = &f.scene.item[i];
    const item_t *was = find(&f.shown, it);
    int s1 = it->scale * 256;
    int s0 = was ? was->scale * 256 : zooms(it) ? 0 : -1;
    if (at_end || s0 == s1) {
      draw_item(it, s1);
    } else if (s0 >= 0) {
      draw_item(it, s0 + (int)((int64_t)(s1 - s0) * j / w));
    }
  }
}

/* ---- the stars ---- */

static void stars_seed(void) { /* $82D2, $84DC */
  for (int k = 0; k < STARS; k++) {
    f.stars[k].x = (int16_t)rnd();
    f.stars[k].y = (int16_t)rnd();
    f.stars[k].z = (uint32_t)(rnd() & 8191u) << 8;
  }
}

/* The stars `d` 256ths of a depth step closer, respawned far when near or
 * off the screen ($85B4). */
static void stars_move(uint32_t d) {
  for (int k = 0; k < STARS; k++) {
    star_t *s = &f.stars[k];
    int32_t z = (int32_t)s->z - (int32_t)d;
    bool fresh = z <= 512 * 256;
    for (;;) {
      if (fresh) {
        s->y = (int16_t)rnd();
        s->x = (int16_t)rnd();
        z = 8191 * 256;
      }
      int x = (int16_t)(((int32_t)s->x * 16) / (z >> 8)) + 160;
      int y = (int16_t)(((int32_t)s->y * 16) / (z >> 8)) + 100;
      if ((uint16_t)x < 320u && (uint16_t)y < 200u) break;
      fresh = true;
    }
    s->z = (uint32_t)z;
  }
}

/* Drawn where the screen shows nothing, coloured by depth. */
static void stars_draw(void) {
  for (int k = 0; k < STARS; k++) {
    const star_t *s = &f.stars[k];
    int32_t z = (int32_t)(s->z >> 8);
    int x = (int16_t)(((int32_t)s->x * 16) / z) + 160;
    int y = (int16_t)(((int32_t)s->y * 16) / z) + 100;
    uint8_t *px = &fb_chunked_buffer[y * FB_CHUNKED_W + x];
    if (!*px) *px = byte_at(STAR_COLOURS + (((uint32_t)z >> 10) & 7u)) & 15u;
  }
}

/* The frame recorded: shown in steps until its VBL ($28D4). */
static bool frame(void) {
  bool first = !f.scr.waiting;
  bool done = screen_frame(&f.scr);
  if (first) {
    if (f.moving) stars_move(STAR_STEP - f.moved);
    f.recording = false;
    f.from = f.scr.ticks;
    f.steps = f.scr.due > f.scr.ticks ? f.scr.due - f.scr.ticks : 0;
    f.moved = 0;
    f.moving = true;
  }
  return done;
}

/* ---- the palette ---- */

static void attract_palette(void) {
  uint16_t p[16];
  for (int i = 0; i < 16; i++) p[i] = (uint16_t)word_at(PALETTE + 2u * (uint32_t)i);
  screen_palette(&f.scr, p);
}

/* The palette saved and blacked, the stars made again ($84DC). */
static void prepare(void) {
  memcpy(f.saved, f.scr.palette, sizeof(f.saved));
  static const uint16_t k_black[16];
  screen_palette(&f.scr, k_black);
  stars_seed();
}

static void restore(void) { screen_palette(&f.scr, f.saved); } /* $8518 */

/* ---- the text script ($8530) ---- */

static bool script(const uint8_t *text, int16_t centre, uint32_t table, bool wait) {
  pt_t *pt = &f.pt_script;
  PT_BEGIN(pt);
  f.script_text = text;
  f.script_centre = centre;
  f.script_table = table;
  f.script_wait = wait;
  f.script_index = 0;
  for (;;) {
    clear();
    {
      int16_t e = (int16_t)word_at(f.script_table + 2u * (uint32_t)f.script_index);
      if (!(f.script_wait && e == 17 && !f.fire)) {
        if (f.script_wait && e == 17) f.fire = false;
        f.script_index++;
      }
      if (e < 0) return true;
      f.scale = e;
      if (e) text_line(f.script_text, e, f.script_centre, true);
    }
    stars();
    FRAME(pt);
    attract_palette();
  }
  PT_END(pt);
}

/* ---- the attract ($82D2) ---- */

/* Stars alone for 34 frames, then the logo zooming in ($8242). */
static bool logo_in(void) {
  pt_t *pt = &f.pt_logo;
  PT_BEGIN(pt);
  f.scale = 1;
  for (f.n = 34; f.n; f.n--) {
    clear();
    stars();
    FRAME(pt);
    if (f.fire) break;
  }
  while (!f.fire) {
    clear();
    logo_zoom(f.scale);
    stars();
    FRAME(pt);
    if (++f.scale >= 16) break;
  }
  PT_END(pt);
}

/* The logo zooming out ($829C). */
static bool logo_out(void) {
  pt_t *pt = &f.pt_logo;
  PT_BEGIN(pt);
  for (f.scale = 16; f.scale && !f.fire; f.scale--) {
    clear();
    logo_zoom(f.scale);
    stars();
    FRAME(pt);
  }
  PT_END(pt);
}

/* The table: its title, its ten lines ($8B40). */
static bool table_frame(void) {
  pt_t *pt = &f.pt_table;
  PT_BEGIN(pt);
  clear();
  text_static(text_at(HIGH_SCORE), 12);
  for (int r = 0; r < FLOW_SCORES; r++) {
    small_text((const uint8_t *)f.scores->text[r], FLOW_SCORE_TEXT, 32, 38 + 16 * r);
  }
  stars();
  FRAME(pt);
  PT_END(pt);
}

static bool attract(void) {
  pt_t *pt = &f.pt_main;
  PT_BEGIN(pt);
  memcpy(f.saved, f.scr.palette, sizeof(f.saved));
  f.hooks.song(f.hooks.ctx, 15);
  f.credit = CREDITS;
  f.index = 0;
  f.line = 120;
  f.phase = 0;
  stars_seed();
  attract_palette();
  PT_CALL(pt, &f.pt_logo, logo_in());
  if (f.fire) goto other;
  for (;;) {
    logo();
    for (;;) { /* $8348: the next step, maybe of the next phase or pair */
      f.line = 120;
      if (f.phase == 1) {
        text_static(text_at(f.credit), 120);
        f.line = 150;
      }
      int16_t e = (int16_t)word_at(ZOOM_CREDITS + 2u * (uint32_t)f.index++);
      if (e > 0) {
        text_zoom(text_at(f.credit + (f.phase == 1 ? 20u : 0u)), e, f.line);
      } else if (e < 0) {
        if (++f.phase != 3) continue;
        f.phase = 0;
        f.index = 0;
        f.credit += 40;
        if (f.credit != CREDITS_END) continue;
        goto credits_end;
      }
      break;
    }
    stars();
    if (f.fire) goto credits_fire;
    FRAME(pt);
    continue;
  credits_end:
    PT_CALL(pt, &f.pt_logo, logo_out());
    if (f.fire) goto other;
    PT_CALL(pt, &f.pt_script, script(text_at(HIGH_SCORE), 12, ZOOM_IN, false));
    for (f.n = 85; f.n && !f.fire; f.n--) PT_CALL(pt, &f.pt_table, table_frame());
    if (f.fire) goto other;
    PT_CALL(pt, &f.pt_script, script(text_at(HIGH_SCORE), 12, ZOOM_OUT, false));
    if (f.fire) goto other;
    PT_CALL(pt, &f.pt_logo, logo_in());
    if (f.fire) goto other;
    f.credit = CREDITS;
    f.index = 0;
  }
credits_fire: /* $86D0 */
  f.fire = false;
  f.hooks.fade(f.hooks.ctx);
  PT_CALL(pt, &f.pt_logo, logo_out());
  goto done;
other: /* $86E0 */
  f.hooks.stop(f.hooks.ctx);
done:
  f.fire = false;
  restore();
  PT_END(pt);
}

/* ---- SELECT ($8CAA) ---- */

static bool select_menu(void) {
  pt_t *pt = &f.pt_main;
  PT_BEGIN(pt);
  prepare();
  PT_CALL(pt, &f.pt_script, script(text_at(SELECT_TEXT), 12, ZOOM_IN, false));
  f.fire = false;
  f.sel = 0;
  for (;;) {
    clear();
    text_static(text_at(SELECT_TEXT), 12);
    text_static(text_at(ONE_PLAYER + (f.sel == 0 ? 20u : 0u)), 60);
    text_static(text_at(TWO_PLAYERS + (f.sel == 1 ? 20u : 0u)), 84);
    text_static(text_at((*f.music ? MUSIC_ON : MUSIC_OFF) + (f.sel == 2 ? 20u : 0u)), 108);
    credits_left();
    stars();
    FRAME(pt);
    if (f.a00 & 1u) {
      f.a00 = 0;
      f.hooks.effect(f.hooks.ctx, 11);
      if (--f.sel < 0) f.sel = 2;
      continue;
    }
    if (f.a00 & 2u) {
      f.a00 = 0;
      f.hooks.effect(f.hooks.ctx, 11);
      if (++f.sel >= 3) f.sel = 0;
      continue;
    }
    if (!f.fire) continue;
    f.fire = false;
    f.hooks.effect(f.hooks.ctx, 13);
    if (f.sel == 2) {
      *f.music = !*f.music;
      continue;
    }
    break;
  }
  f.result = f.sel + 1;
  PT_CALL(pt, &f.pt_script, script(text_at(SELECT_TEXT), 12, ZOOM_OUT, false));
  restore();
  PT_END(pt);
}

/* ---- the messages ($84C4) ---- */

static bool message(void) {
  pt_t *pt = &f.pt_main;
  PT_BEGIN(pt);
  prepare();
  PT_CALL(pt, &f.pt_script, script(f.text, 100, ZOOM_MESSAGE, f.script_wait));
  restore();
  PT_END(pt);
}

/* ---- CONTINUE ($8E7C) ---- */

static bool continue_menu(void) {
  pt_t *pt = &f.pt_main;
  PT_BEGIN(pt);
  prepare();
  PT_CALL(pt, &f.pt_script, script(text_at(CONTINUE_TEXT), 80, ZOOM_IN, false));
  f.fire = false;
  f.result = 0;
  for (f.n = 79; f.n; f.n--) {
    clear();
    text_static(text_at(CONTINUE_TEXT), 80);
    memcpy(f.text, text_at(COUNTDOWN), 20);
    f.text[9] = (uint8_t)('0' + f.n / 8);
    text_static(f.text, 120);
    credits_left();
    stars();
    FRAME(pt);
    if (f.fire) {
      f.result = 1;
      break;
    }
  }
  PT_CALL(pt, &f.pt_script, script(text_at(CONTINUE_TEXT), 80, ZOOM_OUT, false));
  restore();
  f.fire = false;
  PT_END(pt);
}

/* ---- the high scores ($8A36) ---- */

/* The score into the table, the lines below moved down, its initials
 * empty ($8A48). */
static void insert(uint32_t score) {
  flow_scores_t *t = f.scores;
  int k = FLOW_SCORES - 1;
  while (k > 0 && (int32_t)score > (int32_t)t->score[k - 1]) {
    t->score[k] = t->score[k - 1];
    memcpy(&t->text[k][4], &t->text[k - 1][4], 11);
    k--;
  }
  t->score[k] = score;
  t->text[k][12] = t->text[k][13] = t->text[k][14] = NO_NAME;
  int32_t v = (int32_t)score;
  for (int i = 0;; i++) {
    int32_t p = (int32_t)long_at(SCORE_POWERS + 4u * (uint32_t)i);
    if (!p) break;
    char d = '0';
    while ((v -= p) >= 0) d++;
    v += p;
    t->text[k][4 + i] = d;
  }
  f.slot = k;
}

static bool high_score(void) {
  pt_t *pt = &f.pt_main;
  PT_BEGIN(pt);
  prepare();
  PT_CALL(pt, &f.pt_script, script(text_at(HIGH_SCORE), 12, ZOOM_IN, false));
  insert(f.score);
  f.fire = false;
  f.letter = 0;
  for (f.n = 12; f.n < 15; f.n++) { /* each initial */
    for (;;) {
      f.scores->text[f.slot][f.n] = (char)byte_at(LETTERS + (uint32_t)f.letter);
      PT_CALL(pt, &f.pt_table, table_frame());
      if (f.fire) break;
      f.held = f.a00 & 12u;
      if (!f.held) continue;
      f.step = (f.held & 4u) ? -1 : 1;
      for (;;) { /* a step now, then one every 4 frames while held */
        f.letter += f.step;
        if (f.letter < 0) f.letter = 37;
        if (f.letter >= 38) f.letter = 0;
        for (f.repeat = 0; f.repeat < 4; f.repeat++) {
          f.scores->text[f.slot][f.n] = (char)byte_at(LETTERS + (uint32_t)f.letter);
          PT_CALL(pt, &f.pt_table, table_frame());
          if (f.fire || (f.a00 & 12u) != f.held) break;
        }
        if (f.repeat < 4) break;
      }
      if (f.fire) break;
    }
    f.fire = false;
  }
  for (f.n = 17; f.n; f.n--) PT_CALL(pt, &f.pt_table, table_frame());
  PT_CALL(pt, &f.pt_script, script(text_at(HIGH_SCORE), 12, ZOOM_OUT, false));
  restore();
  PT_END(pt);
}

/* ---- the game's side ---- */

void flow_scores_clear(flow_scores_t *scores) {
  memset(scores->score, 0, sizeof(scores->score));
  for (int r = 0; r < FLOW_SCORES; r++) {
    memcpy(scores->text[r], " 1. 0000000 :::", FLOW_SCORE_TEXT);
    scores->text[r][0] = r == 9 ? '1' : ' ';
    scores->text[r][1] = (char)('0' + (r + 1) % 10);
  }
}

void flow_init(const x2screens_header_t *image, const x2global_header_t *global,
               uint32_t seed, flow_scores_t *scores, const flow_hooks_t *hooks) {
  memset(&f, 0, sizeof(f));
  screen_start(&f.scr, image, global, NULL, NULL);
  f.scr.frame_cycles = FRAME_CYCLES;
  rng_seed(f.rng, seed);
  f.scores = scores;
  f.hooks = *hooks;
  bool empty = true;
  for (int r = 0; r < FLOW_SCORES; r++) empty = empty && !scores->text[r][0];
  if (empty) flow_scores_clear(scores);
}

static void begin(kind_t kind) {
  uint16_t shown[16];
  memcpy(shown, f.scr.palette, sizeof(shown));
  screen_start(&f.scr, f.scr.image, f.scr.global, NULL, shown);
  f.scr.frame_cycles = FRAME_CYCLES;
  f.kind = kind;
  f.result = 0;
  f.fire = false;
  f.pt_main.lc = 0;
  f.scene.n = f.shown.n = 0;
  f.recording = f.moving = false;
}

void flow_attract(void) { begin(K_ATTRACT); }

void flow_select(int credits, bool *music) {
  begin(K_SELECT);
  f.credits = credits;
  f.music = music;
}

void flow_get_ready(int player) {
  begin(K_MESSAGE);
  memcpy(f.text, text_at(GET_READY), 20);
  f.text[PLAYER_DIGIT] = (uint8_t)('1' + player);
  f.script_wait = true;
}

void flow_game_over(void) {
  begin(K_MESSAGE);
  memcpy(f.text, text_at(GAME_OVER), 20);
  f.script_wait = false;
}

void flow_continue(int credits) {
  begin(K_CONTINUE);
  f.credits = credits;
}

bool flow_high_score(uint32_t score) {
  if ((int32_t)score <= (int32_t)f.scores->score[FLOW_SCORES - 1]) return false;
  begin(K_SCORE);
  f.score = score;
  return true;
}

bool flow_vbl(uint8_t stick, bool fire) {
  screen_tick(&f.scr);
  if (stick != f.last_stick) f.a00 = stick; /* a report on every change */
  f.last_stick = stick;
  if (fire) f.fire = true;
  bool done = true;
  switch (f.kind) {
    case K_ATTRACT: done = attract(); break;
    case K_SELECT: done = select_menu(); break;
    case K_MESSAGE: done = message(); break;
    case K_CONTINUE: done = continue_menu(); break;
    case K_SCORE: done = high_score(); break;
  }
  if (f.moving) { /* the frame's depth step, spread over its VBLs */
    uint32_t j = f.scr.ticks - f.from;
    uint32_t to = j >= f.steps ? STAR_STEP : STAR_STEP * j / f.steps;
    stars_move(to - f.moved);
    f.moved = to;
    f.moving = to < STAR_STEP;
  }
  return !done;
}

void flow_draw(void) {
  uint32_t j = f.scr.ticks - f.from;
  draw_scene(j < f.steps ? j : f.steps, f.steps);
  stars_draw();
}

int flow_result(void) { return f.result; }
