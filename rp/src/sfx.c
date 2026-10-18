/**
 * File: sfx.c
 * Description: The original sound driver translated to C (sfx.h).
 *
 * Every routine below is one of the driver's ($2CF42-$2E880), in the same
 * order of work, so the chip gets the same writes in the same order. The
 * driver's state stays where it keeps it: a window of its memory
 * ($2D000-$2E900: its channels, its copy of the chip's registers, its
 * effects' state) copied into the caller's RAM at the start; its songs,
 * patterns, instruments and effects are read in place from its bytes in
 * the store. Addresses below are the driver's; the driver reaches them from
 * a3 = $2C6C6, which its own tables count from. The hardware it touches:
 * the chip's registers (register 7 is read back, its port bits as TOS
 * leaves them), the MFP's monitor bit and the display's 50/60 Hz bit (a
 * colour monitor at 50 Hz: no tick skipped), Timer D for its sampled
 * effects (counted, not played: the game plays the sample itself). Checked
 * against the original driver run on the game image: every write, VBL for
 * VBL, of every song and effect; and against Hatari, every write of
 * 87,033 driver calls of the attract and level 1.
 */

#include "sfx.h"

#include <stdbool.h>
#include <string.h>

#include "x2global.h"

#define WINDOW_START 0x2D000u
#define WINDOW_SIZE SFX_WINDOW_SIZE
#define BASE 0x2C6C6u /* a3: the tables' offsets count from here */
#define YM_PORTS_OUT 0x40u /* register 7: port A output, as TOS sets it */

/* The driver's variables. */
#define V_MUSIC 0x2D0BCu      /* the music plays */
#define V_VOLUME 0x2D0BDu     /* the music's volume, 15 the loudest */
#define V_SPEED 0x2D0BEu      /* added to the step counter every tick */
#define V_SPEED_COPY 0x2D0BFu
#define V_FX 0x2D0C0u         /* 3 bytes: an effect on channel 0, 1, 2 */
#define V_HOLD 0x2D0C4u       /* 3 bytes: a tone channel the tick leaves */
#define V_RESUME 0x2D0C9u     /* the music may resume ($2CF6C) */
#define V_TRANSPOSE 0x2D0CAu
#define V_FADE_RATE 0x2D0CBu
#define V_FADE_COUNT 0x2D0CCu
#define V_PERIOD 0x2D0CEu     /* word: the last channel's period */
#define V_STEP 0x2D0D0u       /* a carry out of it steps the patterns */
#define V_NOTE 0x2D0D1u       /* the last note of a noise channel */
#define V_NOISE 0x2D0D2u      /* the noise period */
#define V_MIX 0x2D0D3u        /* the mixer bits the music channels take */
#define V_SKIP 0x2D0D4u       /* the ticks skipped at 60 Hz or mono */
#define V_SKIP_SUM 0x2D0D5u
#define REGS 0x2D7C4u         /* the chip's registers 0-10 as written */
#define FX_PERIOD 0x2D7D2u    /* 3 words: the effects' periods */
#define FX_NOISE 0x2D7D8u
#define FX_VOLUME 0x2D7DAu    /* 3 bytes */
#define CHANNELS 0x2D02Cu     /* 3 music channels of 48 bytes */
#define CHANNEL_SIZE 48u
#define COMMANDS 0x2D40Au     /* the patterns' commands $80-$98 */
#define COMMANDS_N 25
#define PERIODS 0x2D7E0u      /* a note's period, a word each */
#define ARPEGGIOS 0x2D8A0u
#define NO_ARPEGGIO 0x2D8C0u
#define SONGS 0x2D8EEu        /* 8 bytes a song */
#define INSTRUMENTS 0x2F080u
#define FX_TABLE 0x2E356u     /* an effect's 14-byte record */
#define FX_RECORD 14u
#define FX_ENVELOPES 0x2E646u
#define FX_STATE 0x2E6D2u     /* 3 effects' state, 26 bytes each */
#define FX_STATE_SIZE 26u
#define RANDOM 0x2E73Cu       /* two words */
#define SAMPLES 0x2F182u      /* the sampled effects' data */
#define SAMPLE_START 0x2F17Cu /* longs: a sample's offset */
#define SAMPLE_LENGTH 0x2F178u
#define SAMPLE_LOOPS 0x2E818u
#define SAMPLE_LOOP 0x2E816u
#define SAMPLE_POS 0x2E81Au
#define SAMPLE_END 0x2E81Eu
#define SAMPLE_REPEAT 0x2E822u

