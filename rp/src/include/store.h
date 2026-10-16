/**
 * File: store.h
 * Description: The asset store: game data converted for per-frame use, kept
 *              in flash between the app's code and the Booster's area
 *              (ASSET_STORE in memmap_rp.ld) and read in place. It holds the
 *              level being played, rewritten from the card's set when
 *              another level starts; it survives a power cycle.
 */

#ifndef STORE_H
#define STORE_H

#include <stdint.h>

#include "x2io.h"
#include "x2level.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* STORE_H */
