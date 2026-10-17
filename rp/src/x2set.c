/**
 * File: x2set.c
 * Description: The converted set on the SD card (x2set.h).
 *
 * `folder`/SET holds BOOT.X2, GAME.X2, TITLE.X2, LEVEL1.X2 ... LEVEL5.X2
 * (the outputs of x2game.h, in that order) and SET.X2M, the manifest,
 * written last: a line "X2SET <version>", then one line per file, "<name>
 * <size> <crc32 hex>". A set is complete when the manifest has this
 * version and every file it names has its size. Making the set takes a few
 * seconds, so a set that is not complete is made again whole.
 */

#include "x2set.h"

#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "debug.h"
#include "ff.h"
#include "lzhuf.h"
#include "stx.h"
#include "x2game.h"

static const char *const kNames[X2_OUTPUTS] = {
    "BOOT.X2",   "GAME.X2",   "TITLE.X2",  "LEVEL1.X2",
    "LEVEL2.X2", "LEVEL3.X2", "LEVEL4.X2", "LEVEL5.X2"};
#define MANIFEST "SET.X2M"

/* The card's files the converter has open at once: the two disks, the
 * file read back (the boot program for the title, the game image for the
 * levels) and the one being written. Static: FIL holds a sector buffer. */
static FIL f_disk1, f_disk2, f_in, f_out;
static const x2_known_t *expected = x2_known;
static stx_image_t disk1, disk2;
static lzhuf_t lz;
static uint8_t chunk[STX_SECTOR_BYTES];

/* Paths hold a folder and a long file name (FatFs: up to 255 characters).
 * A path that does not fit is refused rather than cut short. */
#define PATH_MAX_LEN 300

static bool path_of(char *buf, size_t size, const char *folder,
                    const char *sub, const char *name) {
  int n = snprintf(buf, size, "%s%s%s%s%s", folder, sub[0] ? "/" : "", sub,
                   name[0] ? "/" : "", name);
  return n > 0 && (size_t)n < size;
}

static int fil_read(void *ctx, uint32_t offset, void *buf, uint32_t len) {
  FIL *f = (FIL *)ctx;
  UINT got = 0;
  if (f_lseek(f, offset) != FR_OK) return X2_ERR_READ;
  if (f_read(f, buf, len, &got) != FR_OK || got != len) return X2_ERR_READ;
  return X2_OK;
}

static int fil_write(void *ctx, uint32_t offset, const void *buf,
                     uint32_t len) {
  FIL *f = (FIL *)ctx;
  UINT put = 0;
  if (f_lseek(f, offset) != FR_OK) return X2_ERR_WRITE;
  if (f_write(f, buf, len, &put) != FR_OK || put != len) return X2_ERR_WRITE;
  return X2_OK;
}

/* ---- progress ---- */

typedef struct {
  x2set_ui_fn ui;
  void *ctx;
  x2set_progress_t p;
  uint32_t base;
} progress_t;

static uint32_t total_bytes(void) {
  uint32_t t = 0;
  for (int k = 0; k < X2_OUTPUTS; k++) t += expected[k].size;
  return t * 2; /* written, then read back for its CRC */
}

static void step(void *ctx, uint32_t done, uint32_t total) {
  (void)total;
  progress_t *pr = (progress_t *)ctx;
  pr->p.done = pr->base + done;
  if (pr->ui) pr->ui(&pr->p, pr->ctx);
}

/* ---- the manifest ---- */

static bool set_complete(const char *folder) {
  char path[PATH_MAX_LEN], line[64];
  path_of(path, sizeof(path), folder, "SET", MANIFEST);
  if (f_open(&f_in, path, FA_READ) != FR_OK) return false;
  int version = 0;
  bool ok = f_gets(line, sizeof(line), &f_in) &&
            sscanf(line, "X2SET %d", &version) == 1 &&
            version == X2SET_VERSION;
  int files = 0;
  while (ok && f_gets(line, sizeof(line), &f_in)) {
    char name[16];
    unsigned size = 0, crc = 0;
    if (sscanf(line, "%15s %u %x", name, &size, &crc) != 3) break;
    FILINFO fno;
    path_of(path, sizeof(path), folder, "SET", name);
    if (f_stat(path, &fno) != FR_OK || fno.fsize != size) ok = false;
    files++;
  }
  f_close(&f_in);
  return ok && files == X2_OUTPUTS;
}

