/**
 * File: tune.h
 * Description: The title tune (x2tune.h) played from the store through the
 *              template's sampled sound: the twelve sections in the boot
 *              program's order, looping, at 7,979 Hz (the DMA chip at its
 *              rate on an STE; on a plain ST through the YM, each sample as
 *              the two volumes as loud as the original's three).
 */

#ifndef TUNE_H
#define TUNE_H

#include <stdbool.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Make the tune ready in the store from the set in `folder` if it is not
 * (`progress` keeps the screen going), then play it from its start. False
 * if it could not be made ready. */
bool tune_start(const char *folder, x2_progress_fn progress,
                void *progress_ctx);

/* Stop it. */
void tune_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* TUNE_H */
