/**
 * File: x2io.h
 * Description: The read and write hooks the converter works through, so the
 *              same code runs over FatFs on the cartridge and over stdio on
 *              a computer (tools/x2conv). Plain C, no SDK.
 */

#ifndef X2IO_H
#define X2IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read `len` bytes at `offset` into `buf`. Returns 0, or a negative error. */
typedef struct {
  void *ctx;
  int (*read)(void *ctx, uint32_t offset, void *buf, uint32_t len);
} x2_source_t;

/* Write `len` bytes at `offset`. Returns 0, or a negative error. */
typedef struct {
  void *ctx;
  int (*write)(void *ctx, uint32_t offset, const void *buf, uint32_t len);
} x2_sink_t;

/* Progress of a long step: `done` of `total` units. Optional (NULL). */
typedef void (*x2_progress_fn)(void *ctx, uint32_t done, uint32_t total);

#define X2_OK 0
#define X2_ERR_READ (-1)
#define X2_ERR_WRITE (-2)
#define X2_ERR_FORMAT (-3)
#define X2_ERR_RANGE (-4)

#ifdef __cplusplus
}
#endif

#endif /* X2IO_H */
