/**
 * File: sfx.h
 * Description: The game's music and effects: the original sound driver
 *              translated to C, its songs, effects and tables read from
 *              the user's game image (the store's shared section,
 *              x2global.h), run the way the original's VBL handler runs
 *              it, every VBL: the effect requests first (the original's
 *              $414 on channel 2, $418 on channel 1), then the driver's
 *              tick. Its sound chip writes come back as (register, value)
 *              pairs for the ST's YM2149. Plain C, no SDK.
 */

#ifndef SFX_H
#define SFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SFX_WRITES_MAX 32 /* a tick writes 11 registers, a stop 4 more */
#define SFX_SONG_LEVEL 0  /* the song every level starts ($7A1E) */
#define SFX_TEMPO_PLAY 12 /* its tempo in play ($2D0BD) */
#define SFX_CHANNEL_A 2   /* the requests' channels: $414 */
#define SFX_CHANNEL_B 1   /* $418 */
#define SFX_CHANNEL_C 0   /* no request word: the game calls the driver */

typedef struct {
  uint8_t reg, value;
} sfx_write_t;

/* The RAM the driver writes: SFX_WINDOW_SIZE bytes, the caller's. */
#define SFX_WINDOW_SIZE 0x1900u

/* Take the driver's bytes (X2G_DRIVER_SIZE, as the game image holds them
 * at X2G_DRIVER_START) and start it from that state, as the game does;
 * `window` (SFX_WINDOW_SIZE bytes) gets the part it writes. */
void sfx_init(const uint8_t *driver, uint8_t *window);

/* Start song `song` at tempo `tempo` (the driver's $2CF42, then the
 * game's poke of the tempo byte). Its writes go to `out` (up to
 * SFX_WRITES_MAX); returns how many. */
int sfx_song(int song, int tempo, sfx_write_t *out);

/* Stop the music and the effects (the driver's $2CF5E). */
int sfx_stop(sfx_write_t *out);

/* Fade the music out over the next ticks (the driver's $2CF96, as the game
 * calls it leaving the attract sequence). */
void sfx_fade(void);

/* Stop the effect under way ($2CF88: the flamer's release). */
int sfx_stop_effect(sfx_write_t *out);

/* A byte of the driver's memory (its window), e.g. $2D0C2, set while an
 * effect plays. */
uint8_t sfx_peek(uint32_t addr);

/* Ask for effect `effect` on `channel` (SFX_CHANNEL_A / _B / _C): started at
 * the next VBL, A then B then C; a later request on the same channel
 * replaces it, as in the original. (The original starts C's at once, from
 * the code that asks: a VBL earlier at most.) */
void sfx_request(int effect, int channel);

/* Ask for it only if nothing is asked for on that channel yet: the gun's
 * rule ($6990). */
void sfx_request_if_free(int effect, int channel);

/* One VBL: the requests, then the tick. Returns the number of writes. */
int sfx_vbl(sfx_write_t *out);

/* Not 0 once a pattern held a command the driver does not have (the
 * original would jump into its code): the driver then stops. The driver's
 * sampled effects (Timer D) asked for so far. */
int sfx_error(void);
uint32_t sfx_samples_asked(void);

#ifdef __cplusplus
}
#endif

#endif /* SFX_H */
