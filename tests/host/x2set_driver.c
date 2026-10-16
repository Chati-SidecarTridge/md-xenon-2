/* Runs rp/src/x2set.c over a host folder (ff_stdio.c), for test_x2set.py:
 *
 *   x2set_driver FOLDER [EXPECTED]
 *
 * EXPECTED, if given, holds the expected size and CRC-32 of each converted
 * file, one "size crc" line each, in x2game.h's order (synthetic disks do
 * not convert to the known release). Prints the status's name and how many
 * times the conversion reported progress. */
#include <stdio.h>
#include <stdlib.h>

#include "x2set.h"

static void count(const x2set_progress_t *p, void *ctx) {
  (void)p;
  (*(unsigned *)ctx)++;
}

int main(int argc, char **argv) {
  static const char *const names[] = {"READY",    "NO_CARD",    "NO_DISKS", "NO_DISK1",
                                      "NO_DISK2", "WRONG_DUMP", "KIXX",     "ERROR"};
  static x2_known_t table[X2_OUTPUTS];
  if (argc < 2) return 2;
  if (argc > 2) {
    FILE *f = fopen(argv[2], "r");
    if (!f) return 2;
    for (int k = 0; k < X2_OUTPUTS; k++) {
      unsigned size, crc;
      if (fscanf(f, "%u %x", &size, &crc) != 2) return 2;
      table[k] = (x2_known_t){"", size, crc};
    }
    fclose(f);
    x2set_set_expected(table);
  }
  unsigned calls = 0;
  x2set_status_t s = x2set_prepare(argv[1], 1, count, &calls);
  printf("%s %u\n", names[s], calls);
  return 0;
}
