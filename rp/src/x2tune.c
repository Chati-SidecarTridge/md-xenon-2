/**
 * File: x2tune.c
 * Description: The title tune converted from the boot program (x2tune.h).
 */

#include "x2tune.h"

#include <string.h>

#define BOOT_BASE 0x40000u
#define BOOT_SIZE 0x28000u
#define SECTION_LIST 0x41760u /* (count, address) pairs, a count of 0 ends */

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
