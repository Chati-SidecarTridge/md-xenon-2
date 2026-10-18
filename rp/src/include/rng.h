/**
 * File: rng.h
 * Description: The game's random generator ($27FE): two longs of state,
 *              stepped by an add-with-carry on their halves. The enemies
 *              and the screens outside play each keep their own state.
 *              Plain C, no SDK.
 */

#ifndef RNG_H
#define RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The state seeded (never all zero). */
void rng_seed(uint32_t state[2], uint32_t seed);

/* The next number: d0 as the original returns it (its low word is what
 * most callers use). */
uint32_t rng_next(uint32_t state[2]);

#ifdef __cplusplus
}
#endif

#endif /* RNG_H */