/* A music channel's bytes. */
#define C_FLAGS 0     /* 0 toggles, 1 noise, 2 slide, 3 bend, 5 envelope,
                         7 bend up */
#define C_NOISE_NOTE 1
#define C_POS 2       /* long: the pattern's next byte */
#define C_LIST 6      /* word: the pattern list's offset */
#define C_LIST_AT 10  /* word: where in it */
#define C_SLIDE 14    /* word */
#define C_ARP_START 16 /* long */
#define C_ARP 20      /* long */
#define C_SLIDE_BY 24
#define C_SLIDE_WAIT 25
#define C_ENV_SPEED 26
#define C_WAIT 27
#define C_LENGTH 28
#define C_NOTE 29
#define C_VOLUME 30
#define C_ENV_WAIT 31
#define C_ENV 32      /* long */
#define C_ENV_START 36 /* long */
#define C_ENV_VOLUME 40
#define C_VIB_DEPTH 41
#define C_VIB_SPEED 42
#define C_VIB 43
#define C_VIB_FLAGS 44
#define C_FX_WAIT 45  /* the channel waits for the effects to end */
#define C_SHIFT 46
#define C_MIX 47

/* An effect's bytes. */
#define E_TIME 0
#define E_REPEAT_WAIT 1
#define E_PERIOD 2    /* word */
#define E_SLIDE 4     /* word */
#define E_FLAGS 6     /* bit 3: no noise */
#define E_RANDOM 7
#define E_SLIDE_DIR 8
#define E_REPEATS 9
#define E_ENVELOPE 10
#define E_ENV_SPEED 11
#define E_HOLD 12
#define E_SLIDE_EVERY 13
#define E_WAIT 14
#define E_ENV_WAIT 15
#define E_SLIDE_WAIT 16
#define E_ENV_START 18 /* long */
#define E_ENV 22       /* long */

static const uint8_t *image;
static uint8_t *window;
static uint8_t ym[16];
static sfx_write_t *writes;
static int nwrites, capacity;
static int requests[3];
static int error;
static uint32_t samples_asked;

/* ---- the driver's memory ---- */

static uint8_t rb(uint32_t a) {
  a &= 0xFFFFFFu;
  if (a - WINDOW_START < WINDOW_SIZE) return window[a - WINDOW_START];
  if (a - X2G_DRIVER_START < X2G_DRIVER_SIZE) {
    return image[a - X2G_DRIVER_START];
  }
  return 0;
}
static uint16_t rw(uint32_t a) { return (uint16_t)(rb(a) << 8 | rb(a + 1)); }
static uint32_t rl(uint32_t a) { return (uint32_t)rw(a) << 16 | rw(a + 2); }
static void wb(uint32_t a, uint32_t v) {
  a &= 0xFFFFFFu;
  if (a - WINDOW_START < WINDOW_SIZE) window[a - WINDOW_START] = (uint8_t)v;
}
static void ww(uint32_t a, uint32_t v) {
  wb(a, v >> 8);
  wb(a + 1, v);
}
static void wl(uint32_t a, uint32_t v) {
  ww(a, v >> 16);
  ww(a + 2, v);
}
static void bset(uint32_t a, int bit) { wb(a, rb(a) | 1u << bit); }

/* A word as the 68000 extends it into an address register. */
static uint32_t sext16(uint32_t v) {
  return (uint32_t)(int32_t)(int16_t)(uint16_t)v;
}

/* A word table's offset for a signed byte (ext.w, add.w). */
static uint32_t byte_index(uint32_t byte) {
  return (uint32_t)((int32_t)(int8_t)byte * 2);
}

/* A table's word offset from a3, as an address. */
static uint32_t at(uint32_t table_word) {
  return BASE + sext16(rw(table_word));
}

static void ym_write(int reg, uint8_t v) {
  ym[reg] = v;
  if (writes && nwrites < capacity) {
    writes[nwrites].reg = (uint8_t)reg;
    writes[nwrites].value = v;
  }
  nwrites++;
}

/* ---- stopping ---- */

/* $2D396: the tone channels off, the volumes 0. */
static void silence(void) {
  ym_write(7, ym[7] | 0x3Fu);
  ym_write(8, 0);
  ym_write(9, 0);
  ym_write(10, 0);
}

/* $2E740 ($2CF88): the effects and the sample stopped. */
static void stop_effects(void) {
  wl(V_FX, 0);
  ww(REGS + 8, 0);
  wb(REGS + 10, 0);
  wb(REGS + 7, 63);
  silence();
}

