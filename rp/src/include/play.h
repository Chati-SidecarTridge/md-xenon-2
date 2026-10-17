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

/* Start level `level` (1): convert it into the store from the set in
 * `folder` if the store holds another one (`progress` keeps the screen
 * going meanwhile), then set the palette and draw its first frame. False
 * if the level could not be made ready. */
bool play_start(const char *folder, uint32_t level, x2_progress_fn progress,
                void *progress_ctx);

/* One main-loop pass: step the game by the ST's VBLs since the last pass
 * and draw it (not published). Returns false when the player leaves (Esc,
 * as in the original) or the game is over. */
bool play_frame(void);

/* A new ST session: the register ring empty, the chip the template's. */
void play_sound_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PLAY_H */
