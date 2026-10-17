/**
 * File: sfx.c
 * Description: The original sound driver run by the interpreter (sfx.h).
 *
 * The driver's memory: its bytes from the store are read in place, but for
 * a window the driver writes ($2D000-$2E900: its channels, its shadow of
 * the chip's registers, the bytes it changes in its own code), copied into
 * the caller's RAM at the start, and a small stack. The hardware it touches: the sound
 * chip's select and data registers (reads give back the selected register,
 * as the YM does; the port bits of register 7 start as TOS leaves them),
 * the MFP's monitor bit (a colour monitor) and the display's 50/60 Hz bit
 * (50 Hz: the driver skips ticks at 60). Its sampled effects arm Timer D
 * (the MFP's registers and the vector at $110): counted, not played yet.
 * Checked against Hatari: from the game image's bytes, every write of
 * 87,033 driver calls of the attract and level 1 is the original's.
 */

#include "sfx.h"

#include <string.h>

#include "m68k.h"
#include "x2global.h"

#define ENTRY_SONG 0x2CF42u
#define ENTRY_TICK 0x2CF50u
#define ENTRY_STOP 0x2CF5Eu
#define ENTRY_EFFECT 0x2CF7Au
#define ENTRY_FADE 0x2CF96u
#define TEMPO_BYTE 0x2D0BDu
#define WINDOW_START 0x2D000u
#define WINDOW_SIZE SFX_WINDOW_SIZE
#define STACK_TOP 0x7FF00u
#define STACK_SIZE 256u
#define MAX_STEPS 100000u
#define YM_SELECT 0xFF8800u
#define YM_SELECT_MIRROR 0xFF8804u
#define YM_DATA 0xFF8802u
#define YM_DATA_MIRROR 0xFF8806u
#define MFP_GPIP 0xFFFA01u
#define MFP_TIMER_D_DATA 0xFFFA25u
#define SHIFTER_SYNC 0xFF820Au
#define MONITOR_COLOUR 0x80u
#define SYNC_50HZ 0x02u
#define YM_PORTS_OUT 0x40u /* register 7: port A output, as TOS sets it */

static const uint8_t *image;
static uint8_t *window;
static uint8_t stack[STACK_SIZE];
static uint8_t ym[16];
static uint8_t selected;
static sfx_write_t *writes;
static int nwrites, capacity;
static int requests[2];
static int error;
static uint32_t samples_asked;
static m68k_t cpu;

static uint8_t read8(void *ctx, uint32_t a) {
  (void)ctx;
  if (a - WINDOW_START < WINDOW_SIZE) return window[a - WINDOW_START];
  if (a - X2G_DRIVER_START < X2G_DRIVER_SIZE) return image[a - X2G_DRIVER_START];
  if (a - (STACK_TOP - STACK_SIZE) < STACK_SIZE) {
    return stack[a - (STACK_TOP - STACK_SIZE)];
  }
  if (a == YM_SELECT || a == YM_SELECT_MIRROR) return ym[selected];
  if (a == MFP_GPIP) return MONITOR_COLOUR;
  if (a == SHIFTER_SYNC) return SYNC_50HZ;
  return 0;
}

static void write8(void *ctx, uint32_t a, uint8_t v) {
  (void)ctx;
  if (a - WINDOW_START < WINDOW_SIZE) {
    window[a - WINDOW_START] = v;
  } else if (a - (STACK_TOP - STACK_SIZE) < STACK_SIZE) {
    stack[a - (STACK_TOP - STACK_SIZE)] = v;
  } else if (a == YM_SELECT || a == YM_SELECT_MIRROR) {
    selected = v & 15u;
  } else if (a == YM_DATA || a == YM_DATA_MIRROR) {
    ym[selected] = v;
    if (writes && nwrites < capacity) {
      writes[nwrites].reg = selected;
      writes[nwrites].value = v;
    }
    nwrites++;
  } else if (a == MFP_TIMER_D_DATA) {
    samples_asked++;
  }
}

/* Run one entry; its writes go to `out`, at most `cap` of them. */
static int call(uint32_t entry, uint32_t d0, uint32_t d1, sfx_write_t *out,
                int cap) {
  if (!image || error) return 0;
  writes = out;
  capacity = cap;
  nwrites = 0;
  cpu.d[0] = d0;
  cpu.d[1] = d1;
  cpu.a[7] = STACK_TOP;
  int e = m68k_call(&cpu, entry, MAX_STEPS);
  if (e != M68K_OK) error = e;
  writes = NULL;
  return nwrites < cap ? nwrites : cap;
}

void sfx_init(const uint8_t *driver, uint8_t *ram) {
  image = driver;
  window = ram;
  memcpy(window, driver + (WINDOW_START - X2G_DRIVER_START), WINDOW_SIZE);
  memset(ym, 0, sizeof(ym));
  ym[7] = YM_PORTS_OUT;
  selected = 0;
  requests[0] = requests[1] = -1;
  error = 0;
  samples_asked = 0;
  memset(&cpu, 0, sizeof(cpu));
  cpu.read8 = read8;
  cpu.write8 = write8;
}

int sfx_song(int song, int tempo, sfx_write_t *out) {
  int n = call(ENTRY_SONG, (uint32_t)song, 0, out, SFX_WRITES_MAX);
  window[TEMPO_BYTE - WINDOW_START] = (uint8_t)tempo;
  return n;
}

int sfx_stop(sfx_write_t *out) {
  return call(ENTRY_STOP, 0, 0, out, SFX_WRITES_MAX);
}

void sfx_fade(void) { call(ENTRY_FADE, 0, 0, NULL, 0); }

void sfx_request(int effect, int channel) {
  if (channel == SFX_CHANNEL_A) requests[0] = effect;
  if (channel == SFX_CHANNEL_B) requests[1] = effect;
}

void sfx_request_if_free(int effect, int channel) {
  int i = channel == SFX_CHANNEL_A ? 0 : 1;
  if (requests[i] < 0) sfx_request(effect, channel);
}

int sfx_vbl(sfx_write_t *out) {
  int n = 0;
  static const uint32_t channel[2] = {SFX_CHANNEL_A, SFX_CHANNEL_B};
  for (int i = 0; i < 2; i++) {
    if (requests[i] < 0) continue;
    n += call(ENTRY_EFFECT, (uint32_t)requests[i], channel[i], out + n,
              SFX_WRITES_MAX - n);
    requests[i] = -1;
  }
  return n + call(ENTRY_TICK, 0, 0, out + n, SFX_WRITES_MAX - n);
}

int sfx_error(void) { return error; }

uint32_t sfx_samples_asked(void) { return samples_asked; }