/* $2D38A ($2CF5E): the music too. */
static void stop(void) {
  wb(V_MUSIC, 0);
  stop_effects();
}

/* ---- the effects ---- */

static const struct {
  uint32_t state, period, volume, random;
} fx_channel[3] = {{FX_STATE, FX_PERIOD, FX_VOLUME, RANDOM},
                   {FX_STATE + FX_STATE_SIZE, FX_PERIOD + 2, FX_VOLUME + 1,
                    RANDOM + 1},
                   {FX_STATE + 2 * FX_STATE_SIZE, FX_PERIOD + 4, FX_VOLUME + 2,
                    RANDOM + 2}};

/* $2E782: a sampled effect (bit 7): Timer D armed, the tick stops writing
 * the chip ($2D0C0 = -1) until the sample's end clears it. */
static void start_sample(uint32_t number) {
  stop_effects();
  uint32_t n = (uint32_t)(int32_t)(int8_t)number & 0xFFFFu;
  uint32_t off = sext16(n << 2);
  uint32_t pos = SAMPLES + rl(SAMPLE_START + off);
  wl(SAMPLE_POS, pos);
  wl(SAMPLE_REPEAT, pos);
  wl(SAMPLE_END, pos + rl(SAMPLE_LENGTH + off));
  samples_asked++; /* Timer D's data and control from $2E80C */
  wb(SAMPLE_LOOP, rb(SAMPLE_LOOPS + sext16(n)));
  wb(V_FX, 0xFF);
}

/* $2DF9E ($2CF7A): effect `effect` on channel `channel` (0-2). */
static void start_effect(uint32_t effect, uint32_t channel) {
  if (effect & 0x80u) {
    start_sample(effect & 0x7Fu);
    return;
  }
  if ((int8_t)rb(V_FX) < 0) stop_effects();
  int c = (channel & 0xFFu) == 0 ? 0 : (channel & 0xFFu) == 1 ? 1 : 2;
  uint32_t e = fx_channel[c].state;
  wb(V_FX + (uint32_t)c, 0);
  uint32_t rec = at(FX_TABLE + byte_index(effect));
  for (uint32_t i = 0; i < FX_RECORD; i++) wb(e + i, rb(rec + i));
  wb(e + E_WAIT, rb(e + E_REPEAT_WAIT));
  ww(fx_channel[c].period, rw(e + E_PERIOD));
  if (!(rb(e + E_FLAGS) & 8u)) wb(FX_NOISE, rb(e + E_PERIOD + 1));
  wb(e + E_ENV_WAIT, rb(e + E_ENV_SPEED));
  wb(e + E_SLIDE_WAIT, rb(e + E_SLIDE_EVERY));
  uint32_t env = at(FX_ENVELOPES + byte_index(rb(e + E_ENVELOPE)));
  wl(e + E_ENV_START, env);
  wl(e + E_ENV, env);
  wb(fx_channel[c].volume, rb(env));
  wb(V_FX + (uint32_t)c, 1);
}

/* $2E720: the driver's random generator, 32 bits shifted left once a
 * tick. */
static void random_step(void) {
  uint32_t x = (((rb(RANDOM) & 0x48u) + 0x38u) >> 6) & 1u;
  uint32_t lo = rw(RANDOM + 2), hi = rw(RANDOM);
  ww(RANDOM + 2, lo << 1 | x);
  ww(RANDOM, hi << 1 | lo >> 15);
}

