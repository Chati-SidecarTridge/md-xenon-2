/**
 * File: x2game.c
 * Description: The original's disks, as its loader reads them (x2game.h).
 *
 * Disk 1, side 0, tracks 1-32 are the boot-time program, loaded at $40000.
 * It decrypts itself in two layers: first each byte from $40000 up to
 * BOOT_SRC_END - BOOT_SHIFT becomes the byte before it XORed with the
 * program's byte BOOT_SHIFT further on (the first with SEED); then, from
 * BOOT_LAYER2, each byte is XORed with the decrypted byte before it, once,
 * the first with SEED.
 *
 * Tracks 34-79 are the game image, loaded at $400 and decrypted in place in
 * two passes: each byte XORed with the decrypted byte before it, the first
 * with SEED. Both passes run in one sweep here.
 *
 * Levels are read through virtual sectors: 0 is disk 2's boot sector, 1-18
 * are 512-byte blocks of the game image at RESIDENT + 512 * n, n > 18 is
 * disk 2's logical sector n - 18. A level is zero bytes, a marker byte, its
 * unpacked length (32-bit little-endian), then LZHUF data (lzhuf.h).
 */

#include "x2game.h"

#include <string.h>


#define TRACK_BYTES (10u * STX_SECTOR_BYTES)
#define SEED 0x2Au

#define BOOT_FIRST_LSN 10u /* track 1 */
#define BOOT_BASE 0x40000u
#define BOOT_SHIFT 0x6F0u
#define BOOT_SRC_END 0x67CFEu
#define BOOT_LAYER2 0x40A7Cu

#define GAME_FIRST_LSN 340u /* track 34 */
#define GAME_BASE 0x400u

#define RESIDENT 0x32B5Cu
#define RESIDENT_SECTORS 18u
#define DISK2_SECTORS 800u

#define TITLE_BAND 0x4994Eu   /* in the boot program */
#define TITLE_BAND_LINES 54u
#define TITLE_BAND_DEST (40u * 160u + 4u)
#define TITLE_PALETTE 0x40A5Cu

const x2_known_t x2_known[X2_OUTPUTS] = {
    {"boot.bin", X2_BOOT_SIZE, 0x735F189Au},
    {"game.bin", X2_GAME_SIZE, 0xDF3B45C9u},
    {"title.bin", X2_TITLE_SIZE, 0x0A4C9F49u},
    {"level1.bin", 124060u, 0x93BF3755u},
    {"level2.bin", 137836u, 0xA134D500u},
    {"level3.bin", 145496u, 0x7C1E8551u},
    {"level4.bin", 183880u, 0x33B927A8u},
    {"level5.bin", 165944u, 0xD9E6FF0Du},
};

/* A one-sector cache over a disk's logical sectors. */
typedef struct {
  const stx_image_t *img;
  uint32_t first_lsn;
  int32_t cached; /* the cached sector, -1 for none */
  uint8_t data[STX_SECTOR_BYTES];
} sector_reader_t;

static int byte_at(sector_reader_t *s, uint32_t offset, uint8_t *b) {
  uint32_t sector = offset / STX_SECTOR_BYTES;
  if ((int32_t)sector != s->cached) {
    int e = stx_read_logical(s->img, s->first_lsn + sector, s->data);
    if (e != X2_OK) return e;
    s->cached = (int32_t)sector;
  }
  *b = s->data[offset % STX_SECTOR_BYTES];
  return X2_OK;
}

int x2_check_disks(const stx_image_t *disk1, const stx_image_t *disk2) {
  uint8_t b[STX_SECTOR_BYTES];
  int e = stx_read_logical(disk1, 0, b);
  if (e != X2_OK) return e;
  if (memcmp(b + 2, "X2    ", 6) != 0) return X2_ERR_FORMAT;
  e = stx_read_logical(disk2, 0, b);
  if (e != X2_OK) return e;
  if (memcmp(b + 2, "X2B   ", 6) != 0) return X2_ERR_FORMAT;
  return X2_OK;
}

int x2_extract_boot(const stx_image_t *disk1, const x2_sink_t *out,
                    x2_progress_fn progress, void *progress_ctx) {
  /* Two readers: the byte being decrypted and the one BOOT_SHIFT ahead. */
  static sector_reader_t here, ahead;
  here = (sector_reader_t){disk1, BOOT_FIRST_LSN, -1, {0}};
  ahead = (sector_reader_t){disk1, BOOT_FIRST_LSN, -1, {0}};
  const uint32_t tail = BOOT_SRC_END - BOOT_SHIFT - BOOT_BASE;
  const uint32_t layer2 = BOOT_LAYER2 - BOOT_BASE;
  uint8_t chunk[STX_SECTOR_BYTES];
  uint8_t d0 = SEED, prev2 = SEED;
  for (uint32_t i = 0; i < X2_BOOT_SIZE; i++) {
    uint8_t v;
    int e;
    if (i < tail) {
      e = byte_at(&ahead, i + BOOT_SHIFT, &v);
      d0 ^= v;
      v = d0;
    } else {
      e = byte_at(&here, i, &v);
    }
    if (e != X2_OK) return e;
    if (i >= layer2) {
      v ^= prev2;
      prev2 = v;
    }
    chunk[i % STX_SECTOR_BYTES] = v;
    if (i % STX_SECTOR_BYTES == STX_SECTOR_BYTES - 1) {
      uint32_t at = i + 1 - STX_SECTOR_BYTES;
      if (out->write(out->ctx, at, chunk, STX_SECTOR_BYTES) != 0) {
        return X2_ERR_WRITE;
      }
      if (progress) progress(progress_ctx, i + 1, X2_BOOT_SIZE);
    }
  }
  return X2_OK;
}

