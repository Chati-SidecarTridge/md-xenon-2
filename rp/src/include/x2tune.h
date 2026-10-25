/**
 * File: x2tune.h
 * Description: The title tune, converted from the boot program (BOOT.X2, as
 *              the original holds it at $40000) for the RP to play: its
 *              samples as signed 8-bit PCM and the list of sections the
 *              boot program plays them in. Plain C, no SDK.
 *
 * The original (the boot program's Timer A handler at $417C4, 7,979 Hz):
 * one unsigned sample a tick, written to the sound chip as three volumes
 * from a 256-entry table that falls from the loudest at 0 to silence at
 * 255; twelve sections (a count and a start address each, a count of 0
 * starting the list again) of two sample blocks, 26.3 s a loop.
 *
 * The image: the header (x2tune_header_t), then the samples from the
 * first block's start to the second's end, each as 127 - the original
 * byte (louder is higher, as PCM). The header also holds, for a sound chip
 * that plays two channels a sample, the pair of volumes whose summed
 * output is nearest the original's three channels for each sample, by
 * the PCM value + 128: as loud as the original on a plain ST.
 */

#ifndef X2TUNE_H
#define X2TUNE_H

#include <stdint.h>

#include "x2io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X2TUNE_MAGIC 0x4E543258u /* "X2TN" */
#define X2TUNE_VERSION 2u
#define X2TUNE_RATE_HZ 7979u     /* 2,457,600 / 4 / 77 */
#define X2TUNE_MAX_SECTIONS 16

typedef struct {
  uint32_t count;  /* samples */
  uint32_t offset; /* from the samples' start */
} x2tune_section_t;

typedef struct {
  uint32_t magic; /* X2TUNE_MAGIC once the image is complete */
  uint32_t version;
  uint32_t source_crc; /* CRC-32 of the boot program it was made from */
  uint32_t size;       /* the whole image */
  uint32_t samples;    /* bytes of samples after the header */
  uint32_t sections;
  x2tune_section_t section[X2TUNE_MAX_SECTIONS];
  uint8_t ym_pairs[256][2]; /* (vA, vB) by PCM value + 128 */
} x2tune_header_t;

/* The two-channel pairs nearest one of the original's three-channel
 * sample tables (256 entries of 8 bytes: 8, vA, 9, vB, 10, vC, 0, 0), by
 * the PCM value (127 - the sample) + 128. */
void x2tune_pairs(const uint8_t table[256 * 8], uint8_t pairs[256][2]);

/* The image's size, or 0 if the boot program's list makes no sense. */
uint32_t x2tune_image_size(const x2_source_t *boot);

/* Convert the boot program `boot` into the image, written from offset 0 of
 * `out` in order, the magic last. */
int x2tune_convert(const x2_source_t *boot, uint32_t source_crc,
                   const x2_sink_t *out);

#ifdef __cplusplus
}
#endif

#endif /* X2TUNE_H */
