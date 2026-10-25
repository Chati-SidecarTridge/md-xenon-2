/**
 * File: screen.h
 * Description: What the screens outside play share (the shop, the attract,
 *              the menus and messages): the original's bytes read by
 *              address from the store, its planar pictures written into the
 *              chunky framebuffer, its pace on a model of the ST's clock,
 *              and the coroutines its routines that wait for frames
 *              become. Plain C, no SDK.
 *
 * The clock: what a screen draws costs the 68000 cycles the original's
 * drawing takes (screen_spend; 30% more while the shop's sample timer
 * interrupts), and a frame follows the original's rule ($28D4): at least
 * 3 VBLs after the last, then the swap once the beam has drawn the picture.
 * The fades and VBL waits count on the same clock; the port's VBLs
 * (screen_tick) follow it.
 *
 * A coroutine keeps its place in its own pt_t and returns false while it
 * waits, true once it is done; a caller runs a callee to its end with
 * PT_CALL, which calls it again on every resume, so a callee reads its
 * arguments before its first wait. Their variables live in the screen's
 * state, never on the stack.
 */

#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "x2global.h"
#include "x2level.h"
#include "x2screens.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t lc;
} pt_t;

#define PT_BEGIN(pt) switch ((pt)->lc) { case 0:
#define PT_END(pt) } (pt)->lc = 0; return true
#define PT_WAIT(pt, cond)    \
  do {                       \
    (pt)->lc = __LINE__;     \
    /* FALLTHROUGH */        \
    case __LINE__:           \
      if (!(cond)) return false; \
  } while (0)
#define PT_CALL(pt, child, call) \
  do {                           \
    (child)->lc = 0;             \
    PT_WAIT(pt, call);           \
  } while (0)

#define SCREEN_VBL_CYCLES 160256u /* a PAL VBL of the 68000 */

typedef struct {
  const x2screens_header_t *image;
  const x2global_header_t *global;
  const x2level_header_t *level; /* NULL outside a level */
  int16_t clip;           /* $420: the last line sprites reach */
  uint32_t frame_cycles;  /* what each frame costs besides its drawing */
  bool timer;             /* the shop's sample timer interrupting */
  uint16_t palette[16];   /* the palette registers */
  /* the clock */
  uint64_t now;           /* cycles since the screen began */
  uint32_t spent;         /* drawn since the clock last moved */
  uint32_t due;           /* the VBL the waited-for moment falls in */
  bool waiting;
  uint16_t vbl, mark;     /* $41C, $404 */
  uint32_t ticks;         /* the port's VBLs since the screen began */
  /* the fades */
  pt_t pt_fade;
  int fade_n;
  int16_t fade_d;
  const uint16_t *fade_to;
} screen_t;

/* A screen begins: the clock at 0, the palette registers as `palette`. */
void screen_start(screen_t *s, const x2screens_header_t *image,
                  const x2global_header_t *global,
                  const x2level_header_t *level, const uint16_t *palette);

/* A VBL of the ST's passed. */
void screen_tick(screen_t *s);

/* `len` bytes of the original's memory at `addr` (the level from $4F000,
 * the sound driver's bytes, the screens' windows), NULL if none holds them;
 * big-endian words and longs, 0 where nothing is. */
const uint8_t *screen_at(const screen_t *s, uint32_t addr, uint32_t len);
uint32_t screen_word(const screen_t *s, uint32_t addr);
uint32_t screen_long(const screen_t *s, uint32_t addr);
uint8_t screen_byte(const screen_t *s, uint32_t addr);

/* Drawing done on the ST's clock. */
void screen_spend(screen_t *s, uint32_t cycles);

/* 16 pixels of the four planes at (x, y), clipped to the screen: where
 * `keep` has a bit the screen's pixel stays, ORed with the planes. */
void screen_group(int x, int y, uint32_t p0, uint32_t p1, uint32_t p2,
                  uint32_t p3, uint32_t keep);

/* Waits on the clock, each a coroutine's condition (PT_WAIT): the frame
 * ($28D4), the next VBL, `n` VBLs counted from 0 ($41C cleared). */
bool screen_frame(screen_t *s);
bool screen_vbl(screen_t *s);
bool screen_vbls_counted(screen_t *s, uint32_t n);

/* The palette registers set, and the fades: to black, d + 1 VBLs a step
 * ($88A6); in to a palette, 2 VBLs a step ($88EA). Coroutines on
 * s->pt_fade. */
void screen_palette(screen_t *s, const uint16_t *p);
bool screen_fade_out(screen_t *s, int16_t d);
bool screen_fade_in(screen_t *s, const uint16_t *to);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_H */
