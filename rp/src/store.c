/**
 * File: store.c
 * Description: The asset store in flash (store.h).
 *
 * An image (a level, or the shared graphics) is written in order: its
 * section is erased for its size first, then programmed a 4 KB sector at a
 * time as the converter writes;
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

static const uint8_t *store_base(void) {
  return (const uint8_t *)(uintptr_t)&_asset_store_start;
}

const x2level_header_t *store_level(uint32_t level, uint32_t source_crc) {
  const x2level_header_t *h =
      (const x2level_header_t *)(uintptr_t)&_asset_store_start;
  if (h->magic != X2LEVEL_MAGIC || h->version != X2LEVEL_VERSION ||
      h->level != level || h->source_crc != source_crc ||
      h->size > STORE_SCREENS_OFFSET) {
    return NULL;
  }
  return h;
}

const x2screens_header_t *store_screens(uint32_t source_crc) {
  const x2screens_header_t *h =
      (const x2screens_header_t *)(store_base() + STORE_SCREENS_OFFSET);
  if (h->magic != X2SCREENS_MAGIC || h->version != X2SCREENS_VERSION ||
      h->source_crc != source_crc ||
      h->size > STORE_TUNE_OFFSET - STORE_SCREENS_OFFSET) {
    return NULL;
  }
  return h;
}

const x2tune_header_t *store_tune(uint32_t source_crc) {
  const x2tune_header_t *h =
      (const x2tune_header_t *)(store_base() + STORE_TUNE_OFFSET);
  if (h->magic != X2TUNE_MAGIC || h->version != X2TUNE_VERSION ||
      h->source_crc != source_crc ||
      h->size > STORE_GLOBAL_OFFSET - STORE_TUNE_OFFSET) {
    return NULL;
  }
  return h;
}

const x2global_header_t *store_global(uint32_t source_crc) {
  const x2global_header_t *h =
      (const x2global_header_t *)(store_base() + STORE_GLOBAL_OFFSET);
  if (h->magic != X2GLOBAL_MAGIC || h->version != X2GLOBAL_VERSION ||
      h->source_crc != source_crc ||
      h->size > store_capacity() - STORE_GLOBAL_OFFSET) {
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

/* What a section's conversion needs, so one writer serves both. */
typedef struct {
  const x2_source_t *src;
  uint32_t size, level, source_crc;
  uint8_t *work;
} job_t;

typedef int (*convert_fn)(const job_t *job, const x2_sink_t *sink);

static int convert_level(const job_t *job, const x2_sink_t *sink) {
  return x2level_convert(job->src, job->size, job->level, job->source_crc,
                         sink, job->work, NULL, NULL);
}

static int convert_global(const job_t *job, const x2_sink_t *sink) {
  return x2global_convert(job->src, job->source_crc, sink, NULL);
}

static int convert_screens(const job_t *job, const x2_sink_t *sink) {
  return x2screens_convert(job->src, job->source_crc, sink);
}

static int convert_tune(const job_t *job, const x2_sink_t *sink) {
  return x2tune_convert(job->src, job->source_crc, sink);
}

/* Erase `total` bytes of the section at `section`, then program what
 * `convert` writes. */
static int write_section(uint32_t section, uint32_t total, convert_fn convert,
                         const job_t *job, x2_progress_fn progress,
                         void *progress_ctx) {
  writer_t *w = malloc(sizeof(writer_t));
  if (!w) return X2_ERR_RANGE;
  w->base = store_offset() + section;
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
  int e = convert(job, &sink);
  free(w);
  return e;
}

int store_write_level(const x2_source_t *src, uint32_t size, uint32_t level,
                      uint32_t source_crc, x2_progress_fn progress,
                      void *progress_ctx) {
  job_t job = {src, size, level, source_crc, malloc(X2LEVEL_WORK_SIZE)};
  if (!job.work) return X2_ERR_RANGE;
  uint32_t total = x2level_image_size(src, size, level, job.work);
  int e = total == 0                     ? X2_ERR_FORMAT
          : total > STORE_SCREENS_OFFSET ? X2_ERR_RANGE
                                         : write_section(0, total, convert_level,
                                                         &job, progress,
                                                         progress_ctx);
  DPRINTF("store: level %lu, %lu bytes, result %d\n", (unsigned long)level,
          (unsigned long)total, e);
  free(job.work);
  return e;
}

