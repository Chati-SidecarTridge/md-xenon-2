/**
 * File: x2app.c
 * Description: The app's screens (x2app.h). Everything the screens show of
 *              the game comes from the user's own disks, converted on the
 *              card (x2set.h): nothing of it is in the firmware.
 */

#include "x2app.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "debug.h"
#include "fb.h"
#include "fb_blit.h"
#include "fb_chunked.h"
#include "fb_font.h"
#include "ikbd.h"
#include "palette.h"
#include "play.h"
#include "pico/time.h"
#include "select.h"
#include "tune.h"
#include "x2game.h"
#include "x2set.h"

extern const struct FB_FONT font8x8; /* fb.c */

#define WHITE 0  /* the template's default palette: 0 white, 15 black */
#define BLACK 15
#define SCAN_SPACE 0x39
#define SCAN_Y 0x15

typedef enum { SCREEN_MESSAGE, SCREEN_TITLE, SCREEN_GAME } app_screen_t;

static app_screen_t screen;
static const char *folder_name;
static uint32_t last_publish_us;

/* ---- text ---- */

static void centred(int y, const char *text) {
  font_move((unsigned)((FB_CHUNKED_W - (int)strlen(text) * 8) / 2), (unsigned)y);
  font_print(text);
}

static void message(const char *const *lines, int count) {
  palette_init();
  fb_chunked_clear(BLACK);
  font_set_font(&font8x8);
  font_set_color(WHITE);
  int y = (FB_CHUNKED_H - count * 12) / 2;
  for (int i = 0; i < count; i++, y += 12) centred(y, lines[i]);
}

/* ---- the conversion's progress ---- */

static void progress(const x2set_progress_t *p, void *ctx) {
  (void)ctx;
  /* Keep the plumbing going: the ST's bytes, the session, SELECT. */
  fb_pump_rom3();
  ikbd_pump();
  select_poll();
  uint32_t now = time_us_32();
  if (now - last_publish_us < 40000) return;
  last_publish_us = now;
  char line[41];
  unsigned pct = p->total ? (unsigned)((uint64_t)p->done * 100 / p->total) : 0;
  if (pct > 100) pct = 100;
  snprintf(line, sizeof(line), "%s  %u%%", p->file, pct);
  const char *lines[] = {"XENON 2 MEGABLAST", "", "CONVERTING THE DISKS", "", line};
  message(lines, 5);
  fb_fill_rect(60, 150, 200, 6, WHITE);
  fb_fill_rect(61, 151, 198, 4, BLACK);
  fb_fill_rect(61, 151, (int)(198 * pct / 100), 4, WHITE);
  fb_publish();
}

/* ---- something made ready in the store: `ctx` names it ---- */

static void level_progress(void *ctx, uint32_t done, uint32_t total) {
  fb_pump_rom3();
  ikbd_pump();
  select_poll();
  uint32_t now = time_us_32();
  if (now - last_publish_us < 40000) return;
  last_publish_us = now;
  unsigned pct = total ? (unsigned)((uint64_t)done * 100 / total) : 0;
  if (pct > 100) pct = 100;
  const char *lines[] = {"XENON 2 MEGABLAST", "", (const char *)ctx};
  message(lines, 3);
  fb_fill_rect(60, 150, 200, 6, WHITE);
  fb_fill_rect(61, 151, 198, 4, BLACK);
  fb_fill_rect(61, 151, (int)(198 * pct / 100), 4, WHITE);
  fb_publish();
}

/* ---- the title: an ST screen and its palette, from TITLE.X2 ---- */

static bool show_title(void) {
  uint8_t line[160];
  for (int y = 0; y < FB_CHUNKED_H; y++) {
    if (x2set_read(folder_name, "TITLE.X2", (uint32_t)y * 160u, line, 160) != X2_OK) {
      return false;
    }
    uint8_t *row = &fb_chunked_buffer[y * FB_CHUNKED_W];
    for (int g = 0; g < 20; g++) {
      const uint8_t *w = &line[g * 8];
      uint16_t p0 = (uint16_t)(w[0] << 8 | w[1]), p1 = (uint16_t)(w[2] << 8 | w[3]);
      uint16_t p2 = (uint16_t)(w[4] << 8 | w[5]), p3 = (uint16_t)(w[6] << 8 | w[7]);
      for (int b = 0; b < 16; b++) {
        uint16_t m = (uint16_t)(0x8000u >> b);
        row[g * 16 + b] = (uint8_t)(((p0 & m) ? 1 : 0) | ((p1 & m) ? 2 : 0) |
                                    ((p2 & m) ? 4 : 0) | ((p3 & m) ? 8 : 0));
      }
    }
  }
  uint8_t pal[32];
  if (x2set_read(folder_name, "TITLE.X2", X2_SCREEN_SIZE, pal, 32) != X2_OK) return false;
  uint16_t entries[PALETTE_ENTRIES];
  for (int i = 0; i < PALETTE_ENTRIES; i++) entries[i] = (uint16_t)(pal[2 * i] << 8 | pal[2 * i + 1]);
  palette_set(entries);
  return true;
}

/* ---- the start ---- */

