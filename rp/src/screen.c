/**
 * File: screen.c
 * Description: What the screens outside play share (screen.h).
 */

#pragma GCC optimize("O3")

#include "screen.h"

#include <string.h>

#include "fb_chunked.h"
#include "palette.h"

#define SWAP_AT 134640u      /* the beam at the picture's end ($28D4's wait) */
#define VBLS_A_FRAME 3u      /* $410 */
#define TIMER_PERCENT 130u   /* while the shop's sample timer interrupts */
#define LEVEL_BASE 0x4F000u

void screen_start(screen_t *s, const x2screens_header_t *image,
                  const x2global_header_t *global,
                  const x2level_header_t *level, const uint16_t *palette) {
  memset(s, 0, sizeof(*s));
  s->image = image;
  s->global = global;
  s->level = level;
  s->clip = 191;
  for (int i = 0; i < 16; i++) s->palette[i] = palette ? palette[i] : 0;
}

void screen_tick(screen_t *s) { s->ticks++; }

/* ---- the original's memory, read only ---- */

const uint8_t *screen_at(const screen_t *s, uint32_t addr, uint32_t len) {
  if (addr >= LEVEL_BASE) return s->level ? x2level_raw(s->level, addr, len) : NULL;
  if (addr - X2G_DRIVER_START < X2G_DRIVER_SIZE) {
    if (addr + len - X2G_DRIVER_START > X2G_DRIVER_SIZE) return NULL;
    return (const uint8_t *)s->global + s->global->driver_offset + addr -
           X2G_DRIVER_START;
  }
  return x2screens_raw(s->image, addr, len);
}

uint32_t screen_word(const screen_t *s, uint32_t addr) {
  const uint8_t *p = screen_at(s, addr, 2);
  return p ? (uint32_t)p[0] << 8 | p[1] : 0;
}

uint32_t screen_long(const screen_t *s, uint32_t addr) {
  return screen_word(s, addr) << 16 | screen_word(s, addr + 2);
}

uint8_t screen_byte(const screen_t *s, uint32_t addr) {
  const uint8_t *p = screen_at(s, addr, 1);
  return p ? *p : 0;
}

/* ---- drawing ---- */

void screen_spend(screen_t *s, uint32_t cycles) {
  s->spent += s->timer ? cycles * TIMER_PERCENT / 100u : cycles;
}

void screen_group(int x, int y, uint32_t p0, uint32_t p1, uint32_t p2,
                  uint32_t p3, uint32_t keep) {
  if (y < 0 || y >= FB_CHUNKED_H) return;
  uint8_t *row = &fb_chunked_buffer[y * FB_CHUNKED_W];
  for (int i = 0; i < 16; i++) {
    int sx = x + i;
    if (sx < 0 || sx >= FB_CHUNKED_W) continue;
    uint32_t bit = 0x8000u >> i;
    uint8_t c = (uint8_t)(((p0 & bit) ? 1 : 0) | ((p1 & bit) ? 2 : 0) |
                          ((p2 & bit) ? 4 : 0) | ((p3 & bit) ? 8 : 0));
    row[sx] = (uint8_t)(((keep & bit) ? row[sx] : 0) | c);
  }
}

/* ---- the clock ---- */

/* The clock moved to cycle t, its VBL counter with it. */
static void to(screen_t *s, uint64_t t) {
  s->vbl = (uint16_t)(s->vbl + (t / SCREEN_VBL_CYCLES - s->now / SCREEN_VBL_CYCLES));
  s->now = t;
}

/* The drawing done, then the clock to the start of VBL `n` from now. */
static void vbls(screen_t *s, uint32_t n) {
  to(s, s->now + s->spent);
  s->spent = 0;
  to(s, (s->now / SCREEN_VBL_CYCLES + n) * SCREEN_VBL_CYCLES);
}

/* The port's VBLs caught up with the ST's moment. */
static bool reached(screen_t *s) {
  if (s->ticks < s->due) return false;
  s->waiting = false;
  return true;
}

bool screen_frame(screen_t *s) {
  if (!s->waiting) {
    to(s, s->now + s->spent + s->frame_cycles);
    s->spent = 0;
    uint16_t since = (uint16_t)(s->vbl - s->mark);
    if (since < VBLS_A_FRAME) vbls(s, VBLS_A_FRAME - since);
    s->mark = s->vbl;
    uint32_t phase = (uint32_t)(s->now % SCREEN_VBL_CYCLES);
    if (phase < SWAP_AT) to(s, s->now - phase + SWAP_AT);
    s->due = (uint32_t)(s->now / SCREEN_VBL_CYCLES);
    s->waiting = true;
  }
  return reached(s);
}

bool screen_vbl(screen_t *s) {
  if (!s->waiting) {
    vbls(s, 1);
    s->due = (uint32_t)(s->now / SCREEN_VBL_CYCLES);
    s->waiting = true;
  }
  return reached(s);
}

bool screen_vbls_counted(screen_t *s, uint32_t n) {
  if (!s->waiting) {
    s->vbl = 0;
    vbls(s, n);
    s->due = (uint32_t)(s->now / SCREEN_VBL_CYCLES);
    s->waiting = true;
  }
  return reached(s);
}

/* ---- the palette ---- */

static void set(screen_t *s, int i, uint16_t c) {
  s->palette[i] = c;
  palette_set_entry((uint8_t)i, c);
}

void screen_palette(screen_t *s, const uint16_t *p) {
  for (int i = 0; i < 16; i++) set(s, i, p[i]);
}

bool screen_fade_out(screen_t *s, int16_t d) {
  pt_t *pt = &s->pt_fade;
  PT_BEGIN(pt);
  for (s->fade_n = 0; s->fade_n < 8; s->fade_n++) {
    PT_WAIT(pt, screen_vbls_counted(s, (uint32_t)d + 1u));
    for (int i = 0; i < 16; i++) {
      uint16_t c = (uint16_t)((s->palette[i] & 0x777u) - 0x111u);
      if (c & 0x8u) c = (uint16_t)(c + 1u);
      if (c & 0x80u) c = (uint16_t)(c + 0x10u);
      if (c & 0x800u) c = (uint16_t)(c + 0x100u);
      set(s, i, c);
    }
  }
  PT_END(pt);
}

bool screen_fade_in(screen_t *s, const uint16_t *to_palette) {
  pt_t *pt = &s->pt_fade;
  PT_BEGIN(pt);
  s->fade_to = to_palette;
  for (s->fade_d = 0x666; s->fade_d >= 0; s->fade_d = (int16_t)(s->fade_d - 0x111)) {
    PT_WAIT(pt, screen_vbls_counted(s, 2));
    for (int i = 0; i < 16; i++) {
      uint16_t c = (uint16_t)((s->fade_to[i] & 0x777u) - (uint16_t)s->fade_d);
      while (c & 0x8u) c = (uint16_t)(c + 1u);
      while (c & 0x80u) c = (uint16_t)(c + 0x10u);
      while (c & 0x800u) c = (uint16_t)(c + 0x100u);
      set(s, i, (uint16_t)(c & 0x777u));
    }
  }
  PT_END(pt);
}