/* $2E128, $2E1E2, $2E29C: an effect's tick. */
static void effect_tick(int c) {
  uint32_t e = fx_channel[c].state, p = fx_channel[c].period;
  if (rb(e + E_TIME) == 0 && rb(e + E_HOLD) == 0) {
    wb(V_FX + (uint32_t)c, 0);
    wb(REGS + 8 + (uint32_t)c, 0);
    return;
  }
  wb(e + E_TIME, rb(e + E_TIME) - 1u);
  if (rb(e + E_WAIT) == 0) {
    if (rb(e + E_HOLD) == 0 && rb(e + E_REPEATS) == 0) goto envelope;
    wb(e + E_REPEATS, rb(e + E_REPEATS) - 1u);
    wb(e + E_WAIT, rb(e + E_REPEAT_WAIT));
    uint32_t d0 = rb(e + E_RANDOM) ? rb(fx_channel[c].random) : 0;
    uint32_t lo = rb(e + E_PERIOD + 1) + d0;
    wb(p + 1, lo);
    wb(p, rb(e + E_PERIOD) + d0 + (lo >> 8));
  }
  wb(e + E_WAIT, rb(e + E_WAIT) - 1u);
  uint32_t every = rb(e + E_SLIDE_EVERY);
  if (every) {
    uint32_t w = (rb(e + E_SLIDE_WAIT) - 1u) & 0xFFu;
    wb(e + E_SLIDE_WAIT, w);
    if (w) goto noise;
    wb(e + E_SLIDE_WAIT, every);
  }
  if (rb(e + E_SLIDE_DIR)) {
    if (rb(e + E_SLIDE_DIR) & 0x80u) {
      ww(p, rw(p) + rw(e + E_SLIDE));
    } else {
      ww(p, rw(p) - rw(e + E_SLIDE));
    }
  }
noise:
  if (!(rb(e + E_FLAGS) & 8u)) wb(FX_NOISE, rb(p + 1));
envelope:;
  uint32_t w = (rb(e + E_ENV_WAIT) - 1u) & 0xFFu;
  wb(e + E_ENV_WAIT, w);
  if (w) return;
  wb(e + E_ENV_WAIT, rb(e + E_ENV_SPEED));
  uint32_t a = rl(e + E_ENV);
  uint8_t v = rb(a++);
  if (v & 0x80u) {
    if (v != 0x80u) return;
    a = rl(e + E_ENV_START);
    v = rb(a++);
  }
  wl(e + E_ENV, a);
  wb(fx_channel[c].volume, v);
}

/* $2E100. */
static void effects_tick(void) {
  random_step();
  if ((int8_t)rb(V_FX) < 0) return;
  if (rb(V_FX)) effect_tick(0);
  if (rb(V_FX + 1)) effect_tick(1);
  if (rb(V_FX + 2)) effect_tick(2);
}

/* ---- the music ---- */

/* $2D578: a channel's pattern stepped; false when a command ended the
 * song ($2D486: the tick ends with it). */