void x2app_start(const char *folder, bool card_mounted) {
  folder_name = folder;
  ikbd_set_esc_auto_exit(true);
  play_sound_reset(); /* the ST starts its ring from the first word */
  ikbd_set_input_mode(IKBD_INPUT_JOYSTICKS); /* the keyboard works too */
  const char *checking[] = {"XENON 2 MEGABLAST", "", "CHECKING THE SD CARD"};
  message(checking, 3);
  fb_publish();
  last_publish_us = time_us_32();
  uint32_t t0 = time_us_32();
  x2set_status_t s = x2set_prepare(folder, card_mounted, progress, NULL);
  DPRINTF("x2app: set status %d after %lu ms\n", (int)s,
          (unsigned long)((time_us_32() - t0) / 1000));
  if (s == X2SET_READY) {
    last_publish_us = 0;
    tune_start(folder, level_progress, (void *)"PREPARING THE TUNE");
  }
  if (s == X2SET_READY && show_title()) {
    screen = SCREEN_TITLE;
    return;
  }
  tune_stop();
  screen = SCREEN_MESSAGE;
  char where[41];
  snprintf(where, sizeof(where), "INTO THE FOLDER %s", folder);
  switch (s) {
    case X2SET_NO_CARD: {
      const char *l[] = {"XENON 2 MEGABLAST", "", "NO SD CARD", "",
                         "INSERT THE CARD, THEN SWITCH", "THE ST OFF AND ON", "",
                         "ESC: BACK TO GEM"};
      message(l, 8);
      break;
    }
    case X2SET_NO_DISKS:
    case X2SET_NO_DISK1:
    case X2SET_NO_DISK2: {
      const char *what = s == X2SET_NO_DISK1   ? "DISK 1 IS MISSING"
                         : s == X2SET_NO_DISK2 ? "DISK 2 IS MISSING"
                                               : "THE ORIGINAL DISKS ARE MISSING";
      const char *l[] = {"XENON 2 MEGABLAST", "", what, "",
                         "COPY THE TWO DISK IMAGES OF THE",
                         "IMAGE WORKS RELEASE (.STX)", where,
                         "ON THE SD CARD (SEE THE README)", "",
                         "ESC: BACK TO GEM"};
      message(l, 10);
      break;
    }
    case X2SET_KIXX: {
      const char *l[] = {"XENON 2 MEGABLAST", "",
                         "THIS IS THE KIXX RELEASE (ONE DISK)",
                         "THE APP NEEDS THE TWO DISK IMAGES",
                         "OF THE IMAGE WORKS RELEASE (.STX)", where,
                         "ON THE SD CARD (SEE THE README)", "",
                         "ESC: BACK TO GEM"};
      message(l, 9);
      break;
    }
    case X2SET_WRONG_DUMP: {
      const char *l[] = {"XENON 2 MEGABLAST", "",
                         "THESE DISKS ARE NOT THE",
                         "IMAGE WORKS RELEASE THE APP KNOWS", "",
                         "USE THE TWO-DISK IMAGE WORKS DUMP",
                         "(SEE THE README)", "", "ESC: BACK TO GEM"};
      message(l, 9);
      break;
    }
    default: {
      const char *l[] = {"XENON 2 MEGABLAST", "", "THE SD CARD FAILED",
                         "WHILE CONVERTING", "", "ESC: BACK TO GEM"};
      message(l, 6);
      break;
    }
  }
}

/* ---- per frame ---- */

static void failed(const char *what) {
  screen = SCREEN_MESSAGE;
  ikbd_set_esc_auto_exit(true);
  const char *l[] = {"XENON 2 MEGABLAST", "", what, "", "ESC: BACK TO GEM"};
  message(l, 5);
}

/* The game's attract, then SELECT: Esc there goes back to GEM. */
static void attract(void) {
  last_publish_us = 0;
  ikbd_set_esc_auto_exit(true);
  if (play_attract(folder_name, level_progress, (void *)"PREPARING THE GAME")) {
    screen = SCREEN_GAME;
    return;
  }
  failed("THE GAME COULD NOT BE PREPARED");
}

void x2app_frame(void) {
  if (screen == SCREEN_GAME) {
    play_state_t st = play_frame();
    if (st == PLAY_START) {
      last_publish_us = 0;
      if (play_start(folder_name, 1, level_progress, (void *)"PREPARING LEVEL 1")) {
        ikbd_set_esc_auto_exit(false); /* Esc in play: back to the attract */
        return;
      }
      failed("LEVEL 1 COULD NOT BE PREPARED");
      return;
    }
    if (st == PLAY_WON) {
      static char label[24];
      snprintf(label, sizeof(label), "PREPARING LEVEL %lu",
               (unsigned long)(play_level() % 5 + 1));
      last_publish_us = 0;
      if (play_next(folder_name, level_progress, label)) return;
      st = PLAY_OVER;
    }
    if (st == PLAY_OVER) attract();
    return;
  }
  bool toggle = false;
  ikbd_key_event_t k;
  while (ikbd_pop_key(&k)) {
    if (k.is_press && k.scancode == SCAN_SPACE) toggle = true;
#if defined(_DEBUG) && (_DEBUG != 0)
    if (k.is_press && k.scancode == SCAN_Y) { /* the YM on an STE too */
      static bool ym;
      ym = !ym;
      audio_prefer_ym(ym); /* from the ST's next reset */
    }
#endif
  }
  ikbd_joystick_t j;
  ikbd_read_joystick(1, &j);
  if (j.pressed & IKBD_JOY_FIRE) toggle = true;
  if (!toggle || screen != SCREEN_TITLE) return;
  tune_stop(); /* the title's tune; the attract has its own music */
  attract();
}
