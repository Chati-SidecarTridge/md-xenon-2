/**
 * File: options.c
 * Description: The player's options (options.h).
 */

#include "options.h"

#include "scroll.h"

options_t options = {false, false, OPTIONS_DAMAGE_ORIGINAL, 0, true, false, false, 0};

static const uint8_t k_lives[3] = {3, 5, 9};

void options_original(options_t *o) {
  *o = (options_t){false, false, OPTIONS_DAMAGE_ORIGINAL, 0, true, false, false, 0};
}

void options_step(options_t *o, options_item_t item) {
  switch (item) {
    case OPTIONS_FRAME_RATE: o->fifty = !o->fifty; break;
    case OPTIONS_SPEED: /* only at 50 */
      if (o->fifty) o->fast = !o->fast;
      break;
    case OPTIONS_DAMAGE: o->damage = (uint8_t)((o->damage + 1) % 3); break;
    case OPTIONS_LIVES: o->lives = (uint8_t)((o->lives + 1) % 3); break;
    case OPTIONS_SHOTS: o->shots = !o->shots; break;
    case OPTIONS_START: o->start = (uint8_t)((o->start + 1) % 5); break;
    case OPTIONS_MUSIC: o->music = !o->music; break;
    case OPTIONS_STE_SOUND: o->ym = !o->ym; break;
    case OPTIONS_ORIGINAL: options_original(o); break;
    default: break;
  }
}

/* Bit 0 fifty, 1 fast, 2-3 damage, 4-5 lives, 6 the music off, 7 ym,
 * 8 shots, 9-11 the start level. */
uint32_t options_pack(const options_t *o) {
  return (o->fifty ? 1u : 0u) | (o->fast ? 2u : 0u) | (uint32_t)o->damage << 2 |
         (uint32_t)o->lives << 4 | (o->music ? 0u : 64u) | (o->ym ? 128u : 0u) |
         (o->shots ? 256u : 0u) | (uint32_t)o->start << 9;
}

void options_unpack(options_t *o, uint32_t v) {
  options_original(o);
  if (v > 4095u || (v >> 2 & 3u) > 2u || (v >> 4 & 3u) > 2u || (v >> 9) > 4u) return;
  o->fifty = (v & 1u) != 0;
  o->fast = (v & 2u) != 0;
  o->damage = (uint8_t)(v >> 2 & 3u);
  o->lives = (uint8_t)(v >> 4 & 3u);
  o->music = (v & 64u) == 0;
  o->ym = (v & 128u) != 0;
  o->shots = (v & 256u) != 0;
  o->start = (uint8_t)(v >> 9);
}

int options_lives(const options_t *o) { return k_lives[o->lives % 3u]; }

int options_weapon_steps(const options_t *o) { return o->shots ? 3 : 1; }

int options_start_level(const options_t *o) { return 1 + o->start % 5; }

int32_t options_damage(const options_t *o, int32_t damage) {
  if (o->damage == OPTIONS_DAMAGE_NONE) return 0;
  if (o->damage == OPTIONS_DAMAGE_ONE) return 1;
  return damage;
}

uint32_t options_vbls_q8(const options_t *o) {
  return o->fast && o->fifty ? SCROLL_VBLS_PER_FRAME_Q8 / 3u : SCROLL_VBLS_PER_FRAME_Q8;
}

bool options_between(const options_t *o) { return o->fifty && !o->fast; }

const char *options_line(const options_t *o, options_item_t item) {
  switch (item) {
    case OPTIONS_FRAME_RATE: return o->fifty ? "F1 FRAME RATE     50" : "F1 FRAME RATE     17";
    case OPTIONS_SPEED:
      return o->fifty && o->fast ? "F2 SPRITES        X3" : "F2 SPRITES  ORIGINAL";
    case OPTIONS_DAMAGE:
      return o->damage == OPTIONS_DAMAGE_NONE ? "F3 DAMAGE       NONE"
             : o->damage == OPTIONS_DAMAGE_ONE ? "F3 DAMAGE          1"
                                               : "F3 DAMAGE   ORIGINAL";
    case OPTIONS_LIVES:
      return o->lives == 2 ? "F4 LIVES           9"
             : o->lives == 1 ? "F4 LIVES           5"
                             : "F4 LIVES           3";
    case OPTIONS_SHOTS: return o->shots ? "F5 SHOTS          X3" : "F5 SHOTS    ORIGINAL";
    case OPTIONS_START: {
      static const char *const k_start[5] = {
          "F6 START LEVEL     1", "F6 START LEVEL     2", "F6 START LEVEL     3",
          "F6 START LEVEL     4", "F6 START LEVEL     5"};
      return k_start[o->start % 5];
    }
    case OPTIONS_MUSIC: return o->music ? "F7 MUSIC          ON" : "F7 MUSIC         OFF";
    case OPTIONS_STE_SOUND: return o->ym ? "F8 STE SOUND      YM" : "F8 STE SOUND     DMA";
    case OPTIONS_ORIGINAL: return "F9 ALL ORIGINAL     ";
    default: return "                    ";
  }
}
