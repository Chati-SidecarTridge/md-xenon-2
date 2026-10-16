/**
 * File: lzhuf.c
 * Description: LZHUF decoding, LHarc -lh1- (lzhuf.h). The algorithm is
 *              Haruyasu Yoshizaki's LZHUF (1988, after Haruhiko Okumura's
 *              LZSS); -lh1- starts the ring with a fixed pattern instead of
 *              spaces. tools/re/lzhuf.py is the same decoder in Python.
 */

#include "lzhuf.h"

#include <string.h>

#define N LZHUF_N
#define F LZHUF_F
#define N_CHAR LZHUF_N_CHAR
#define T LZHUF_T
#define R (T - 1)
#define MAX_FREQ 0x8000u

/* Upper 6 bits of a match position: the code each first byte gives, and
 * how many bits the position takes; built from runs of equal codes. */
static uint8_t d_code[256];
static uint8_t d_len[256];
static int tables_built;

static void build_tables(void) {
  static const struct {
    uint8_t values, repeat, bits;
  } runs[] = {{1, 32, 3}, {3, 16, 4}, {8, 8, 5}, {12, 4, 6}, {24, 2, 7}, {16, 1, 8}};
  unsigned i = 0, code = 0;
  for (unsigned r = 0; r < sizeof(runs) / sizeof(runs[0]); r++) {
    for (unsigned v = 0; v < runs[r].values; v++, code++) {
      for (unsigned k = 0; k < runs[r].repeat; k++, i++) {
        d_code[i] = (uint8_t)code;
        d_len[i] = runs[r].bits;
      }
    }
  }
  tables_built = 1;
}

static void lh1_ring(uint8_t *ring) {
  unsigned p = 0;
  for (unsigned v = 0; v < 256; v++) {
    memset(ring + p, (int)v, 13);
    p += 13;
  }
  for (unsigned v = 0; v < 256; v++) ring[p++] = (uint8_t)v;
  for (int v = 255; v >= 0; v--) ring[p++] = (uint8_t)v;
  memset(ring + p, 0, 128);
  p += 128;
  memset(ring + p, ' ', 128);
}

static void start_huff(lzhuf_t *z) {
  unsigned i, j;
  for (i = 0; i < N_CHAR; i++) {
    z->freq[i] = 1;
    z->son[i] = (uint16_t)(i + T);
    z->prnt[i + T] = (uint16_t)i;
  }
  i = 0;
  j = N_CHAR;
  while (j <= R) {
    z->freq[j] = (uint16_t)(z->freq[i] + z->freq[i + 1]);
    z->son[j] = (uint16_t)i;
    z->prnt[i] = z->prnt[i + 1] = (uint16_t)j;
    i += 2;
    j++;
  }
  z->freq[T] = 0xFFFF;
  z->prnt[R] = 0;
}

static void reconst(lzhuf_t *z) {
  unsigned i, j, k;
  uint16_t f;
  j = 0;
  for (i = 0; i < T; i++) {
    if (z->son[i] >= T) {
      z->freq[j] = (uint16_t)((z->freq[i] + 1) / 2);
      z->son[j] = z->son[i];
      j++;
    }
  }
  for (i = 0, j = N_CHAR; j < T; i += 2, j++) {
    f = z->freq[j] = (uint16_t)(z->freq[i] + z->freq[i + 1]);
    for (k = j - 1; f < z->freq[k]; k--) {
    }
    k++;
    memmove(&z->freq[k + 1], &z->freq[k], (j - k) * sizeof(z->freq[0]));
    z->freq[k] = f;
    memmove(&z->son[k + 1], &z->son[k], (j - k) * sizeof(z->son[0]));
    z->son[k] = (uint16_t)i;
  }
  for (i = 0; i < T; i++) {
    k = z->son[i];
    if (k >= T) {
      z->prnt[k] = (uint16_t)i;
    } else {
      z->prnt[k] = z->prnt[k + 1] = (uint16_t)i;
    }
  }
}

