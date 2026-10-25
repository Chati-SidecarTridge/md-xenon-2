/**
 * File: tune.c
 * Description: The title tune from the store (tune.h).
 */

#include "tune.h"

#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "store.h"
#include "x2game.h"
#include "x2set.h"

static const x2tune_header_t *tune;
static uint32_t section, pos;

static void fill(int8_t *buf, uint32_t samples) {
  const int8_t *data = (const int8_t *)tune + sizeof(*tune);
  while (samples) {
    const x2tune_section_t *s = &tune->section[section];
    uint32_t n = s->count - pos;
    if (n > samples) n = samples;
    memcpy(buf, data + s->offset + pos, n);
    buf += n;
    samples -= n;
    pos += n;
    if (pos == s->count) {
      pos = 0;
      section = section + 1 < tune->sections ? section + 1 : 0;
    }
  }
}

bool tune_start(const char *folder, x2_progress_fn progress,
                void *progress_ctx) {
  const x2_known_t *k = &x2_known[X2_OUT_BOOT];
  tune = store_tune(k->crc32);
  if (!tune) {
    x2set_window_t *w = malloc(sizeof(x2set_window_t));
    if (!w) return false;
    x2_source_t src = x2set_window(w, folder, "BOOT.X2", k->size);
    store_write_tune(&src, k->crc32, progress, progress_ctx);
    free(w);
    tune = store_tune(k->crc32);
    if (!tune) return false;
  }
  section = 0;
  pos = 0;
  audio_set_pcm_callback(fill, X2TUNE_RATE_HZ);
  audio_set_pcm_ym_pairs(tune->ym_pairs); /* as loud as the original's YM */
  return true;
}

void tune_stop(void) { audio_set_fill_callback(NULL); }
