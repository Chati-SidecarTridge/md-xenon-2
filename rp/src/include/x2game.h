/**
 * File: x2game.h
 * Description: The original game's disks, read the way its loader reads
 *              them: the boot-time program and the game image decrypted,
 *              the levels unpacked from the level stream, the logo band and
 *              palette the boot program shows. Everything streams through
 *              the x2io hooks; nothing large is held in RAM. Plain C, no SDK.
 *              tools/re/x2disk.py is the same in Python.
 */

#ifndef X2GAME_H
#define X2GAME_H

#include <stdint.h>

#include "lzhuf.h"
#include "stx.h"
#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X2_BOOT_SIZE 163840u  /* disk 1 tracks 1-32, at $40000 */
#define X2_GAME_SIZE 235520u  /* disk 1 tracks 34-79, at $400 */
#define X2_LEVELS 5
#define X2_LEVEL_MAX 200000u  /* the largest level unpacks to 183,880 */
#define X2_SCREEN_SIZE 32000u
#define X2_TITLE_SIZE (X2_SCREEN_SIZE + 32u) /* the screen, its palette */
#define X2_LEVEL1_START 512u  /* virtual sector 1, byte 0 */

/* What a correct dump of the Image Works release converts to: each output's
 * size and CRC-32 (crc32.h). An extraction that differs comes from another
 * dump. */
typedef enum {
  X2_OUT_BOOT,
  X2_OUT_GAME,
  X2_OUT_TITLE,
  X2_OUT_LEVEL1,
  X2_OUT_LEVEL2,
  X2_OUT_LEVEL3,
  X2_OUT_LEVEL4,
  X2_OUT_LEVEL5,
  X2_OUTPUTS
} x2_output_t;

typedef struct {
  const char *name; /* as tools/x2conv names the file */
  uint32_t size;
  uint32_t crc32;
} x2_known_t;

extern const x2_known_t x2_known[X2_OUTPUTS];

/* Both disks are the Image Works release: the boot sectors carry the names
 * "X2" and "X2B". X2_OK, X2_ERR_FORMAT (another disk) or a read error. */
int x2_check_disks(const stx_image_t *disk1, const stx_image_t *disk2);

/* The boot-time program as it sits at $40000 once it has decrypted itself
 * (X2_BOOT_SIZE bytes). */
int x2_extract_boot(const stx_image_t *disk1, const x2_sink_t *out,
                    x2_progress_fn progress, void *progress_ctx);

/* The game image as it sits at $400 once decrypted (X2_GAME_SIZE bytes). */
int x2_extract_game(const stx_image_t *disk1, const x2_sink_t *out,
                    x2_progress_fn progress, void *progress_ctx);

/* The title: an ST low-resolution screen, black but for the logo band the
 * boot program copies at full scale, then its 16 palette words
 * (X2_TITLE_SIZE bytes). `boot` reads the extracted boot program. */
int x2_make_title(const x2_source_t *boot, const x2_sink_t *out);

/* One level, unpacked, from the level stream position `*pos` (virtual
 * byte: sector * 512 + offset; level 1 starts at X2_LEVEL1_START, each
 * other where the one before ends). `game` reads the extracted game image.
 * On X2_OK `*pos` is where the next level starts and `*length` this one's
 * unpacked size. `z` and `chunk` are the caller's (chunk: 512 bytes). */
int x2_extract_level(const x2_source_t *game, const stx_image_t *disk2,
                     uint32_t *pos, lzhuf_t *z, uint8_t *chunk,
                     const x2_sink_t *out, uint32_t *length,
                     x2_progress_fn progress, void *progress_ctx);

#ifdef __cplusplus
}
#endif

#endif /* X2GAME_H */
