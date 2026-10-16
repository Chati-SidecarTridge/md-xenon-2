/**
 * File: lzhuf.h
 * Description: LZHUF decoding as in LHarc's -lh1- method: LZSS over a
 *              4,096-byte ring (match lengths 3-60) with adaptive Huffman
 *              coding of literals and lengths, and a fixed prefix code for
 *              the upper 6 bits of match positions; the ring starts with
 *              -lh1-'s fixed pattern. Integers only, no allocation, no SDK:
 *              the state is about 8.6 KB, the caller's.
 */

#ifndef LZHUF_H
#define LZHUF_H

#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LZHUF_N 4096
#define LZHUF_F 60
#define LZHUF_THRESHOLD 2
#define LZHUF_N_CHAR (256 - LZHUF_THRESHOLD + LZHUF_F) /* 314 */
#define LZHUF_T (LZHUF_N_CHAR * 2 - 1)                  /* 627 */

/* The next input byte, 0-255; anything else ends the input (read as 0). */
typedef int (*lzhuf_getbyte_fn)(void *ctx);

typedef struct {
  uint16_t freq[LZHUF_T + 1];
  uint16_t prnt[LZHUF_T + LZHUF_N_CHAR];
  uint16_t son[LZHUF_T];
  uint8_t ring[LZHUF_N];
  /* bit input */
  lzhuf_getbyte_fn get;
  void *get_ctx;
  uint32_t fetched; /* bytes taken from the input */
  uint16_t buf;
  uint8_t bits;
} lzhuf_t;

/* Unpack `length` bytes, written to `out` from offset 0 in pieces of up to
 * `chunk_size` bytes through `chunk` (the caller's buffer). `consumed`, if
 * not NULL, gets the number of input bytes used, as LZHUF counts them (the
 * bytes taken minus the whole bytes still in the bit buffer). Progress, if
 * given, is called every chunk. X2_OK or the sink's error. */
int lzhuf_decode(lzhuf_t *z, lzhuf_getbyte_fn get, void *get_ctx,
                 uint32_t length, const x2_sink_t *out, uint8_t *chunk,
                 uint32_t chunk_size, uint32_t *consumed,
                 x2_progress_fn progress, void *progress_ctx);

#ifdef __cplusplus
}
#endif

#endif /* LZHUF_H */