/* The image as it is, a block at a time, its magic left erased until the
 * header goes down again last. */
static int copy_level(const job_t *job, const x2_sink_t *sink) {
  uint8_t buf[512];
  x2level_header_t h;
  for (uint32_t at = 0; at < job->size;) {
    uint32_t n = job->size - at < sizeof(buf) ? job->size - at : sizeof(buf);
    int e = job->src->read(job->src->ctx, at, buf, n);
    if (e != X2_OK) return e;
    if (at == 0) {
      if (n < sizeof(h)) return X2_ERR_FORMAT;
      memcpy(&h, buf, sizeof(h));
      memset(buf, 0xFF, sizeof(h.magic));
    }
    e = sink->write(sink->ctx, at, buf, n);
    if (e != X2_OK) return e;
    at += n;
  }
  return sink->write(sink->ctx, 0, &h, sizeof(h));
}

int store_copy_level(const x2_source_t *img, uint32_t size, uint32_t level,
                     uint32_t source_crc, x2_progress_fn progress,
                     void *progress_ctx) {
  x2level_header_t h;
  if (size < sizeof(h) || img->read(img->ctx, 0, &h, sizeof(h)) != X2_OK ||
      h.magic != X2LEVEL_MAGIC || h.version != X2LEVEL_VERSION ||
      h.level != level || h.source_crc != source_crc || h.size != size ||
      size > STORE_SCREENS_OFFSET) {
    return X2_ERR_FORMAT;
  }
  job_t job = {img, size, level, source_crc, NULL};
  int e = write_section(0, size, copy_level, &job, progress, progress_ctx);
  DPRINTF("store: level %lu copied, %lu bytes, result %d\n", (unsigned long)level,
          (unsigned long)size, e);
  return e;
}

int store_write_screens(const x2_source_t *game, uint32_t source_crc,
                        x2_progress_fn progress, void *progress_ctx) {
  uint32_t total = x2screens_image_size(game);
  if (total == 0) return X2_ERR_FORMAT;
  if (total > STORE_TUNE_OFFSET - STORE_SCREENS_OFFSET) return X2_ERR_RANGE;
  job_t job = {game, 0, 0, source_crc, NULL};
  int e = write_section(STORE_SCREENS_OFFSET, total, convert_screens, &job,
                        progress, progress_ctx);
  DPRINTF("store: screens, %lu bytes, result %d\n", (unsigned long)total, e);
  return e;
}

int store_write_tune(const x2_source_t *boot, uint32_t source_crc,
                     x2_progress_fn progress, void *progress_ctx) {
  uint32_t total = x2tune_image_size(boot);
  if (total == 0) return X2_ERR_FORMAT;
  if (total > STORE_GLOBAL_OFFSET - STORE_TUNE_OFFSET) return X2_ERR_RANGE;
  job_t job = {boot, 0, 0, source_crc, NULL};
  int e = write_section(STORE_TUNE_OFFSET, total, convert_tune, &job, progress,
                        progress_ctx);
  DPRINTF("store: title tune, %lu bytes, result %d\n", (unsigned long)total, e);
  return e;
}

int store_write_global(const x2_source_t *game, uint32_t source_crc,
                       x2_progress_fn progress, void *progress_ctx) {
  uint32_t total = x2global_image_size(game);
  if (total == 0) return X2_ERR_FORMAT;
  if (total > store_capacity() - STORE_GLOBAL_OFFSET) return X2_ERR_RANGE;
  job_t job = {game, 0, 0, source_crc, NULL};
  int e = write_section(STORE_GLOBAL_OFFSET, total, convert_global, &job,
                        progress, progress_ctx);
  DPRINTF("store: shared graphics, %lu bytes, result %d\n",
          (unsigned long)total, e);
  return e;
}
