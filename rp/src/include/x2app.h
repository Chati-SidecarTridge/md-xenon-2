/**
 * File: x2app.h
 * Description: The app's screens and its per-frame step: the start (the
 *              converted set checked or made, with its messages), the title
 *              and level 1 (play.h). Esc leaves to GEM, but in play, where
 *              it returns to the title as in the original.
 */

#ifndef X2APP_H
#define X2APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* At every ST start: check or make the set in `folder`, then show the
 * title, or a message saying what is missing. */
void x2app_start(const char *folder, bool card_mounted);

/* Once per main-loop pass: input, then the frame (drawn, not published). */
void x2app_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* X2APP_H */
