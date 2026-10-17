/**
 * File: play.h
 * Description: A level in play: the store made ready for it, the world
 *              drawn every VBL and moved at the original's pace. For now the
 *              world alone (level 1); debug builds add a fly-through.
 */

#ifndef PLAY_H
#define PLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PLAY_ON,   /* the level goes on */
  PLAY_OVER, /* the player left (Esc, as in the original) or the game ended */
  PLAY_WON   /* the boss destroyed and its payout over: the next level */
} play_state_t;

/* A new game at level `level` (1): convert it into the store from the set
 * in `folder` if the store holds another one (`progress` keeps the screen
 * going meanwhile), then set the palette and draw its first frame. False
 * if the level could not be made ready. */
bool play_start(const char *folder, uint32_t level, x2_progress_fn progress,
                void *progress_ctx);

/* The next level after PLAY_WON, the ship's lives, shield, score and
 * weapons kept (level 5 is followed by level 1 again, the game going round
 * once more). False if the level could not be made ready. */
bool play_next(const char *folder, x2_progress_fn progress,
               void *progress_ctx);

/* The level being played. */
uint32_t play_level(void);

/* One main-loop pass: step the game by the ST's VBLs since the last pass
 * and draw it (not published). */
play_state_t play_frame(void);

/* A new ST session: the register ring empty, the chip the template's. */
void play_sound_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PLAY_H */
