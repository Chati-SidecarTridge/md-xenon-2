/**
 * File: scroll.h
 * Description: How the world moves: the original's scroll, stepped once per
 *              original frame, and the pacing that gives the original's
 *              frames per second at 50 Hz. Plain C, no SDK.
 *
 * The original redraws at 16.7 fps (one frame every 3 VBLs) and moves the
 * world in whole lines, so each of its frames is a position the port can
 * show exactly: the port steps the original's rule once per original frame
 * (scroll_tick) and draws every VBL. The world thus moves at the original's
 * lines per second and every frame shown is one the original showed.
 */

#ifndef SCROLL_H
#define SCROLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCROLL_START_LINE 4608  /* 288 rows of 16 lines: every level */
#define SCROLL_WINDOW 16        /* how far back the world may scroll */
#define SCROLL_DOUBLE_AFTER 35  /* frames off the set speed before doubling */
#define SCROLL_SHIP_TOP 16      /* the ship's highest and lowest y */
#define SCROLL_SHIP_BOTTOM 176
#define SCROLL_STICK_UP 0x01    /* as the original's stick byte */
#define SCROLL_STICK_DOWN 0x02

/* The original's variables, named. */
typedef struct {
  int32_t line;       /* the world line at the screen's top ($CD0) */
  int32_t speed;      /* the set speed, lines a frame ($CCC) */
  int32_t step;       /* this frame's step ($CDA) */
  int32_t last_step;  /* the last step taken ($CD8) */
  int32_t off_speed;  /* frames the step has differed from the speed ($CE0) */
  int32_t lowest;     /* the line the scroll stops at ($CE8) */
  int32_t back_limit; /* the highest line it may go back to ($CEA) */
  int32_t window;     /* $CEE */
  uint32_t frame;     /* the level's frame counter ($CDC) */
  uint32_t backdrop;  /* the backdrop line at the screen's top, 0-191 */
} scroll_t;

/* Level start, up to the first frame's drawing: the world at its start,
 * the backdrop's first step taken. */
void scroll_start(scroll_t *s);

/* One original frame: the step from the stick (`stick`, SCROLL_STICK_*)
 * and the ship's y before it moves, the move, then the next frame's
 * backdrop step. Draw after it. */
void scroll_tick(scroll_t *s, uint8_t stick, int32_t ship_y);

/* The state the original reaches at world line `line` going forward at a
 * line a frame from the level's start (for looking at any part of the
 * level, and for restart points). */
void scroll_seek(scroll_t *s, int32_t line);

/* 50 Hz pacing: `vbls_per_frame_q8` VBLs per original frame, 8.8 fixed
 * point (3.0 = 768: the original's 16.7 fps). Each VBL, scroll_pace says
 * how many original frames to step. */
typedef struct {
  uint32_t phase_q8;
  uint32_t vbls_per_frame_q8;
} scroll_pace_t;

#define SCROLL_VBLS_PER_FRAME_Q8 768u

void scroll_pace_init(scroll_pace_t *p, uint32_t vbls_per_frame_q8);
uint32_t scroll_pace(scroll_pace_t *p);

#ifdef __cplusplus
}
#endif

#endif /* SCROLL_H */
