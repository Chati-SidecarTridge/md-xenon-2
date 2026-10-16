/**
 * File: store.h
 * Description: The asset store: game data converted for per-frame use, kept
 *              in flash between the app's code and the Booster's area
 *              (ASSET_STORE in memmap_rp.ld) and read in place. Two
 *              sections: the level being played (from its start, rewritten
 *              from the card's set when another level starts) and the
 *              graphics every level shares (STORE_GLOBAL_OFFSET, written
 *              once). Both survive a power cycle.
 */

#ifndef STORE_H
#define STORE_H

#include <stdint.h>

#include "x2io.h"
#include "x2global.h"
#include "x2level.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the shared graphics start in the store: the level section has the
 * 640 KB before them, the shared ones the 128 KB after. */
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
