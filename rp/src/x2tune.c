/**
 * File: x2tune.c
 * Description: The title tune converted from the boot program (x2tune.h).
 */

#include "x2tune.h"

#include <string.h>

#define BOOT_BASE 0x40000u
#define BOOT_SIZE 0x28000u
#define SECTION_LIST 0x41760u /* (count, address) pairs, a count of 0 ends */
#define VOLUME_TABLE 0x41822u /* 8 bytes a sample: 8, vA, 9, vB, 10, vC, 0, 0 */

/* A channel's output at each volume, x 10,000: 2^((v - 15) / 2) (audio.c's
 * curve). */
static const uint16_t k_amp[16] = {0,    78,   110,  156,  221,  313,
                                   442,  625,  884,  1250, 1768, 2500,
                                   3536, 5000, 7071, 10000};

static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The section list, and the samples' span: [*start, *end). */
static int layout(const x2_source_t *boot, x2tune_header_t *h, uint32_t *start,
                  uint32_t *end) {
  uint8_t pair[8];
  uint32_t lo = 0xFFFFFFFFu, hi = 0;
  h->sections = 0;
  for (uint32_t i = 0;; i++) {
    if (boot->read(boot->ctx, SECTION_LIST - BOOT_BASE + 8 * i, pair, 8) != 0) {
      return X2_ERR_READ;
    }
    uint32_t count = be32(pair), addr = be32(pair + 4);
    if (count == 0) break;
    if (i == X2TUNE_MAX_SECTIONS || addr < BOOT_BASE ||
        addr + count > BOOT_BASE + BOOT_SIZE) {
      return X2_ERR_FORMAT;
    }
    h->section[i].count = count;
    h->section[i].offset = addr;
    h->sections = i + 1;
    if (addr < lo) lo = addr;
    if (addr + count > hi) hi = addr + count;
  }
  if (h->sections == 0) return X2_ERR_FORMAT;
  for (uint32_t i = 0; i < h->sections; i++) h->section[i].offset -= lo;
  *start = lo;
  *end = hi;
  h->samples = hi - lo;
  h->size = (uint32_t)sizeof(*h) + h->samples;
  return X2_OK;
}

void x2tune_pairs(const uint8_t table[256 * 8], uint8_t pairs[256][2]) {
  for (uint32_t b = 0; b < 256u; b++) {
    const uint8_t *e = table + 8u * b;
    uint32_t want = (uint32_t)k_amp[e[1] & 15] + k_amp[e[3] & 15] + k_amp[e[5] & 15];
    uint32_t best = 0, best_diff = 0xFFFFFFFFu;
    for (uint32_t a = 0; a < 16u; a++) {
      for (uint32_t c = a; c < 16u; c++) {
        uint32_t got = (uint32_t)k_amp[a] + k_amp[c];
        uint32_t diff = got > want ? got - want : want - got;
        if (diff < best_diff) {
          best_diff = diff;
          best = a << 4 | c;
        }
      }
    }
    pairs[255u - b][0] = (uint8_t)(best >> 4); /* PCM 127 - b, + 128 */
    pairs[255u - b][1] = (uint8_t)(best & 15u);
  }
}

/* For each original sample, the two volumes nearest its three. */
static int ym_pairs(const x2_source_t *boot, x2tune_header_t *h) {
  static uint8_t table[256 * 8];
  if (boot->read(boot->ctx, VOLUME_TABLE - BOOT_BASE, table, sizeof(table)) != 0) {
    return X2_ERR_READ;
  }
  x2tune_pairs(table, h->ym_pairs);
  return X2_OK;
}

uint32_t x2tune_image_size(const x2_source_t *boot) {
  x2tune_header_t h;
  uint32_t start, end;
  return layout(boot, &h, &start, &end) == X2_OK ? h.size : 0;
}

int x2tune_convert(const x2_source_t *boot, uint32_t source_crc,
                   const x2_sink_t *out) {
  x2tune_header_t h;
  uint32_t start, end;
  memset(&h, 0xFF, sizeof(h)); /* unwritten flash; the magic comes last */
  int e = layout(boot, &h, &start, &end);
  if (e != X2_OK) return e;
  for (uint32_t i = h.sections; i < X2TUNE_MAX_SECTIONS; i++) {
    h.section[i].count = 0;
    h.section[i].offset = 0;
  }
  e = ym_pairs(boot, &h);
  if (e != X2_OK) return e;
  h.version = X2TUNE_VERSION;
  h.source_crc = source_crc;
  uint32_t at = 0;
  if (out->write(out->ctx, at, &h, sizeof(h)) != 0) return X2_ERR_WRITE;
  at += sizeof(h);
  uint8_t buf[256];
  for (uint32_t a = start; a < end; a += sizeof(buf)) {
    uint32_t n = end - a < sizeof(buf) ? end - a : sizeof(buf);
    if (boot->read(boot->ctx, a - BOOT_BASE, buf, n) != 0) return X2_ERR_READ;
    for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)(127 - buf[i]);
    if (out->write(out->ctx, at, buf, n) != 0) return X2_ERR_WRITE;
    at += n;
  }
  if (at != h.size) return X2_ERR_FORMAT;
  uint32_t magic = X2TUNE_MAGIC;
  return out->write(out->ctx, 0, &magic, sizeof(magic)) == 0 ? X2_OK
                                                              : X2_ERR_WRITE;
}
