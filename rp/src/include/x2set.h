/**
 * File: x2set.h
 * Description: The converted set on the SD card. The user copies the two
 *              disk images of the original (the Image Works release, Pasti
 *              .stx) into the app's folder; at start the set in its SET
 *              subfolder is checked and, when it is missing or made by
 *              another version of the converter, made again from the disks
 *              (x2game.h), each file checked against the known release.
 */

#ifndef X2SET_H
#define X2SET_H

#include <stdbool.h>
#include <stdint.h>

#include "x2game.h"
#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever the set's files change: a set of another version is made
 * again. */
#define X2SET_VERSION 1

typedef enum {
  X2SET_READY = 0,     /* the set is there, complete */
  X2SET_NO_CARD,       /* no card, or no folder */
  X2SET_NO_DISKS,      /* neither disk image in the folder */
  X2SET_NO_DISK1,      /* disk 2 found, not disk 1 */
  X2SET_NO_DISK2,      /* disk 1 found, not disk 2 */
  X2SET_WRONG_DUMP,    /* the disks convert, but not to the known release */
  X2SET_KIXX,          /* the Kixx re-release's single disk: not supported */
  X2SET_ERROR          /* the card failed while reading or writing */
} x2set_status_t;

/* What a conversion step reports to the screen: the file being made and
 * the whole conversion's progress, in bytes. */
typedef struct {
  const char *file;
  uint32_t done;
  uint32_t total;
} x2set_progress_t;

typedef void (*x2set_ui_fn)(const x2set_progress_t *p, void *ctx);

/* Check the set in `folder`/SET and, if needed, make it from the disk
 * images in `folder`. `ui` is called often during a conversion (it keeps
 * the main loop's plumbing going and draws the progress). */
x2set_status_t x2set_prepare(const char *folder, bool card_mounted,
                             x2set_ui_fn ui, void *ui_ctx);

/* Tests only: check the converted files against `table` (X2_OUTPUTS
 * entries) instead of the known release (x2_known); NULL restores it. */
void x2set_set_expected(const x2_known_t *table);

/* Close every file the converter has open on the card, so a restart (the
 * SELECT button) never cuts a write short: a card restarted mid-write can
 * stop answering until it loses power. A set left without its manifest is
 * made again at the next start. */
void x2set_stop_card_work(void);

/* Read `len` bytes at `offset` of a set file (e.g. "TITLE.X2"). */
int x2set_read(const char *folder, const char *name, uint32_t offset,
               void *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* X2SET_H */
