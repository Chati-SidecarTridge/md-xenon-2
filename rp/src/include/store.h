/**
 * File: store.h
 * Description: The asset store: game data converted for per-frame use, kept
 *              in flash between the app's code and the Booster's area
 *              (ASSET_STORE in memmap_rp.ld) and read in place. The
 *              sections: the level being played (from its start, rewritten
 *              from the card's set when another level starts), the screens
 *              outside play (STORE_SCREENS_OFFSET: the shop, the attract,
 *              the menus), the title tune (STORE_TUNE_OFFSET) and
 *              the graphics every level shares with the sound driver
 *              (STORE_GLOBAL_OFFSET), the last three written once. All
 *              survive a power cycle.
 */

#ifndef STORE_H
#define STORE_H

#include <stdint.h>

#include "x2io.h"
#include "x2global.h"
#include "x2level.h"
#include "x2screens.h"
#include "x2tune.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the sections start in the store: the level from 0 (400 KB), the
 * screens outside play at 400 KB (112 KB), the title tune at 512 KB
 * (128 KB), the shared graphics and the sound driver at 640 KB (128 KB). */
#define STORE_SCREENS_OFFSET (400u * 1024u)
#define STORE_TUNE_OFFSET (512u * 1024u)
#define STORE_GLOBAL_OFFSET (640u * 1024u)

/* The converted level in the store, if it is level `level` made from the
 * unpacked level of CRC `source_crc` by this version of the converter;
 * NULL otherwise. */
const x2level_header_t *store_level(uint32_t level, uint32_t source_crc);

/* Convert the unpacked level `src` (`size` bytes) into the store: erase,
 * then program as x2level_convert writes, the magic last. Core 0 only,
 * with core 1 parked; interrupts are off during each erase and program.
 * `progress` (optional) is called between flash operations with the bytes
 * done and the total, so the caller can keep the screen and the bus
 * going. X2_OK or an error. */
int store_write_level(const x2_source_t *src, uint32_t size, uint32_t level,
                      uint32_t source_crc, x2_progress_fn progress,
                      void *progress_ctx);

/* The screens outside play in the store, if made from the game image of
 * CRC `source_crc` by this version of the converter; NULL otherwise. */
const x2screens_header_t *store_screens(uint32_t source_crc);

/* Convert the game image `game` into the store's screens section. */
int store_write_screens(const x2_source_t *game, uint32_t source_crc,
                        x2_progress_fn progress, void *progress_ctx);

/* The title tune in the store, if made from the boot program of CRC
 * `source_crc` by this version of the converter; NULL otherwise. */
const x2tune_header_t *store_tune(uint32_t source_crc);

/* Convert the boot program `boot` into the store's tune section. */
int store_write_tune(const x2_source_t *boot, uint32_t source_crc,
                     x2_progress_fn progress, void *progress_ctx);

/* The shared graphics in the store, if made from the game image of CRC
 * `source_crc` by this version of the converter; NULL otherwise. */
const x2global_header_t *store_global(uint32_t source_crc);

/* Convert the game image `game` into the store's shared section, as
 * store_write_level does for a level. */
int store_write_global(const x2_source_t *game, uint32_t source_crc,
                       x2_progress_fn progress, void *progress_ctx);

#ifdef __cplusplus
}
#endif

#endif /* STORE_H */
