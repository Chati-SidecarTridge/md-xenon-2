/**
 * File: x2app.c
 * Description: The app's screens (x2app.h). Everything the screens show of
 *              the game comes from the user's own disks, converted on the
 *              card (x2set.h): nothing of it is in the firmware.
 */

#include "x2app.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "fb.h"
#include "fb_blit.h"
#include "fb_chunked.h"
#include "fb_font.h"
#include "ikbd.h"
#include "palette.h"
#include "options.h"
#include "play.h"
#include "qrcodegen.h"
#include "pico/time.h"
#include "select.h"
#include "st_session.h"
#include "tune.h"
#include "x2game.h"
#include "x2set.h"

extern const struct FB_FONT font8x8; /* fb.c */

#define WHITE 0  /* the template's default palette: 0 white, 15 black */
#define BLACK 15
#define SCAN_SPACE 0x39
#define SCAN_O 0x18 /* on the title: the options */
#define SCAN_X 0x2D /* outside play: back to Booster */

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

static char label[24]; /* what a progress screen is making */

/* A message about the disks, with the page that says how to set the game
 * up as a QR code under it, black on white. */
#define SETUP_URL \
  "https://docs.sidecartridge.com/sidecartridge-multidevice/microfirmwares/xenon-2-megablast-50fps"
#define QR_VERSION_MAX 6 /* 41 modules: the URL at the lowest correction */
#define QR_MODULE_PX 2
#define QR_QUIET 2 /* modules of white around it */

static void message_qr(const char *const *lines, int count) {
  palette_init();
  fb_chunked_clear(BLACK);
  font_set_font(&font8x8);
  font_set_color(WHITE);
  for (int i = 0; i < count; i++) centred(4 + 11 * i, lines[i]);
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
  uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
  if (!qrcodegen_encodeText(SETUP_URL, temp, qr, qrcodegen_Ecc_LOW, 1, QR_VERSION_MAX,
                            qrcodegen_Mask_AUTO, true)) {
    return;
  }
  int size = qrcodegen_getSize(qr);
  int side = (size + 2 * QR_QUIET) * QR_MODULE_PX;
  int x0 = (FB_CHUNKED_W - side) / 2, y0 = FB_CHUNKED_H - side - 4;
  fb_fill_rect(x0, y0, side, side, WHITE);
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (qrcodegen_getModule(qr, x, y)) {
        fb_fill_rect(x0 + (QR_QUIET + x) * QR_MODULE_PX, y0 + (QR_QUIET + y) * QR_MODULE_PX,
                     QR_MODULE_PX, QR_MODULE_PX, BLACK);
      }
    }
  }
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

/* The keys under the logo, in the picture's brightest colour. */
static void title_keys(const uint16_t *p) {
  int best = 0, light = -1;
  for (int i = 0; i < PALETTE_ENTRIES; i++) {
    int l = (p[i] >> 8 & 7) * 3 + (p[i] >> 4 & 7) * 6 + (p[i] & 7);
    if (l > light) {
      light = l;
      best = i;
    }
  }
  font_set_font(&font8x8);
  font_set_color((uint8_t)best);
  centred(160, "FIRE OR SPACE: START   O: OPTIONS");
  centred(176, "ESC: BACK TO GEM   X: BACK TO BOOSTER");
}

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
  title_keys(entries);
  return true;
}

/* ---- the start ---- */

