/**
 * File: crc32.c
 * Description: CRC-32, reflected, polynomial 0xEDB88320 (crc32.h), with a
 *              256-entry table built on first use.
 */

#include "crc32.h"

static uint32_t table[256];
static int table_built;

static void build(void) {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    table[n] = c;
  }
  table_built = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len) {
  if (!table_built) build();
  const uint8_t *p = (const uint8_t *)data;
  crc = ~crc;
  while (len--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
