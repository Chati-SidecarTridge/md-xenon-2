/**
 * File: options.h
 * Description: The player's options, changed on the options screen (O on
 *              the attract, a function key an option) and kept in the
 *              app's settings: the frame rate, the sprites' speed, the
 *              cheats (the shield a hit takes, the lives, the weapons three
 *              times as fast, the level a game starts at), the music and
 *              the STE's sound. Each starts at the original's value. Plain C, no SDK.
 *
 * The original runs a frame every 3 VBLs (16.7 a second). The frame rate is
 * how often the world scrolls: at 17 a line every 3 VBLs, the original's,
 * at 50 a line every VBL. At 50 the sprites (the ship, the shots, the
 * enemies, cash, tokens, explosions) and the timers either run every VBL
 * too, three times the original's speed, or keep the original's: the game
 * steps every third VBL, seeing the lines the world moved meanwhile, and
 * what moves is drawn on its way between its last two places.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OPTIONS_FRAME_RATE, /* F1: 17 (the original's) or 50 frames a second */
  OPTIONS_SPEED,      /* F2: at 50, the sprites at the original's speed or x3 */
  OPTIONS_DAMAGE,     /* F3: the shield a hit takes: the original's, 1, none */
  OPTIONS_LIVES,      /* F4: 3 (the original's), 5, 9 */
  OPTIONS_SHOTS,      /* F5: the ship's weapons at the original's speed or x3 */
  OPTIONS_START,      /* F6: the level a game starts at, 1 (the original's) to 5 */
  OPTIONS_MUSIC,      /* F7: SELECT's MUSIC ON / OFF ($9FC) */
  OPTIONS_STE_SOUND,  /* F8: an STE's sound through the DMA chip or the YM */
  OPTIONS_ORIGINAL,   /* F9: every option back to the original's */
  OPTIONS_COUNT
} options_item_t;

enum { OPTIONS_DAMAGE_ORIGINAL, OPTIONS_DAMAGE_ONE, OPTIONS_DAMAGE_NONE };

#define OPTIONS_LINE 20 /* characters on a line of the options screen */

typedef struct {
  bool fifty;     /* 50 frames a second: the world a line every VBL */
  bool fast;      /* at 50, the sprites and timers every VBL too */
  uint8_t damage; /* OPTIONS_DAMAGE_* */
  uint8_t lives;  /* 0, 1, 2: 3, 5, 9 */
  bool music;
  bool ym;        /* an STE through the YM, from its next reset */
  bool shots;     /* the weapons stepped three times a step of the game */
  uint8_t start;  /* 0-4: levels 1-5 */
} options_t;

/* The options the game plays with. */
extern options_t options;

/* Every option at the original's value. */
void options_original(options_t *o);

/* The option `item` to its next value (OPTIONS_ORIGINAL: all of them back). */
void options_step(options_t *o, options_item_t item);

/* The options as one number for the settings (0: the original's), and
 * back; a value out of range comes back as the original's. */
uint32_t options_pack(const options_t *o);
void options_unpack(options_t *o, uint32_t v);

/* The lives a game starts with, and each continue. */
int options_lives(const options_t *o);

/* The weapons' steps a step of the game (1, or 3 with the shots cheat). */
int options_weapon_steps(const options_t *o);

/* The level a game starts at, 1-5. */
int options_start_level(const options_t *o);

/* The shield a hit of `damage` takes. */
int32_t options_damage(const options_t *o, int32_t damage);

/* The pace: VBLs per step of the game (8.8 fixed point); whether the world
 * moves on its own every VBL between steps, the sprites drawn between their
 * places (50 frames a second, the sprites at the original's speed). */
uint32_t options_vbls_q8(const options_t *o);
bool options_between(const options_t *o);

/* The options screen's line for `item`: OPTIONS_LINE characters of the
 * game's small font. */
const char *options_line(const options_t *o, options_item_t item);

#ifdef __cplusplus
}
#endif

#endif /* OPTIONS_H */