void x2app_start(const char *folder, bool card_mounted) {
  folder_name = folder;
  ikbd_set_esc_auto_exit(true);
  play_sound_reset(); /* the ST starts its ring from the first word */
  play_load_options(); /* before the ST's hello: its sound path */
  ikbd_set_input_mode(IKBD_INPUT_JOYSTICKS); /* the keyboard works too */
  const char *checking[] = {"XENON 2 MEGABLAST", "", "CHECKING THE SD CARD"};
  message(checking, 3);
  fb_publish();
  last_publish_us = time_us_32();
  uint32_t t0 = time_us_32();
  x2set_status_t s = x2set_prepare(folder, card_mounted, progress, NULL);
  DPRINTF("x2app: set status %d after %lu ms\n", (int)s,
          (unsigned long)((time_us_32() - t0) / 1000));
  /* Each level converted once onto the card, then only copied; level 1
   * last, so it is the one left in the store for the first game. */
  for (uint32_t l = 5; s == X2SET_READY && l >= 1; l--) {
    snprintf(label, sizeof(label), "CONVERTING LEVEL %lu OF 5", (unsigned long)(6 - l));
    last_publish_us = 0;
    play_prepare_level(folder, l, level_progress, label);
  }
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
                         "ESC: BACK TO GEM   X: BOOSTER"};
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
                         "ON THE SD CARD: SCAN THE CODE FOR HOW",
                         "ESC: BACK TO GEM   X: BOOSTER"};
      message_qr(l, 9);
      break;
    }
    case X2SET_KIXX: {
      const char *l[] = {"XENON 2 MEGABLAST", "",
                         "THIS IS THE KIXX RELEASE (ONE DISK)",
                         "THE APP NEEDS THE TWO DISK IMAGES",
                         "OF THE IMAGE WORKS RELEASE (.STX)", where,
                         "ON THE SD CARD: SCAN THE CODE FOR HOW",
                         "ESC: BACK TO GEM   X: BOOSTER"};
      message_qr(l, 8);
      break;
    }
    case X2SET_WRONG_DUMP: {
      const char *l[] = {"XENON 2 MEGABLAST", "",
                         "THESE DISKS ARE NOT THE",
                         "IMAGE WORKS RELEASE THE APP KNOWS", "",
                         "USE THE TWO-DISK IMAGE WORKS DUMP:",
                         "SCAN THE CODE FOR HOW",
                         "ESC: BACK TO GEM   X: BOOSTER"};
      message_qr(l, 8);
      break;
    }
    default: {
      const char *l[] = {"XENON 2 MEGABLAST", "", "THE SD CARD FAILED",
                         "WHILE CONVERTING", "", "ESC: BACK TO GEM   X: BOOSTER"};
      message(l, 6);
      break;
    }
  }
}

/* ---- per frame ---- */

static void failed(const char *what) {
  screen = SCREEN_MESSAGE;
  ikbd_set_esc_auto_exit(true);
  const char *l[] = {"XENON 2 MEGABLAST", "", what, "", "ESC: BACK TO GEM   X: BOOSTER"};
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
    if (st == PLAY_START) { /* level 1, or the options' start level */
      int level = options_start_level(&options);
      snprintf(label, sizeof(label), "PREPARING LEVEL %d", level);
      last_publish_us = 0;
      if (play_start(folder_name, (uint32_t)level, level_progress, (void *)label)) {
        ikbd_set_esc_auto_exit(false); /* Esc in play: back to the attract */
        return;
      }
      failed("THE LEVEL COULD NOT BE PREPARED");
      return;
    }
    if (st == PLAY_WON) {
      snprintf(label, sizeof(label), "PREPARING LEVEL %lu",
               (unsigned long)(play_level() % 5 + 1));
      last_publish_us = 0;
      if (play_next(folder_name, level_progress, label)) return;
      st = PLAY_OVER;
    }
    if (st == PLAY_OVER) attract();
    return;
  }
  bool toggle = false, options_too = false;
  ikbd_key_event_t k;
  while (ikbd_pop_key(&k)) {
    if (!k.is_press) continue;
    if (k.scancode == SCAN_X) st_session_return_to_booster();
    if (k.scancode == SCAN_SPACE) toggle = true;
    if (k.scancode == SCAN_O) toggle = options_too = true;
  }
  ikbd_joystick_t j;
  ikbd_read_joystick(1, &j);
  if (j.pressed & IKBD_JOY_FIRE) toggle = true;
  if (!toggle || screen != SCREEN_TITLE) return;
  tune_stop(); /* the title's tune; the attract has its own music */
  attract();
  if (options_too && screen == SCREEN_GAME) play_options();
}
