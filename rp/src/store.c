/**
 * File: store.c
 * Description: The asset store in flash (store.h).
 *
 * A level image is written in order: the store is erased for its size
 * first, then programmed a 4 KB sector at a time as the converter writes;
 * the converter's last write, the header's magic at offset 0, programs the
 * first page again with every other byte 0xFF, which leaves them as they
 * are. So an image cut short (a reset, a pulled card) has no magic and is
 * made again at the next start.
 */

#include "store.h"

#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "debug.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

extern uint8_t _asset_store_end;

#define ERASE_CHUNK (64u * 1024u) /* a block erase: about 150 ms */

typedef struct {
  uint32_t base; /* the store's offset in flash */
  uint32_t at;   /* bytes written so far */
  uint32_t fill; /* bytes waiting in buf */
  uint32_t total;
  x2_progress_fn progress;
  void *progress_ctx;
  uint8_t buf[FLASH_SECTOR_SIZE];
} writer_t;

static uint32_t store_offset(void) {
  return (uint32_t)&_asset_store_start - XIP_BASE;
}

static uint32_t store_capacity(void) {
  return (uint32_t)&_asset_store_end - (uint32_t)&_asset_store_start;
}

const x2level_header_t *store_level(uint32_t level, uint32_t source_crc) {
  const x2level_header_t *h =
      (const x2level_header_t *)(uintptr_t)&_asset_store_start;
  if (h->magic != X2LEVEL_MAGIC || h->version != X2LEVEL_VERSION ||
      h->level != level || h->source_crc != source_crc ||
      h->size > store_capacity()) {
    return NULL;
  }
  return h;
}

static void program(uint32_t offset, const uint8_t *data, uint32_t len) {
  uint32_t ints = save_and_disable_interrupts();
  flash_range_program(offset, data, len);
  restore_interrupts(ints);
}

static void flush(writer_t *w) {
  if (w->fill == 0) return;
  memset(w->buf + w->fill, 0xFF, sizeof(w->buf) - w->fill);
  /* The sector holding the last `fill` bytes written. */
  uint32_t sector = (w->at - w->fill) / FLASH_SECTOR_SIZE * FLASH_SECTOR_SIZE;
  program(w->base + sector, w->buf, FLASH_SECTOR_SIZE);
  w->fill = 0;
  if (w->progress) w->progress(w->progress_ctx, w->at, w->total);
}

static int sink_write(void *ctx, uint32_t offset, const void *data,
                      uint32_t len) {
  writer_t *w = ctx;
  if (offset == 0 && w->at > 0) {
    /* The magic, last: the first page again, 0xFF but for it. */
    uint8_t page[FLASH_PAGE_SIZE];
    if (len > sizeof(page)) return X2_ERR_WRITE;
    flush(w);
    memset(page, 0xFF, sizeof(page));
    memcpy(page, data, len);
    program(w->base, page, sizeof(page));
    return X2_OK;
  }
  if (offset != w->at || w->at + len > w->total) return X2_ERR_WRITE;
  const uint8_t *p = data;
  while (len) {
    uint32_t n = sizeof(w->buf) - w->fill;
    if (n > len) n = len;
    memcpy(w->buf + w->fill, p, n);
    w->fill += n;
    w->at += n;
    p += n;
    len -= n;
    if (w->fill == sizeof(w->buf)) flush(w);
  }
  return X2_OK;
}

int store_write_level(const x2_source_t *src, uint32_t size, uint32_t level,
                      uint32_t source_crc, x2_progress_fn progress,
                      void *progress_ctx) {
  uint8_t head[64];
  if (src->read(src->ctx, 0, head, sizeof(head)) != 0) return X2_ERR_READ;
  uint32_t total = x2level_image_size(head, size);
  if (total == 0) return X2_ERR_FORMAT;
  if (total > store_capacity()) return X2_ERR_RANGE;

  writer_t *w = malloc(sizeof(writer_t));
  uint8_t *work = malloc(X2LEVEL_WORK_SIZE);
  if (!w || !work) {
    free(w);
    free(work);
    return X2_ERR_RANGE;
  }
  w->base = store_offset();
  w->at = 0;
  w->fill = 0;
  w->total = total;
  w->progress = progress;
  w->progress_ctx = progress_ctx;

  uint32_t erase = (total + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE *
                   FLASH_SECTOR_SIZE;
  for (uint32_t done = 0; done < erase;) {
    uint32_t n = erase - done < ERASE_CHUNK ? erase - done : ERASE_CHUNK;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(w->base + done, n);
    restore_interrupts(ints);
    done += n;
    if (progress) progress(progress_ctx, 0, total);
  }

  x2_sink_t sink = {w, sink_write};
  int e = x2level_convert(src, size, level, source_crc, &sink, work, NULL,
                          NULL);
  DPRINTF("store: level %lu, %lu bytes, result %d\n", (unsigned long)level,
          (unsigned long)total, e);
  free(work);
  free(w);
  return e;
}