static x2set_status_t write_manifest(const char *folder,
                                     const uint32_t sizes[X2_OUTPUTS],
                                     const uint32_t crcs[X2_OUTPUTS]) {
  char path[PATH_MAX_LEN], line[64];
  path_of(path, sizeof(path), folder, "SET", MANIFEST);
  if (f_open(&f_out, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
    return X2SET_ERROR;
  }
  bool ok = f_printf(&f_out, "X2SET %d\n", X2SET_VERSION) > 0;
  for (int k = 0; ok && k < X2_OUTPUTS; k++) {
    snprintf(line, sizeof(line), "%s %lu %08lx\n", kNames[k],
             (unsigned long)sizes[k], (unsigned long)crcs[k]);
    ok = f_puts(line, &f_out) >= 0;
  }
  return (f_close(&f_out) == FR_OK && ok) ? X2SET_READY : X2SET_ERROR;
}

/* ---- finding the disks ---- */

/* Open each file in `folder` that is a Pasti image and tell disk 1 from
 * disk 2 by the name in its boot sector. The Kixx re-release is one
 * double-sided disk with disk 1's name: it is skipped, and named when no
 * Image Works disk is found. */
static x2set_status_t find_disks(const char *folder) {
  DIR dir;
  static FILINFO fno;
  bool have1 = false, have2 = false, kixx = false;
  if (f_opendir(&dir, folder) != FR_OK) return X2SET_NO_CARD;
  while (!(have1 && have2) && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
    if (fno.fattrib & AM_DIR) continue;
    char path[PATH_MAX_LEN];
    if (!path_of(path, sizeof(path), folder, "", fno.fname)) continue;
    FIL *f = !have1 ? &f_disk1 : &f_disk2;
    if (f_open(f, path, FA_READ) != FR_OK) continue;
    x2_source_t src = {f, fil_read};
    stx_image_t *img = !have1 ? &disk1 : &disk2;
    uint8_t boot[STX_SECTOR_BYTES];
    if (stx_open(img, &src) == X2_OK && stx_read_logical(img, 0, boot) == X2_OK) {
      if (stx_sides(img) == 2 && memcmp(boot + 2, "X2", 2) == 0) {
        kixx = true;
        DPRINTF("x2set: %s is the Kixx release: skipped\n", path);
      } else if (!have1 && memcmp(boot + 2, "X2    ", 6) == 0) {
        have1 = true;
        DPRINTF("x2set: disk 1 is %s\n", path);
        continue;
      }
      if (!have2 && memcmp(boot + 2, "X2B   ", 6) == 0) {
        if (f == &f_disk1) { /* found disk 2 first: move it over */
          f_close(&f_disk1);
          if (f_open(&f_disk2, path, FA_READ) != FR_OK) continue;
          x2_source_t src2 = {&f_disk2, fil_read};
          if (stx_open(&disk2, &src2) != X2_OK) continue;
        }
        have2 = true;
        DPRINTF("x2set: disk 2 is %s\n", path);
        continue;
      }
    }
    f_close(f);
  }
  f_closedir(&dir);
  if (have1 && have2) return X2SET_READY;
  if (have1) f_close(&f_disk1);
  if (have2) f_close(&f_disk2);
  if (kixx && !have1 && !have2) return X2SET_KIXX;
  return have1 ? X2SET_NO_DISK2 : (have2 ? X2SET_NO_DISK1 : X2SET_NO_DISKS);
}

/* ---- making the set ---- */

static bool open_out(const char *folder, int k) {
  char path[PATH_MAX_LEN];
  path_of(path, sizeof(path), folder, "SET", kNames[k]);
  return f_open(&f_out, path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ) == FR_OK;
}

/* Read a just-written file back for its size and CRC-32. */
static bool file_crc(FIL *f, uint32_t *size, uint32_t *crc, progress_t *pr) {
  uint32_t c = 0, n = 0;
  UINT got;
  if (f_lseek(f, 0) != FR_OK) return false;
  do {
    if (f_read(f, chunk, sizeof(chunk), &got) != FR_OK) return false;
    c = crc32_update(c, chunk, got);
    n += got;
    step(pr, n, 0);
  } while (got == sizeof(chunk));
  pr->base += n;
  *size = n;
  *crc = c;
  return true;
}

static x2set_status_t convert(const char *folder, progress_t *pr) {
  uint32_t sizes[X2_OUTPUTS], crcs[X2_OUTPUTS];
  x2_sink_t out = {&f_out, fil_write};
  x2_source_t in = {&f_in, fil_read};
  char path[PATH_MAX_LEN];
  bool known = true;
  uint32_t pos = X2_LEVEL1_START;
  for (int k = 0; k < X2_OUTPUTS; k++) {
    pr->p.file = kNames[k];
    if (!open_out(folder, k)) return X2SET_ERROR;
    int e = X2_OK;
    uint32_t length = 0;
    if (k == X2_OUT_BOOT) {
      e = x2_extract_boot(&disk1, &out, step, pr);
    } else if (k == X2_OUT_GAME) {
      e = x2_extract_game(&disk1, &out, step, pr);
    } else if (k == X2_OUT_TITLE) {
      path_of(path, sizeof(path), folder, "SET", kNames[X2_OUT_BOOT]);
      e = f_open(&f_in, path, FA_READ) == FR_OK ? x2_make_title(&in, &out)
                                                 : X2_ERR_READ;
      f_close(&f_in);
    } else {
      path_of(path, sizeof(path), folder, "SET", kNames[X2_OUT_GAME]);
      e = f_open(&f_in, path, FA_READ) == FR_OK
              ? x2_extract_level(&in, &disk2, &pos, &lz, chunk, &out, &length,
                                 step, pr)
              : X2_ERR_READ;
      f_close(&f_in);
    }
    if (e != X2_OK) {
      f_close(&f_out);
      DPRINTF("x2set: %s failed (%d)\n", kNames[k], e);
      return e == X2_ERR_FORMAT ? X2SET_WRONG_DUMP : X2SET_ERROR;
    }
    pr->base += expected[k].size; /* written: advance by the expected size */
    if (!file_crc(&f_out, &sizes[k], &crcs[k], pr)) {
      f_close(&f_out);
      return X2SET_ERROR;
    }
    f_close(&f_out);
    if (sizes[k] != expected[k].size || crcs[k] != expected[k].crc32) {
      DPRINTF("x2set: %s: %lu B crc %08lx, not the known release\n", kNames[k],
              (unsigned long)sizes[k], (unsigned long)crcs[k]);
      known = false;
    }
  }
  if (!known) return X2SET_WRONG_DUMP;
  return write_manifest(folder, sizes, crcs);
}

x2set_status_t x2set_prepare(const char *folder, bool card_mounted,
                             x2set_ui_fn ui, void *ui_ctx) {
  if (!card_mounted) return X2SET_NO_CARD;
  if (set_complete(folder)) return X2SET_READY;
  x2set_status_t s = find_disks(folder);
  if (s != X2SET_READY) return s;
  char path[PATH_MAX_LEN];
  path_of(path, sizeof(path), folder, "SET", "");
  FRESULT r = f_mkdir(path);
  if (r != FR_OK && r != FR_EXIST) {
    f_close(&f_disk1);
    f_close(&f_disk2);
    return X2SET_ERROR;
  }
  progress_t pr = {ui, ui_ctx, {kNames[0], 0, total_bytes()}, 0};
  s = convert(folder, &pr);
  f_close(&f_disk1);
  f_close(&f_disk2);
  return s;
}

void x2set_set_expected(const x2_known_t *table) {
  expected = table ? table : x2_known;
}

void x2set_stop_card_work(void) {
  f_close(&f_out); /* flushes what was written */
  f_close(&f_in);
  f_close(&f_disk1);
  f_close(&f_disk2);
}

int x2set_read(const char *folder, const char *name, uint32_t offset,
               void *buf, uint32_t len) {
  char path[PATH_MAX_LEN];
  path_of(path, sizeof(path), folder, "SET", name);
  if (f_open(&f_in, path, FA_READ) != FR_OK) return X2_ERR_READ;
  int e = fil_read(&f_in, offset, buf, len);
  f_close(&f_in);
  return e;
}

static int window_read(void *ctx, uint32_t offset, void *out, uint32_t len) {
  x2set_window_t *w = ctx;
  uint8_t *o = out;
  if (offset + len > w->size) return X2_ERR_READ;
  while (len) {
    if (offset < w->base || offset >= w->base + w->len) {
      w->base = offset / X2SET_WINDOW_BYTES * X2SET_WINDOW_BYTES;
      w->len = w->size - w->base < X2SET_WINDOW_BYTES ? w->size - w->base
                                                      : X2SET_WINDOW_BYTES;
      if (x2set_read(w->folder, w->name, w->base, w->buf, w->len) != X2_OK) {
        w->len = 0;
        return X2_ERR_READ;
      }
    }
    uint32_t n = w->base + w->len - offset;
    if (n > len) n = len;
    memcpy(o, w->buf + (offset - w->base), n);
    o += n;
    offset += n;
    len -= n;
  }
  return X2_OK;
}

x2_source_t x2set_window(x2set_window_t *w, const char *folder,
                         const char *name, uint32_t size) {
  w->folder = folder;
  w->name = name;
  w->size = size;
  w->base = 0;
  w->len = 0;
  x2_source_t src = {w, window_read};
  return src;
}
