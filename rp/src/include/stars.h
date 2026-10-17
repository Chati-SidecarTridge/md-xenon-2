/**
 * File: stars.h
 * Description: The stars over the scenery, as the original draws them ($2B1A,
 *              moved by $2ABA): 48 single pixels in four groups of 12,
 *              colours 4, 5, 6 and 7, each drawn only where the scenery
 *              shows colour 0. Each original frame star i moves by the last
 *              scroll step x (384 + 8 i) / 384 lines, wrapping in the
 *              playfield. They start at random places (the original's from
 *              its own generator, so the port's stars are not the
 *              original's, only like them). Plain C, no SDK.
 */

#ifndef STARS_H
#define STARS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STARS_COUNT 48
#define STARS_GROUP 12
#define STARS_LINES 192
#define STARS_WIDTH 320
#define STARS_FRACTION 384 /* a line, in the fraction's units */

typedef struct {
  struct {
    uint16_t x, line;
    int16_t fraction;
  } star[STARS_COUNT];
  uint32_t random;
} stars_t;

/* Every star at a random place (`seed` starts the generator). */
void stars_init(stars_t *s, uint32_t seed);

/* One original frame: the last scroll step (the original's $CD8). */
void stars_move(stars_t *s, int32_t step);

/* Draw into `dst` (STARS_LINES lines of STARS_WIDTH bytes): a star shows
 * where the pixel is colour 0. */
void stars_draw(const stars_t *s, uint8_t *dst);

#ifdef __cplusplus
}
#endif

#endif /* STARS_H */