static bool channel_step(uint32_t ch) {
  wb(ch + C_WAIT, rb(ch + C_WAIT) - 1u);
  if (rb(ch + C_WAIT)) { /* $2D5FA: the bend */
    uint8_t f = rb(ch + C_FLAGS);
    if (f & 8u) wb(ch + C_NOTE, rb(ch + C_NOTE) + ((f & 0x80u) ? 1u : 0xFFu));
    return true;
  }
  wb(ch + C_FLAGS, 0);
  uint32_t a1 = rl(ch + C_POS);
  for (;;) {
    uint32_t d0 = rb(a1++);
    if (!(d0 & 0x80u)) { /* a note */
      wb(ch + C_NOTE, d0);
      if (rb(ch + C_NOISE_NOTE)) wb(V_NOTE, d0);
      uint32_t env = rl(ch + C_ENV_START);
      wl(ch + C_ENV, env);
      wb(ch + C_ENV_VOLUME, rb(env));
      wb(ch + C_VOLUME, rb(env));
      wb(ch + C_ENV_WAIT, rb(ch + C_ENV_SPEED));
      bset(ch + C_FLAGS, 5);
      break;
    }
    if (d0 >= 0xE0u) { /* a length */
      wb(ch + C_LENGTH, ((d0 + 32u) & 0xFFu) + 1u);
      continue;
    }
    if (d0 >= 0xD0u) { /* an instrument */
      uint32_t env = at(INSTRUMENTS + ((d0 + 48u) & 0xFFu) * 2u);
      wl(ch + C_ENV_START, env);
      wb(ch + C_ENV_SPEED, rb(env - 1u));
      continue;
    }
    if (d0 >= 0xB8u) { /* an arpeggio */
      uint32_t arp = at(ARPEGGIOS + ((d0 + 64u) & 0xFFu) * 2u);
      wl(ch + C_ARP, arp);
      wl(ch + C_ARP_START, arp);
      continue;
    }
    uint32_t cmd = d0 & 0x7Fu;
    if (cmd >= COMMANDS_N) { /* the driver jumps into its code */
      error = 1;
      return true;
    }
    switch (at(COMMANDS + cmd * 2u)) {
      case 0x2D43C: /* $98: the sample's flag off */
        wb(V_FX, 0);
        continue;
      case 0x2D444: /* $97: an effect on channel 0 */
        start_effect(rb(a1++), 0);
        continue;
      case 0x2D450: /* $96: the volume */
        wb(V_VOLUME, rb(a1++));
        continue;
      case 0x2D458: /* $95: a fade */
        wb(V_FADE_RATE, rb(a1++));
        wb(V_FADE_COUNT, rb(V_FADE_RATE));
        continue;
      case 0x2D466: /* $94: the speed */
        wb(V_SPEED, rb(a1++));
        wb(V_SPEED_COPY, rb(V_SPEED));
        continue;
      case 0x2D474: /* $93: another pattern list */
        wb(ch + C_LIST, rb(a1++));
        wb(ch + C_LIST + 1, rb(a1++));
        ww(ch + C_LIST_AT, 0);
        continue;
      case 0x2D486: /* $8E: the song's end */
        wb(V_RESUME, 0);
        stop();
        return false;
      case 0x2D490: { /* $87: the list's next pattern */
        uint32_t i = rw(ch + C_LIST_AT);
        uint32_t list = sext16(rw(ch + C_LIST));
        uint32_t a2 = list + sext16(i);
        i = (i + 2u) & 0xFFFFu;
        if (rw(BASE + sext16(a2)) == 0) {
          a2 = list;
          i = 2;
        }
        a1 = at(BASE + sext16(a2));
        ww(ch + C_LIST_AT, i);
        continue;
      }
      case 0x2D4B6: { /* $8B: the tone on, as the channel's mix */
        uint32_t m = rb(ch + C_MIX), mix = rb(V_MIX);
        wb(V_MIX, (((m & 7u) ^ mix) & m) ^ mix);
        wb(ch + C_NOISE_NOTE, 0xFF);
        continue;
      }
      case 0x2D4D6: { /* $8A: the noise on */
        uint32_t m = rb(ch + C_MIX), mix = rb(V_MIX);
        wb(V_MIX, (((m & 0x38u) ^ mix) & m) ^ mix);
        wb(ch + C_NOISE_NOTE, 0);
        continue;
      }
      case 0x2D4F6: /* $8C: the channel's bits off */
        wb(V_MIX, rb(V_MIX) & (~rb(ch + C_MIX) & rb(V_MIX)));
        wb(ch + C_NOISE_NOTE, 0xFF);
        continue;
      case 0x2D50E: /* $84: a slide */
        ww(ch + C_SLIDE, 0);
        bset(ch + C_FLAGS, 2);
        wb(ch + C_SLIDE_BY, rb(a1++));
        wb(ch + C_SLIDE_WAIT, rb(a1++));
        continue;
      case 0x2D522: /* $89: transpose */
        wb(V_TRANSPOSE, rb(a1++));
        continue;
      case 0x2D528: /* $92: the channel's shift */
        wb(ch + C_SHIFT, rb(a1++));
        continue;
      case 0x2D52E: /* $86: bend up */
        bset(ch + C_FLAGS, 7);
        bset(ch + C_FLAGS, 3);
        continue;
      case 0x2D534: /* $85: bend down */
        bset(ch + C_FLAGS, 3);
        continue;
      case 0x2D53C: /* $88: vibrato */
        wb(ch + C_VIB_SPEED, rb(a1++));
        wb(ch + C_VIB_DEPTH, rb(a1));
        wb(ch + C_VIB, rb(a1++));
        wb(ch + C_VIB_FLAGS, 64);
        continue;
      case 0x2D548: /* $82: vibrato on, the depth kept */
        wb(ch + C_VIB_FLAGS, 64);
        continue;
      case 0x2D550: /* $81: vibrato off */
        wb(ch + C_VIB_FLAGS, 0);
        continue;
      case 0x2D556: /* $83, $8D */
        bset(ch + C_FLAGS, 1);
        continue;
      case 0x2D55E: /* $80: a rest */
        wb(ch + C_VOLUME, 0);
        break;
      case 0x2D564: /* $8F: a tie */
        bset(ch + C_FLAGS, 5);
        break;
      case 0x2D56C: /* $90: wait for the effects */
        wb(ch + C_FX_WAIT, 0xFF);
        continue;
      case 0x2D572: /* $91 */
        wb(ch + C_FX_WAIT, 0);
        continue;
      default:
        error = 1;
        return true;
    }
    break;
  }
  /* $2D5BA */
  wb(ch + C_WAIT, rb(ch + C_LENGTH));
  wl(ch + C_POS, a1);
  if (rb(ch + C_FX_WAIT)) {
    for (int c = 0; c < 3; c++) {
      if (rb(V_FX + (uint32_t)c) &&
          !(rb(fx_channel[c].state + E_FLAGS) & 8u)) {
        return true;
      }
    }
    wb(ch + C_FX_WAIT, 0xFF);
  }
  return true;
}

