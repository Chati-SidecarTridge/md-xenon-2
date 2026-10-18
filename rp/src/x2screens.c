/**
 * File: x2screens.c
 * Description: The screens outside play converted from the game image
 *              (x2screens.h).
 */

#include "x2screens.h"

#include <string.h>

#define GAME_BASE 0x400u
#define SHOP_START 0x10520u /* its strings, tables and pictures */
#define SHOP_END 0x1FB92u
#define ADVICE_START 0x77F2u /* the game's own third advice */
#define FLOW_START 0x83D4u   /* the attract's and the menus' tables and texts */
#define FLOW_END 0x8F90u
#define FONTS_START 0x9DC0u  /* the two fonts and the logo */
#define FONTS_END 0xE2E0u
#define ADVICE_END 0x7850u
#define ITEM_LISTS 0x10F10u /* 27 lists of sprite pointers, 0 ends one */
#define ITEMS 27
#define GROUP_BYTES 10u     /* a sprite's 16 pixels of a line, with the mask */
#define MAX_RANGES 256 /* the lists name 169 sprites, some twice */

static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

static int read_at(const x2_source_t *game, uint32_t addr, void *buf,
                   uint32_t n) {
  if (addr < GAME_BASE) return X2_ERR_FORMAT;
  return game->read(game->ctx, addr - GAME_BASE, buf, n) == 0 ? X2_OK
                                                              : X2_ERR_READ;
}

typedef struct {
  uint32_t start, end;
} range_t;

static int add(range_t *r, int *n, uint32_t start, uint32_t end) {
  if (*n == MAX_RANGES) return X2_ERR_FORMAT;
  r[(*n)++] = (range_t){start, end};
  return X2_OK;
}

/* Every range the screens read, sorted and merged into windows. */
static int layout(const x2_source_t *game, x2screens_header_t *h) {
  static range_t r[MAX_RANGES]; /* 2 KB: too much for core 0's stack */
  int n = 0, e;
  if ((e = add(r, &n, SHOP_START, SHOP_END)) != X2_OK) return e;
  if ((e = add(r, &n, ADVICE_START, ADVICE_END)) != X2_OK) return e;
  if ((e = add(r, &n, FLOW_START, FLOW_END)) != X2_OK) return e;
  if ((e = add(r, &n, FONTS_START, FONTS_END)) != X2_OK) return e;
  uint8_t b[8];
  for (uint32_t id = 0; id < ITEMS; id++) {
    if ((e = read_at(game, ITEM_LISTS + 4u * id, b, 4)) != X2_OK) return e;
    for (uint32_t a = be32(b), k = 0; k < 64; k++, a += 4) {
      if ((e = read_at(game, a, b, 4)) != X2_OK) return e;
      uint32_t s = be32(b);
      if (s < GAME_BASE || s >= 0x80000u) break; /* 0, then the loop */
      if ((e = read_at(game, s, b, 8)) != X2_OK) return e;
      uint32_t groups = (be16(b + 4) + 15u) / 16u, lines = be16(b + 6) + 1u;
      if (groups == 0 || groups > 8 || lines > 200) return X2_ERR_FORMAT;
      if ((e = add(r, &n, s, s + 8u + lines * groups * GROUP_BYTES)) != X2_OK) {
        return e;
      }
    }
  }
  for (int i = 1; i < n; i++) { /* sort by start */
    range_t t = r[i];
    int j = i - 1;
    for (; j >= 0 && r[j].start > t.start; j--) r[j + 1] = r[j];
    r[j + 1] = t;
  }
  h->windows = 0;
  uint32_t offset = (uint32_t)sizeof(*h);
  for (int i = 0; i < n;) {
    uint32_t start = r[i].start & ~1u, end = r[i].end;
    for (i++; i < n && r[i].start <= end + 64u; i++) {
      if (r[i].end > end) end = r[i].end;
    }
    end = (end + 1u) & ~1u;
    if (h->windows == X2SCREENS_WINDOWS) return X2_ERR_FORMAT;
    x2screens_window_t *w = &h->window[h->windows++];
    w->start = start;
    w->size = end - start;
    w->offset = offset;
    offset += (w->size + 3u) & ~3u;
  }
  h->size = offset;
  return X2_OK;
}

uint32_t x2screens_image_size(const x2_source_t *game) {
  x2screens_header_t h;
  return layout(game, &h) == X2_OK ? h.size : 0;
}

int x2screens_convert(const x2_source_t *game, uint32_t source_crc,
                   const x2_sink_t *out) {
  x2screens_header_t h;
  memset(&h, 0xFF, sizeof(h)); /* unwritten flash; the magic comes last */
  int e = layout(game, &h);
  if (e != X2_OK) return e;
  for (uint32_t w = h.windows; w < X2SCREENS_WINDOWS; w++) {
    h.window[w] = (x2screens_window_t){0, 0, 0};
  }
  h.version = X2SCREENS_VERSION;
  h.source_crc = source_crc;
  if (out->write(out->ctx, 0, &h, sizeof(h)) != 0) return X2_ERR_WRITE;
  uint8_t buf[256];
  for (uint32_t w = 0; w < h.windows; w++) {
    const x2screens_window_t *win = &h.window[w];
    uint32_t len = (win->size + 3u) & ~3u;
    for (uint32_t done = 0; done < len; done += sizeof(buf)) {
      uint32_t n = len - done < sizeof(buf) ? len - done : sizeof(buf);
      memset(buf, 0, sizeof(buf));
      uint32_t real = done >= win->size ? 0
                      : done + n <= win->size ? n : win->size - done;
      if (real && (e = read_at(game, win->start + done, buf, real)) != X2_OK) {
        return e;
      }
      if (out->write(out->ctx, win->offset + done, buf, n) != 0) {
        return X2_ERR_WRITE;
      }
    }
  }
  uint32_t magic = X2SCREENS_MAGIC;
  return out->write(out->ctx, 0, &magic, sizeof(magic)) == 0 ? X2_OK
                                                              : X2_ERR_WRITE;
}

const uint8_t *x2screens_raw(const x2screens_header_t *h, uint32_t addr,
                          uint32_t len) {
  for (uint32_t w = 0; w < h->windows && w < X2SCREENS_WINDOWS; w++) {
    const x2screens_window_t *win = &h->window[w];
    if (addr >= win->start && addr + len <= win->start + win->size) {
      return (const uint8_t *)h + win->offset + (addr - win->start);
    }
  }
  return NULL;
}
