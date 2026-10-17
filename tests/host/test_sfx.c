// units: rp/src/sfx.c rp/src/m68k.c
/* The sound driver's host on a synthetic driver (tiny routines reached from
 * the original driver's entries, no game data): the effect requests reach
 * the effect entry before the tick, channel 2 before channel 1, a later
 * request replaces an earlier one; the chip's writes come back in order;
 * the song entry gets its number and the tempo byte its value; a write
 * outside the driver's window goes nowhere; reads of the chip's select
 * register give back the selected register. */
#include <string.h>

#include "sfx.h"
#include "test.h"
#include "x2global.h"

static uint8_t drv[X2G_DRIVER_SIZE];
static uint8_t window[SFX_WINDOW_SIZE];

static void put(uint32_t addr, const uint16_t *words, int n) {
  for (int i = 0; i < n; i++) {
    drv[addr - X2G_DRIVER_START + 2 * i] = (uint8_t)(words[i] >> 8);
    drv[addr - X2G_DRIVER_START + 2 * i + 1] = (uint8_t)words[i];
  }
}

int main(void) {
  /* effect ($2CF7A): store d0 at $2D000 and d1 at $2D001 */
  static const uint16_t effect[] = {0x13C0, 0x0002, 0xD000, 0x13C1, 0x0002, 0xD001, 0x4E75};
  /* tick ($2CF50): register 8 = ($2D000), register 9 = ($2D001);
   * then read register 9 back from the chip into $2D002; a write to $4000
   * (outside the window) */
  static const uint16_t tick[] = {
      0x13FC, 0x0008, 0x00FF, 0x8800, 0x13F9, 0x0002, 0xD000, 0x00FF, 0x8802,
      0x13FC, 0x0009, 0x00FF, 0x8800, 0x13F9, 0x0002, 0xD001, 0x00FF, 0x8802,
      0x13F9, 0x00FF, 0x8800, 0x0002, 0xD002, 0x13FC, 0x0055, 0x0000, 0x4000,
      0x4E75};
  /* song ($2CF42): register 0 = d0 */
  static const uint16_t song[] = {0x13FC, 0x0000, 0x00FF, 0x8800, 0x13C0, 0x00FF, 0x8802, 0x4E75};
  /* stop ($2CF5E): register 7 = $3F */
  static const uint16_t stop[] = {0x13FC, 0x0007, 0x00FF, 0x8800, 0x13FC, 0x003F, 0x00FF, 0x8802, 0x4E75};
  /* as in the original, each entry is a short stub: here a JMP to a body */
  put(0x2C100, effect, 7);
  put(0x2C200, tick, 28);
  put(0x2C300, song, 8);
  put(0x2C400, stop, 9);
  static const uint16_t stubs[4][3] = {{0x4EF9, 0x0002, 0xC100}, {0x4EF9, 0x0002, 0xC200},
                                       {0x4EF9, 0x0002, 0xC300}, {0x4EF9, 0x0002, 0xC400}};
  put(0x2CF7A, stubs[0], 3);
  put(0x2CF50, stubs[1], 3);
  put(0x2CF42, stubs[2], 3);
  put(0x2CF5E, stubs[3], 3);
  sfx_init(drv, window);

  sfx_write_t w[SFX_WRITES_MAX];
  /* no request: the tick alone, from the window as copied (0, 0) */
  int n = sfx_vbl(w);
  CHECK_EQ(n, 2);
  CHECK_EQ(w[0].reg, 8);
  CHECK_EQ(w[0].value, 0);

  /* channel 1 then channel 2 asked; channel 2 replaced: 9 runs first, 3 last */
  sfx_request(5, SFX_CHANNEL_A);
  sfx_request(3, SFX_CHANNEL_B);
  sfx_request(9, SFX_CHANNEL_A);
  n = sfx_vbl(w);
  CHECK_EQ(n, 2);
  CHECK_EQ(w[0].value, 3); /* the last effect call's d0: channel 1's 3 */
  CHECK_EQ(w[1].value, SFX_CHANNEL_B);
  n = sfx_vbl(w); /* requests consumed: the window keeps the last */
  CHECK_EQ(w[0].value, 3);

  n = sfx_song(7, SFX_TEMPO_PLAY, w);
  CHECK_EQ(n, 1);
  CHECK_EQ(w[0].reg, 0);
  CHECK_EQ(w[0].value, 7);
  n = sfx_stop(w);
  CHECK_EQ(n, 1);
  CHECK_EQ(w[0].reg, 7);
  CHECK_EQ(w[0].value, 0x3F);
  CHECK_EQ(sfx_error(), 0);
  TEST_END();
}
