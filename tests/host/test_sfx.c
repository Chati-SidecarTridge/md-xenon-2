// units: rp/src/sfx.c
/* The sound driver on synthetic data (a song, two effects and their
 * envelopes laid out as the driver keeps them, no game data): a song's
 * first tick writes the three channels' periods and volumes, then the
 * mixer; the volume the game pokes after the song's start scales them;
 * an effect takes its channel over, its envelope stepping and looping, and
 * its flag ($2D0C2 on the game's channel 2) clears when its time is out; a
 * later request replaces an earlier one, a request "if free" does not; the
 * song's end command and a fade stop the music; a sampled effect stops the
 * tick's writes; a command the driver does not have is an error. */
#include <string.h>

#include "sfx.h"
#include "test.h"
#include "x2global.h"

#define BASE 0x2C6C6u
#define SONGS 0x2D8EEu
#define PERIODS 0x2D7E0u
#define NO_ARPEGGIO 0x2D8C0u
#define INSTRUMENTS 0x2F080u
#define FX_TABLE 0x2E356u
#define FX_ENVELOPES 0x2E646u
#define COMMANDS 0x2D40Au /* the commands' handlers, from a3 */
#define MUSIC 0x2D0BCu
#define FX 0x2D0C0u

static uint8_t drv[X2G_DRIVER_SIZE];
static uint8_t window[SFX_WINDOW_SIZE];
static sfx_write_t w[SFX_WRITES_MAX];

static void put(uint32_t addr, const uint8_t *bytes, int n) {
  memcpy(&drv[addr - X2G_DRIVER_START], bytes, (size_t)n);
}
static void put16(uint32_t addr, uint32_t v) {
  drv[addr - X2G_DRIVER_START] = (uint8_t)(v >> 8);
  drv[addr - X2G_DRIVER_START + 1] = (uint8_t)v;
}
/* A song whose three channels play the pattern list at `list`. */
static void song(int n, int speed, uint32_t list) {
  uint32_t s = SONGS + 8u * (uint32_t)n;
  drv[s + 1 - X2G_DRIVER_START] = (uint8_t)speed;
  for (uint32_t c = 0; c < 3; c++) put16(s + 2 + 2 * c, list - BASE);
}
static void list(uint32_t at, uint32_t pattern) {
  put16(at, pattern - BASE);
  put16(at + 2, 0);
}
static int value(int n, int reg) { /* the last write of `reg` among n */
  int v = -1;
  for (int i = 0; i < n; i++) {
    if (w[i].reg == reg) v = w[i].value;
  }
  return v;
}

