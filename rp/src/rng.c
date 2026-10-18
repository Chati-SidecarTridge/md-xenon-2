/**
 * File: rng.c
 * Description: The game's random generator (rng.h).
 */

#include "rng.h"

static uint32_t swap16(uint32_t v) { return (v << 16) | (v >> 16); }

void rng_seed(uint32_t state[2], uint32_t seed) {
  state[0] = seed | 1u;
  state[1] = seed * 2654435761u;
}

uint32_t rng_next(uint32_t state[2]) {
  uint32_t d0 = state[0], d1 = state[1];
  uint32_t lo = (d0 & 0xFFFFu) * 2u, x = lo >> 16;
  uint32_t d2 = lo & 0xFFFFu;
  uint32_t sum = d2 + (d1 & 0xFFFFu) + x;
  x = sum >> 16;
  d0 = (d0 & 0xFFFF0000u) | (sum & 0xFFFFu);
  d1 = (d1 & 0xFFFF0000u) | d2;
  d0 = swap16(d0);
  d1 = swap16(d1);
  d2 = d0 & 0xFFFFu;
  sum = d2 + (d1 & 0xFFFFu) + x;
  d0 = (d0 & 0xFFFF0000u) | (sum & 0xFFFFu);
  d1 = (d1 & 0xFFFF0000u) | d2;
  state[0] = swap16(d0);
  state[1] = swap16(d1);
  return state[0];
}