int x2_extract_game(const stx_image_t *disk1, const x2_sink_t *out,
                    x2_progress_fn progress, void *progress_ctx) {
  uint8_t sector[STX_SECTOR_BYTES];
  uint8_t prev1 = SEED, prev2 = SEED;
  for (uint32_t s = 0; s < X2_GAME_SIZE / STX_SECTOR_BYTES; s++) {
    int e = stx_read_logical(disk1, GAME_FIRST_LSN + s, sector);
    if (e != X2_OK) return e;
    for (uint32_t k = 0; k < STX_SECTOR_BYTES; k++) {
      prev1 ^= sector[k];        /* first pass */
      prev2 ^= prev1;            /* second pass, over the first's output */
      sector[k] = prev2;
    }
    if (out->write(out->ctx, s * STX_SECTOR_BYTES, sector, STX_SECTOR_BYTES) !=
        0) {
      return X2_ERR_WRITE;
    }
    if (progress) progress(progress_ctx, (s + 1) * STX_SECTOR_BYTES, X2_GAME_SIZE);
  }
  return X2_OK;
}

int x2_make_title(const x2_source_t *boot, const x2_sink_t *out) {
  uint8_t line[160];
  memset(line, 0, sizeof(line));
  for (uint32_t off = 0; off < X2_SCREEN_SIZE; off += sizeof(line)) {
    if (out->write(out->ctx, off, line, sizeof(line)) != 0) return X2_ERR_WRITE;
  }
  for (uint32_t l = 0; l < TITLE_BAND_LINES; l++) {
    if (boot->read(boot->ctx, TITLE_BAND - BOOT_BASE + l * 160u, line, 160) != 0) {
      return X2_ERR_READ;
    }
    if (out->write(out->ctx, TITLE_BAND_DEST + l * 160u, line, 160) != 0) {
      return X2_ERR_WRITE;
    }
  }
  uint8_t palette[32];
  if (boot->read(boot->ctx, TITLE_PALETTE - BOOT_BASE, palette, 32) != 0) {
    return X2_ERR_READ;
  }
  return out->write(out->ctx, X2_SCREEN_SIZE, palette, 32) == 0 ? X2_OK
                                                                : X2_ERR_WRITE;
}

/* The level stream's virtual sectors, a byte at a time. */
typedef struct {
  const x2_source_t *game;
  const stx_image_t *disk2;
  uint32_t pos;
  int32_t cached;
  int error;
  uint8_t data[STX_SECTOR_BYTES];
} vstream_t;

static int vstream_byte(void *ctx) {
  vstream_t *v = (vstream_t *)ctx;
  uint32_t sector = v->pos / STX_SECTOR_BYTES;
  if ((int32_t)sector != v->cached) {
    int e;
    if (sector >= 1 && sector <= RESIDENT_SECTORS) {
      e = v->game->read(v->game->ctx,
                        RESIDENT - GAME_BASE + sector * STX_SECTOR_BYTES,
                        v->data, STX_SECTOR_BYTES);
    } else if (sector == 0) {
      e = stx_read_logical(v->disk2, 0, v->data);
    } else if (sector - RESIDENT_SECTORS < DISK2_SECTORS) {
      e = stx_read_logical(v->disk2, sector - RESIDENT_SECTORS, v->data);
    } else {
      memset(v->data, 0, sizeof(v->data)); /* past the disk: zeros */
      e = X2_OK;
    }
    if (e != X2_OK) {
      v->error = e;
      memset(v->data, 0, sizeof(v->data));
    }
    v->cached = (int32_t)sector;
  }
  return v->data[v->pos++ % STX_SECTOR_BYTES];
}

int x2_extract_level(const x2_source_t *game, const stx_image_t *disk2,
                     uint32_t *pos, lzhuf_t *z, uint8_t *chunk,
                     const x2_sink_t *out, uint32_t *length,
                     x2_progress_fn progress, void *progress_ctx) {
  static vstream_t v;
  v = (vstream_t){game, disk2, *pos, -1, X2_OK, {0}};
  const uint32_t limit = (DISK2_SECTORS + RESIDENT_SECTORS) * STX_SECTOR_BYTES;
  int b;
  do {
    if (v.pos >= limit) return X2_ERR_FORMAT; /* only padding to the end */
    b = vstream_byte(&v);
  } while (b == 0 && v.error == X2_OK);
  /* that byte was the marker; then the unpacked length */
  uint32_t n = 0;
  for (int k = 0; k < 4; k++) n |= (uint32_t)vstream_byte(&v) << (8 * k);
  if (v.error != X2_OK) return v.error;
  if (n == 0 || n > X2_LEVEL_MAX) return X2_ERR_FORMAT;
  uint32_t data_start = v.pos, used = 0;
  int e = lzhuf_decode(z, vstream_byte, &v, n, out, chunk, STX_SECTOR_BYTES,
                       &used, progress, progress_ctx);
  if (e != X2_OK) return e;
  if (v.error != X2_OK) return v.error;
  *pos = data_start + used;
  *length = n;
  return X2_OK;
}
