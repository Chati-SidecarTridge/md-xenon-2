/**
 * File: tools/x2conv/x2conv.c
 * Description: The converter's core (rp/src/stx.c, x2game.c, lzhuf.c,
 *              crc32.c) on a computer: reads the two Pasti images of the
 *              original game and writes what the cartridge converts, as raw
 *              files, with their sizes and CRC-32s:
 *
 *   x2conv DISK1.stx DISK2.stx OUTDIR
 *
 * OUTDIR/boot.bin, game.bin, title.bin, level1.bin ... level5.bin; the same
 * bytes as tools/re/x2disk.py (title.bin: the screen and its palette).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "crc32.h"
#include "lzhuf.h"
#include "stx.h"
#include "x2game.h"

static int file_read(void *ctx, uint32_t offset, void *buf, uint32_t len) {
  FILE *f = (FILE *)ctx;
  if (fseek(f, (long)offset, SEEK_SET) != 0) return X2_ERR_READ;
  return fread(buf, 1, len, f) == len ? X2_OK : X2_ERR_READ;
}

static int file_write(void *ctx, uint32_t offset, const void *buf, uint32_t len) {
  FILE *f = (FILE *)ctx;
  if (fseek(f, (long)offset, SEEK_SET) != 0) return X2_ERR_WRITE;
  return fwrite(buf, 1, len, f) == len ? X2_OK : X2_ERR_WRITE;
}

static FILE *open_out(const char *dir, const char *name) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE *f = fopen(path, "w+b");
  if (!f) fprintf(stderr, "error: cannot write %s\n", path);
  return f;
}

static int matches = 0;

static int report(FILE *f, const char *name) {
  uint8_t buf[4096];
  uint32_t crc = 0, size = 0;
  size_t n;
  rewind(f);
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    crc = crc32_update(crc, buf, (uint32_t)n);
    size += (uint32_t)n;
  }
  int known = 0;
  for (int k = 0; k < X2_OUTPUTS; k++) {
    if (strcmp(x2_known[k].name, name) == 0) {
      known = x2_known[k].size == size && x2_known[k].crc32 == crc;
    }
  }
  matches += known;
  printf("%-11s %7u B  crc32 %08x%s\n", name, (unsigned)size, (unsigned)crc,
         known ? "" : "  (not the known release)");
  return 0;
}

static int fail(const char *what, int e) {
  fprintf(stderr, "error: %s (%d)\n", what, e);
  return 1;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: x2conv DISK1.stx DISK2.stx OUTDIR\n");
    return 2;
  }
  if (mkdir(argv[3], 0777) != 0 && errno != EEXIST) {
    fprintf(stderr, "error: cannot create %s\n", argv[3]);
    return 1;
  }
  FILE *f1 = fopen(argv[1], "rb"), *f2 = fopen(argv[2], "rb");
  if (!f1 || !f2) return fail("cannot open the disk images", X2_ERR_READ);
  x2_source_t s1 = {f1, file_read}, s2 = {f2, file_read};
  static stx_image_t d1, d2;
  int e;
  if ((e = stx_open(&d1, &s1)) != X2_OK) return fail("disk 1 is not a Pasti image", e);
  if ((e = stx_open(&d2, &s2)) != X2_OK) return fail("disk 2 is not a Pasti image", e);
  if ((e = x2_check_disks(&d1, &d2)) != X2_OK) {
    return fail("not the two Image Works disks (boot sector names X2 / X2B)", e);
  }

  FILE *boot = open_out(argv[3], "boot.bin");
  FILE *game = open_out(argv[3], "game.bin");
  FILE *title = open_out(argv[3], "title.bin");
  if (!boot || !game || !title) return 1;
  x2_sink_t boot_out = {boot, file_write}, game_out = {game, file_write},
            title_out = {title, file_write};
  if ((e = x2_extract_boot(&d1, &boot_out, NULL, NULL)) != X2_OK) return fail("boot", e);
  report(boot, "boot.bin");
  if ((e = x2_extract_game(&d1, &game_out, NULL, NULL)) != X2_OK) return fail("game", e);
  report(game, "game.bin");
  x2_source_t boot_in = {boot, file_read}, game_in = {game, file_read};
  if ((e = x2_make_title(&boot_in, &title_out)) != X2_OK) return fail("title", e);
  report(title, "title.bin");

  static lzhuf_t z;
  static uint8_t chunk[STX_SECTOR_BYTES];
  uint32_t pos = X2_LEVEL1_START;
  for (int n = 1; n <= X2_LEVELS; n++) {
    char name[32];
    snprintf(name, sizeof(name), "level%d.bin", n);
    FILE *lf = open_out(argv[3], name);
    if (!lf) return 1;
    x2_sink_t level_out = {lf, file_write};
    uint32_t start = pos, length = 0;
    if ((e = x2_extract_level(&game_in, &d2, &pos, &z, chunk, &level_out, &length,
                              NULL, NULL)) != X2_OK) {
      return fail(name, e);
    }
    report(lf, name);
    printf("            (virtual sector %u, offset %u)\n", (unsigned)(start / 512),
           (unsigned)(start % 512));
    fclose(lf);
  }
  printf(matches == X2_OUTPUTS ? "All outputs match the Image Works release.\n"
                               : "%d of %d outputs differ from the Image Works release.\n",
         X2_OUTPUTS - matches, X2_OUTPUTS);
  fclose(boot);
  fclose(game);
  fclose(title);
  fclose(f1);
  fclose(f2);
  return 0;
}