int main(void) {
  /* the two commands used: $87 the next pattern, $8E the song's end */
  put16(COMMANDS + 2 * 7, 0x2D490 - BASE);
  put16(COMMANDS + 2 * 14, 0x2D486 - BASE);
  /* song 0: instrument 0, a length of 4, note 12, the next pattern */
  static const uint8_t pattern[] = {0xD0, 0xE3, 12, 0x87};
  put(0x2E100, pattern, sizeof(pattern));
  list(0x2E000, 0x2E100);
  song(0, 0x80, 0x2E000);
  put16(INSTRUMENTS, 0x2E200 - BASE);
  static const uint8_t env[] = {1, 13, 0x80}; /* its speed, then volumes */
  put(0x2E1FF, env, sizeof(env));
  put16(PERIODS + 24, 0x0123);
  static const uint8_t no_arp[] = {0, 0x80};
  put(NO_ARPEGGIO, no_arp, sizeof(no_arp));
  /* song 1 ends at once; song 2 has a command the driver does not have */
  static const uint8_t end[] = {0x8E}, bad[] = {0x99};
  put(0x2E110, end, 1);
  list(0x2E010, 0x2E110);
  song(1, 0x80, 0x2E010);
  put(0x2E120, bad, 1);
  list(0x2E020, 0x2E120);
  song(2, 0x80, 0x2E020);
  /* effects 0 and 1: 6 ticks, periods $200 and $300, no noise, envelope 0 */
  static const uint8_t fx0[14] = {6, 0, 2, 0, 0, 0, 8, 0, 0, 0, 0, 1, 0, 0};
  static const uint8_t fx1[14] = {6, 0, 3, 0, 0, 0, 8, 0, 0, 0, 0, 1, 0, 0};
  put(0x2E400, fx0, 14);
  put(0x2E410, fx1, 14);
  put16(FX_TABLE, 0x2E400 - BASE);
  put16(FX_TABLE + 2, 0x2E410 - BASE);
  static const uint8_t fx_env[] = {10, 9, 0x80};
  put(0x2E500, fx_env, sizeof(fx_env));
  put16(FX_ENVELOPES, 0x2E500 - BASE);

  /* nothing playing: no tick */
  sfx_init(drv, window);
  CHECK_EQ(sfx_vbl(w), 0);

  /* a stop: the tone channels off (the port bits kept), the volumes 0 */
  int n = sfx_stop(w);
  CHECK_EQ(n, 4);
  CHECK_EQ(w[0].reg, 7);
  CHECK_EQ(w[0].value, 0x7F);
  CHECK_EQ(w[3].reg, 10);
  CHECK_EQ(w[3].value, 0);

  /* the song: its start stops first; its first tick writes noise, the three
   * channels (period, volume) and the mixer */
  n = sfx_song(0, 15, w);
  CHECK_EQ(n, 4);
  CHECK_EQ(sfx_peek(MUSIC), 0xFF);
  n = sfx_vbl(w);
  CHECK_EQ(n, 11);
  CHECK_EQ(w[0].reg, 6);
  CHECK_EQ(w[1].reg, 0);
  CHECK_EQ(w[1].value, 0x23);
  CHECK_EQ(w[2].reg, 1);
  CHECK_EQ(w[2].value, 0x01);
  CHECK_EQ(w[3].reg, 8);
  CHECK_EQ(w[3].value, 13);
  CHECK_EQ(value(n, 10), 13);
  CHECK_EQ(w[10].reg, 7);
  /* the game's volume 12 takes 3 off */
  sfx_song(0, SFX_TEMPO_PLAY, w);
  n = sfx_vbl(w);
  CHECK_EQ(value(n, 8), 10);

  /* an effect on the game's channel 2 takes the driver's channel 2 over */
  sfx_init(drv, window);
  sfx_request(0, SFX_CHANNEL_A);
  n = sfx_vbl(w);
  CHECK_EQ(n, 11);
  CHECK_EQ(sfx_peek(FX + 2), 1);
  CHECK_EQ(value(n, 4), 0x00);
  CHECK_EQ(value(n, 5), 0x02);
  CHECK_EQ(value(n, 10), 10);
  CHECK_EQ(value(n, 7), 0x60); /* tone C on, noise C off, ports kept */
  n = sfx_vbl(w);
  CHECK_EQ(value(n, 10), 9);
  n = sfx_vbl(w);
  CHECK_EQ(value(n, 10), 10); /* the envelope loops */
  for (int i = 0; i < 3; i++) sfx_vbl(w);
  CHECK_EQ(sfx_peek(FX + 2), 1);
  sfx_vbl(w); /* its time out: the flag clears, the channel's volume 0 */
  CHECK_EQ(sfx_peek(FX + 2), 0);

  /* a later request replaces; "if free" does not */
  sfx_init(drv, window);
  sfx_request(0, SFX_CHANNEL_A);
  sfx_request(1, SFX_CHANNEL_A);
  n = sfx_vbl(w);
  CHECK_EQ(value(n, 5), 0x03);
  sfx_init(drv, window);
  sfx_request(0, SFX_CHANNEL_A);
  sfx_request_if_free(1, SFX_CHANNEL_A);
  n = sfx_vbl(w);
  CHECK_EQ(value(n, 5), 0x02);
  /* the game's channel 1 is the driver's 1 */
  sfx_init(drv, window);
  sfx_request(1, SFX_CHANNEL_B);
  n = sfx_vbl(w);
  CHECK_EQ(sfx_peek(FX + 1), 1);
  CHECK_EQ(value(n, 3), 0x03);

  /* the song's end command: the music stops in the tick */
  sfx_init(drv, window);
  sfx_song(1, 15, w);
  n = sfx_vbl(w);
  CHECK_EQ(n, 4);
  CHECK_EQ(sfx_peek(MUSIC), 0);
  CHECK_EQ(sfx_vbl(w), 0);

  /* a fade: a step of volume every 8 ticks, the music over at 0 */
  sfx_init(drv, window);
  sfx_song(0, 15, w);
  sfx_fade();
  for (int i = 0; i < 119; i++) sfx_vbl(w);
  CHECK_EQ(sfx_peek(MUSIC), 0xFF);
  sfx_vbl(w);
  CHECK_EQ(sfx_peek(MUSIC), 0);

  /* a sampled effect: counted, its flag -1, the tick writes nothing more */
  sfx_init(drv, window);
  sfx_song(0, 15, w);
  sfx_request(128, SFX_CHANNEL_A);
  n = sfx_vbl(w);
  CHECK_EQ(n, 4); /* the effects stopped first */
  CHECK_EQ(sfx_samples_asked(), 1);
  CHECK_EQ(sfx_peek(FX), 0xFF);
  CHECK_EQ(sfx_vbl(w), 0);
  CHECK_EQ(sfx_stop_effect(w), 4);
  CHECK_EQ(sfx_peek(FX), 0);

  /* a command the driver does not have stops it */
  sfx_init(drv, window);
  sfx_song(2, 15, w);
  sfx_vbl(w);
  CHECK(sfx_error() != 0);
  CHECK_EQ(sfx_vbl(w), 0);
  TEST_END();
}