/* $2D67A: a channel's period and volume for this tick. */
static void channel_out(uint32_t ch, uint32_t *period, uint8_t *volume) {
  uint32_t d7 = rb(ch + C_FLAGS);
  if (d7 & 0x20u) {
    uint32_t w = rb(ch + C_ENV_WAIT);
    wb(ch + C_ENV_WAIT, w - 1u);
    if (w == 0) {
      wb(ch + C_ENV_WAIT, rb(ch + C_ENV_SPEED));
      uint32_t a2 = rl(ch + C_ENV);
      uint8_t v = rb(a2 + 1);
      if (!(v & 0x80u)) {
        wl(ch + C_ENV, a2 + 1);
        wb(ch + C_ENV_VOLUME, v);
      }
    }
    wb(ch + C_VOLUME, rb(ch + C_ENV_VOLUME));
  }
  uint32_t note =
      (uint32_t)rb(ch + C_NOTE) + rb(V_TRANSPOSE) + rb(ch + C_SHIFT);
  uint32_t a1 = rl(ch + C_ARP);
  uint32_t d1 = rb(a1++);
  if (d1 & 0x80u) {
    d1 &= 0x7Fu;
    a1 = rl(ch + C_ARP_START);
  }
  wl(ch + C_ARP, a1);
  note = ((note + d1) * 2u) & 0xFFu;
  uint32_t d0 = rw(PERIODS + note);
  uint32_t vf = rb(ch + C_VIB_FLAGS);
  if (vf & 0x40u) {
    uint32_t d5 = note;
    uint32_t d4 = (rb(ch + C_VIB_DEPTH) << 1) & 0xFFu;
    uint32_t v = rb(ch + C_VIB);
    if (!((vf & 0x80u) && (d7 & 1u))) {
      if (!(vf & 0x20u)) {
        if (v < rb(ch + C_VIB_SPEED)) {
          bset(ch + C_VIB_FLAGS, 5);
          v = 0;
        } else {
          v -= rb(ch + C_VIB_SPEED);
        }
      } else {
        v = (v + rb(ch + C_VIB_SPEED)) & 0xFFu;
        if (v >= d4) {
          wb(ch + C_VIB_FLAGS, rb(ch + C_VIB_FLAGS) & ~0x20u);
          v = d4;
        }
      }
      wb(ch + C_VIB, v);
    }
    d4 >>= 1;
    uint32_t off = v - d4; /* d1.w: the byte's borrow into its high byte */
    off = (v < d4) ? (off & 0xFFu) - 0x100u : off & 0xFFu;
    off &= 0xFFFFu;
    d5 = d5 + 0xA0u;
    if (d5 < 0x100u) {
      do {
        off = (off << 1) & 0xFFFFu;
        d5 = (d5 & 0xFFu) + 0x18u;
      } while (d5 < 0x100u);
    }
    d0 = (d0 + off) & 0xFFFFu;
  }
  d7 ^= 1u;
  wb(ch + C_FLAGS, d7);
  if (d7 & 4u) {
    uint32_t w = (rb(ch + C_SLIDE_WAIT) - 1u) & 0xFFu;
    if (w == 0) {
      uint32_t by = rb(ch + C_SLIDE_BY);
      if (by & 0x80u) by += 0xFF00u;
      by = (by + rw(ch + C_SLIDE)) & 0xFFFFu;
      ww(ch + C_SLIDE, by);
      d0 = (d0 + by) & 0xFFFFu;
    } else {
      wb(ch + C_SLIDE_WAIT, w);
    }
  }
  uint32_t d3 = rb(V_MIX);
  if (((d7 ^ 0xFFu) & 3u) == 0) {
    wb(V_NOISE, rb(V_NOTE) ^ 8u);
    d3 = 7;
  }
  uint32_t mixer = rb(REGS + 7);
  d3 = ((d3 ^ mixer) & rb(ch + C_MIX)) ^ mixer;
  if (rb(ch + C_FX_WAIT) & 0x80u) {
    wb(ch + C_FX_WAIT, rb(ch + C_FX_WAIT) & 0x7Fu);
    d3 &= ~(rb(ch + C_MIX) & 0x38u);
    wb(V_NOISE, 1);
  }
  wb(REGS + 7, d3);
  *period = d0;
  *volume = rb(ch + C_VOLUME);
}

/* $2D488: the song over; the tick ends. */
static void end_song(void) {
  wb(V_RESUME, 0);
  stop();
}

