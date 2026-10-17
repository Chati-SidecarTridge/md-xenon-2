/**
 * File: x2sprite.c
 * Description: Masked sprites and animation lists (x2sprite.h).
 */

#include "x2sprite.h"

#include <stddef.h>

#define MAX_LIST 256    /* entries in one list before it is taken as runaway */
#define MAX_VISITS 16   /* lists followed from one root */
#define GROUP_BYTES 10u /* a 16-pixel group of a line: 4 planes and the mask */

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

static int read_at(const x2_source_t *src, uint32_t base, uint32_t addr,
                   void *buf, uint32_t len) {
  if (addr < base) return X2_ERR_FORMAT;
  return src->read(src->ctx, addr - base, buf, len) == 0 ? X2_OK : X2_ERR_READ;
}

/* Insert `a` into the sorted `addrs`, once. */
static int insert(uint32_t *addrs, int *count, int max, uint32_t a) {
  int i = *count;
  while (i > 0 && addrs[i - 1] > a) i--;
  if (i > 0 && addrs[i - 1] == a) return X2_OK;
  if (*count >= max) return X2_ERR_RANGE;
  for (int j = *count; j > i; j--) addrs[j] = addrs[j - 1];
  addrs[i] = a;
  (*count)++;
  return X2_OK;
}

int x2sprite_collect(const x2_source_t *src, uint32_t base,
                     const uint32_t *anims, int n_anims,
                     const uint32_t *singles, int n_singles, uint32_t *addrs,
                     int max, int *count) {
  int e;
  *count = 0;
  for (int i = 0; i < n_singles; i++) {
    if ((e = insert(addrs, count, max, singles[i])) != X2_OK) return e;
  }
  for (int r = 0; r < n_anims; r++) {
    uint32_t visited[MAX_VISITS], list = anims[r];
    int visits = 0;
    while (list) {
      for (int v = 0; v < visits; v++) {
        if (visited[v] == list) list = 0;
      }
      if (!list) break;
      if (visits == MAX_VISITS) return X2_ERR_FORMAT;
      visited[visits++] = list;
      uint32_t a = list;
      list = 0;
      for (int n = 0;; n++) {
        uint8_t entry[8];
        if (n == MAX_LIST) return X2_ERR_FORMAT;
        if ((e = read_at(src, base, a, entry, 8)) != X2_OK) return e;
        uint32_t sprite = be32(entry);
        if (sprite) {
          if ((e = insert(addrs, count, max, sprite)) != X2_OK) return e;
          a += 6;
          continue;
        }
        uint32_t routine = be32(entry + 4);
        if (routine == X2SPRITE_LOOP || routine == X2SPRITE_LOOP_AT) {
          if ((e = read_at(src, base, a + 8, entry, 4)) != X2_OK) return e;
          list = be32(entry);
        }
        break;
      }
    }
  }
  return X2_OK;
}

int x2sprite_shape(const x2_source_t *src, uint32_t base, uint32_t addr,
                   x2_sprite_t *s) {
  uint8_t h[16];
  int e = read_at(src, base, addr - 8u, h, sizeof(h));
  if (e != X2_OK) return e;
  s->source = addr;
  s->box_x = (int16_t)be16(h);
  s->box_y = (int16_t)be16(h + 2);
  s->box_w = (int16_t)be16(h + 4);
  s->box_h = (int16_t)be16(h + 6);
  s->hot_x = (int16_t)be16(h + 8);
  s->hot_y = (int16_t)be16(h + 10);
  s->width = (uint16_t)((be16(h + 12) + 15u) / 16u * 16u);
  s->height = (uint16_t)(be16(h + 14) + 1u);
  if (s->width == 0 || s->width > X2SPRITE_MAX_WIDTH ||
      s->height > X2SPRITE_MAX_LINES) {
    return X2_ERR_FORMAT;
  }
  return X2_OK;
}

int x2sprite_layout(const x2_source_t *src, uint32_t base,
                    const uint32_t *addrs, int count, x2_sprite_t *table,
                    uint32_t first, uint32_t *end) {
  for (int i = 0; i < count; i++) {
    int e = x2sprite_shape(src, base, addrs[i], &table[i]);
    if (e != X2_OK) return e;
    table[i].offset = first;
    first += (uint32_t)table[i].width * table[i].height;
  }
  *end = first;
  return X2_OK;
}

int x2sprite_pixels(const x2_source_t *src, uint32_t base,
                    const x2_sprite_t *s, const x2_sink_t *out, uint32_t *at) {
  uint32_t groups = s->width / 16u, line_bytes = groups * GROUP_BYTES;
  uint8_t line[X2SPRITE_MAX_WIDTH / 16u * GROUP_BYTES];
  uint8_t row[X2SPRITE_MAX_WIDTH];
  for (uint32_t y = 0; y < s->height; y++) {
    int e = read_at(src, base, s->source + 8 + y * line_bytes, line, line_bytes);
    if (e != X2_OK) return e;
    for (uint32_t g = 0; g < groups; g++) {
      uint16_t p[5];
      for (uint32_t k = 0; k < 5; k++) p[k] = be16(line + 2 * (k * groups + g));
      for (int x = 0; x < 16; x++) {
        uint16_t m = (uint16_t)(0x8000u >> x);
        row[g * 16 + x] =
            (p[4] & m) ? X2SPRITE_KEY
                       : (uint8_t)(((p[0] & m) ? 1 : 0) | ((p[1] & m) ? 2 : 0) |
                                   ((p[2] & m) ? 4 : 0) | ((p[3] & m) ? 8 : 0));
      }
    }
    if (out->write(out->ctx, *at, row, s->width) != 0) return X2_ERR_WRITE;
    *at += s->width;
  }
  return X2_OK;
}

const x2_sprite_t *x2sprite_find(const x2_sprite_t *table, int count,
                                 uint32_t source) {
  int lo = 0, hi = count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (table[mid].source == source) return &table[mid];
    if (table[mid].source < source) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return NULL;
}