static void update(lzhuf_t *z, unsigned c) {
  unsigned i, j, k, l;
  if (z->freq[R] == MAX_FREQ) reconst(z);
  c = z->prnt[c + T];
  do {
    k = ++z->freq[c];
    l = c + 1;
    if (k > z->freq[l]) {
      while (k > z->freq[l + 1]) l++;
      z->freq[c] = z->freq[l];
      z->freq[l] = (uint16_t)k;
      i = z->son[c];
      z->prnt[i] = (uint16_t)l;
      if (i < T) z->prnt[i + 1] = (uint16_t)l;
      j = z->son[l];
      z->son[l] = (uint16_t)i;
      z->prnt[j] = (uint16_t)c;
      if (j < T) z->prnt[j + 1] = (uint16_t)c;
      z->son[c] = (uint16_t)j;
      c = l;
    }
  } while ((c = z->prnt[c]) != 0);
}

/* The bit buffer: 16 bits, refilled a byte at a time while 8 or fewer are
 * left, as LZHUF does (tools/re/lzhuf.py's Bits). */
static void fill(lzhuf_t *z) {
  while (z->bits <= 8) {
    int b = z->get(z->get_ctx);
    z->fetched++;
    if (b < 0 || b > 255) b = 0;
    z->buf = (uint16_t)(z->buf | (unsigned)b << (8 - z->bits));
    z->bits = (uint8_t)(z->bits + 8);
  }
}

static unsigned get_bit(lzhuf_t *z) {
  fill(z);
  unsigned v = (z->buf >> 15) & 1u;
  z->buf = (uint16_t)(z->buf << 1);
  z->bits--;
  return v;
}

static unsigned get_byte(lzhuf_t *z) {
  fill(z);
  unsigned v = z->buf >> 8;
  z->buf = (uint16_t)(z->buf << 8);
  z->bits = (uint8_t)(z->bits - 8);
  return v;
}

static unsigned decode_char(lzhuf_t *z) {
  unsigned c = z->son[R];
  while (c < T) c = z->son[c + get_bit(z)];
  c -= T;
  update(z, c);
  return c;
}

static unsigned decode_position(lzhuf_t *z) {
  unsigned i = get_byte(z);
  unsigned c = (unsigned)d_code[i] << 6;
  for (unsigned j = d_len[i] - 2; j > 0; j--) i = (i << 1) + get_bit(z);
  return c | (i & 0x3F);
}

int lzhuf_decode(lzhuf_t *z, lzhuf_getbyte_fn get, void *get_ctx,
                 uint32_t length, const x2_sink_t *out, uint8_t *chunk,
                 uint32_t chunk_size, uint32_t *consumed,
                 x2_progress_fn progress, void *progress_ctx) {
  if (!tables_built) build_tables();
  lh1_ring(z->ring);
  start_huff(z);
  z->get = get;
  z->get_ctx = get_ctx;
  z->fetched = 0;
  z->buf = 0;
  z->bits = 0;
  unsigned r = N - F;
  uint32_t done = 0, fill_level = 0;
  while (done < length) {
    unsigned c = decode_char(z);
    unsigned count;
    unsigned from = 0;
    if (c < 256) {
      count = 1;
    } else {
      from = (r - decode_position(z) - 1) & (N - 1);
      count = c - 255 + LZHUF_THRESHOLD;
    }
    for (unsigned k = 0; k < count && done < length; k++) {
      uint8_t b = c < 256 ? (uint8_t)c : z->ring[(from + k) & (N - 1)];
      z->ring[r] = b;
      r = (r + 1) & (N - 1);
      chunk[fill_level++] = b;
      done++;
      if (fill_level == chunk_size || done == length) {
        int e = out->write(out->ctx, done - fill_level, chunk, fill_level);
        if (e != 0) return X2_ERR_WRITE;
        fill_level = 0;
        if (progress) progress(progress_ctx, done, length);
      }
    }
  }
  if (consumed) *consumed = z->fetched - z->bits / 8;
  return X2_OK;
}