/* $2D0DA ($2CF50): one tick. */
static void tick(void) {
  /* A colour monitor at 50 Hz: none skipped (72 on a mono monitor, 43 at
   * 60 Hz). */
  wb(V_SKIP, 0);
  if (rb(V_MUSIC) == 0 && rl(V_FX) == 0) return;
  uint32_t sum = rb(V_SKIP_SUM) + rb(V_SKIP);
  wb(V_SKIP_SUM, sum);
  if (sum > 0xFFu) return;
  effects_tick();
  if (rb(V_MUSIC)) {
    if (rb(V_FADE_RATE)) {
      if (rb(V_VOLUME) == 0) {
        end_song();
        return;
      }
      wb(V_FADE_COUNT, rb(V_FADE_COUNT) - 1u);
      if (rb(V_FADE_COUNT) == 0) {
        wb(V_VOLUME, rb(V_VOLUME) - 1u);
        if (rb(V_VOLUME) == 0) {
          end_song();
          return;
        }
        wb(V_FADE_COUNT, rb(V_FADE_RATE));
      }
    }
    wb(V_NOISE, rb(V_NOTE));
    uint32_t step = rb(V_STEP) + rb(V_SPEED);
    wb(V_STEP, step);
    if (step > 0xFFu) {
      for (uint32_t c = 0; c < 3; c++) {
        if (!channel_step(CHANNELS + c * CHANNEL_SIZE)) return;
      }
    }
    wb(V_VOLUME, rb(V_VOLUME) & 15u);
    for (uint32_t c = 0; c < 3; c++) {
      uint32_t period;
      uint8_t volume;
      channel_out(CHANNELS + c * CHANNEL_SIZE, &period, &volume);
      ww(V_PERIOD, period);
      wb(REGS + 2 * c, period);
      wb(REGS + 2 * c + 1, rb(V_PERIOD));
      uint32_t quiet = rb(V_VOLUME) ^ 15u;
      wb(REGS + 8 + c, volume < quiet ? 0 : volume - quiet);
    }
    wb(REGS + 6, rb(V_NOISE));
  }
  /* the effects over the music; a sample playing: nothing written */
  uint8_t a = rb(V_FX);
  if (a) {
    if (a & 0x80u) return;
    wb(REGS + 0, rb(FX_PERIOD + 1));
    wb(REGS + 1, rb(FX_PERIOD));
    uint8_t f = rb(FX_STATE + E_FLAGS);
    if (!(f & 8u)) wb(REGS + 6, rb(FX_NOISE));
    wb(REGS + 7, (rb(REGS + 7) | 9u) & f);
    wb(REGS + 8, rb(FX_VOLUME));
  }
  if (rb(V_FX + 1)) {
    wb(REGS + 2, rb(FX_PERIOD + 3));
    wb(REGS + 3, rb(FX_PERIOD + 2));
    uint8_t f = rb(FX_STATE + FX_STATE_SIZE + E_FLAGS);
    if (!(f & 8u)) wb(REGS + 6, rb(FX_NOISE));
    wb(REGS + 7, (rb(REGS + 7) | 0x12u) & (uint8_t)(f << 1 | f >> 7));
    wb(REGS + 9, rb(FX_VOLUME + 1));
  }
  if (rb(V_FX + 2)) {
    wb(REGS + 4, rb(FX_PERIOD + 5));
    wb(REGS + 5, rb(FX_PERIOD + 4));
    uint8_t f = rb(FX_STATE + 2 * FX_STATE_SIZE + E_FLAGS);
    if (!(f & 8u)) wb(REGS + 6, rb(FX_NOISE));
    wb(REGS + 7, (rb(REGS + 7) | 0x24u) & (uint8_t)(f << 2 | f >> 6));
    wb(REGS + 10, rb(FX_VOLUME + 2));
  }
  wb(REGS + 7, rb(REGS + 7) & 63u);
  /* $2D2A0: to the chip */
  uint8_t mask = 0;
  if (rl(V_HOLD) == 0) ym_write(6, rb(REGS + 6));
  for (uint32_t c = 0; c < 3; c++) {
    if (rb(V_HOLD + c)) continue;
    ym_write((int)(2 * c), rb(REGS + 2 * c));
    ym_write((int)(2 * c + 1), rb(REGS + 2 * c + 1));
    ym_write((int)(8 + c), rb(REGS + 8 + c));
    mask |= (uint8_t)(9u << c);
  }
  uint8_t old = ym[7];
  ym_write(7, (uint8_t)(((rb(REGS + 7) ^ old) & mask) ^ old));
}

/* $2CFA0 ($2CF42): song `song` from its start. */
static void start_song(uint32_t song) {
  stop();
  wb(V_TRANSPOSE, 0);
  uint32_t d0 = ((uint32_t)(int32_t)(int8_t)song * 8u) & 0xFFFFu;
  wb(V_SPEED, rb(SONGS + 1 + sext16(d0)));
  wb(V_SPEED_COPY, rb(V_SPEED));
  uint32_t ch = CHANNELS;
  for (int c = 0; c < 3; c++) {
    wb(ch + C_WAIT, 1);
    wb(ch + C_FLAGS, 0);
    wb(ch + C_VIB_FLAGS, 0);
    wb(ch + C_FX_WAIT, 0);
    wb(ch + C_SHIFT, 0);
    wl(ch + C_ARP_START, NO_ARPEGGIO);
    wl(ch + C_ARP, NO_ARPEGGIO);
    uint32_t list = rw(SONGS + 2 + sext16(d0));
    ww(ch + C_LIST, list);
    ww(ch + C_LIST_AT, 2);
    wl(ch + C_POS, at(BASE + sext16(list)));
    ch += CHANNEL_SIZE;
    d0 = (d0 + 2u) & 0xFFFFu;
  }
  wl(V_HOLD, 0);
  wb(V_VOLUME, 15);
  wb(V_FADE_RATE, 0);
  wb(V_STEP, 0xFF);
  wb(V_RESUME, 0xFF);
  wb(V_MUSIC, 0xFF);
}

/* ---- the game's side ---- */

/* Run one entry; its writes go to `out`, at most `cap` of them. */
static void begin(sfx_write_t *out, int cap) {
  writes = out;
  capacity = cap;
  nwrites = 0;
}

static int end(int cap) {
  writes = NULL;
  return nwrites < cap ? nwrites : cap;
}

void sfx_init(const uint8_t *driver, uint8_t *ram) {
  image = driver;
  window = ram;
  memcpy(window, driver + (WINDOW_START - X2G_DRIVER_START), WINDOW_SIZE);
  memset(ym, 0, sizeof(ym));
  ym[7] = YM_PORTS_OUT;
  requests[0] = requests[1] = requests[2] = -1;
  error = 0;
  samples_asked = 0;
}

int sfx_song(int song, int tempo, sfx_write_t *out) {
  if (!image || error) return 0;
  begin(out, SFX_WRITES_MAX);
  start_song((uint32_t)song);
  int n = end(SFX_WRITES_MAX);
  wb(V_VOLUME, (uint32_t)tempo);
  return n;
}

int sfx_stop(sfx_write_t *out) {
  if (!image || error) return 0;
  begin(out, SFX_WRITES_MAX);
  stop();
  return end(SFX_WRITES_MAX);
}

/* $2D3F8 ($2CF96). */
void sfx_fade(void) {
  if (!image || error) return;
  wb(V_FADE_COUNT, 8);
  wb(V_FADE_RATE, 8);
}

int sfx_stop_effect(sfx_write_t *out) {
  if (!image || error) return 0;
  begin(out, SFX_WRITES_MAX);
  stop_effects();
  return end(SFX_WRITES_MAX);
}

uint8_t sfx_peek(uint32_t addr) {
  return addr - WINDOW_START < WINDOW_SIZE ? window[addr - WINDOW_START] : 0;
}

void sfx_request(int effect, int channel) {
  if (channel == SFX_CHANNEL_A) requests[0] = effect;
  if (channel == SFX_CHANNEL_B) requests[1] = effect;
  if (channel == SFX_CHANNEL_C) requests[2] = effect;
}

void sfx_request_if_free(int effect, int channel) {
  int i = channel == SFX_CHANNEL_A ? 0 : 1;
  if (requests[i] < 0) sfx_request(effect, channel);
}

int sfx_vbl(sfx_write_t *out) {
  if (!image || error) return 0;
  static const int channel[3] = {SFX_CHANNEL_A, SFX_CHANNEL_B, SFX_CHANNEL_C};
  int n = 0;
  for (int i = 0; i < 3; i++) {
    if (requests[i] < 0) continue;
    begin(out + n, SFX_WRITES_MAX - n);
    start_effect((uint32_t)requests[i], (uint32_t)channel[i]);
    n += end(SFX_WRITES_MAX - n);
    requests[i] = -1;
  }
  begin(out + n, SFX_WRITES_MAX - n);
  tick();
  return n + end(SFX_WRITES_MAX - n);
}

int sfx_error(void) { return error; }

uint32_t sfx_samples_asked(void) { return samples_asked; }
