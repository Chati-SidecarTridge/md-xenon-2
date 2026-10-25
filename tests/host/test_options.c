// units: rp/src/options.c
/* The options: each starts at the original's value and packs to 0; every
 * value packs and comes back; a stored value out of range is the
 * original's; the pace each frame rate and speed give; the damage and the
 * lives; the screen's lines, 20 characters of the game's small font. */
#include <stdlib.h>
#include <string.h>

#include "options.h"
#include "scroll.h"
#include "test.h"

/* The small font's characters, and the space it skips. */
static bool small_font(const char *line) {
  for (const char *c = line; *c; c++) {
    if (*c == ' ' || *c == '.' || *c == ':') continue;
    if ((*c < 'A' || *c > 'Z') && (*c < '0' || *c > '9')) return false;
  }
  return true;
}

int main(void) {
  options_t o;
  options_unpack(&o, 0); /* the original's */
  CHECK_EQ(options_pack(&o), 0u);

  /* The defaults: 50, the sprites at the original's speed, a hit taking
   * 1, 5 lives, the shots x3, level 1, the music on, the DMA chip; the
   * settings' default is their packed value. */
  options_t d;
  options_default(&d);
  CHECK_EQ(memcmp(&d, &options, sizeof(d)), 0);
  CHECK(d.fifty && !d.fast && d.shots && d.music && !d.ym);
  CHECK_EQ(options_damage(&d, 8), 1);
  CHECK_EQ(options_lives(&d), 5);
  CHECK_EQ(options_start_level(&d), 1);
  CHECK(options_between(&d));
  CHECK_EQ(options_pack(&d), (uint32_t)atoi(OPTIONS_DEFAULT_PACKED));
  CHECK(o.music);
  CHECK_EQ(options_lives(&o), 3);
  CHECK_EQ(options_damage(&o, 4), 4);
  CHECK_EQ(options_damage(&o, 16), 16);
  CHECK(!options_between(&o));
  CHECK_EQ(options_vbls_q8(&o), SCROLL_VBLS_PER_FRAME_Q8);

  /* Every combination packs and comes back. */
  int seen = 0;
  for (uint32_t v = 0; v < 4096; v++) {
    options_t a, b;
    options_unpack(&a, v);
    if (options_pack(&a) != v) continue; /* a value out of range */
    seen++;
    options_unpack(&b, options_pack(&a));
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
  }
  CHECK_EQ(seen, 2 * 2 * 3 * 3 * 2 * 2 * 2 * 5);
  options_unpack(&o, 3u << 2); /* damage 3: none such */
  CHECK_EQ(options_pack(&o), options_pack(&d));
  options_unpack(&o, 5u << 9); /* level 6: none such */
  CHECK_EQ(options_pack(&o), options_pack(&d));
  options_unpack(&o, 99999u);
  CHECK_EQ(options_pack(&o), options_pack(&d));

  /* Each key steps its option round its values; F7 puts all back. */
  options_unpack(&o, 0); /* the original's */
  options_step(&o, OPTIONS_DAMAGE);
  CHECK_EQ(options_damage(&o, 8), 1);
  options_step(&o, OPTIONS_DAMAGE);
  CHECK_EQ(options_damage(&o, 8), 0);
  options_step(&o, OPTIONS_DAMAGE);
  CHECK_EQ(options_damage(&o, 8), 8);
  options_step(&o, OPTIONS_LIVES);
  CHECK_EQ(options_lives(&o), 5);
  options_step(&o, OPTIONS_LIVES);
  CHECK_EQ(options_lives(&o), 9);
  options_step(&o, OPTIONS_LIVES);
  CHECK_EQ(options_lives(&o), 3);
  options_step(&o, OPTIONS_SHOTS);
  CHECK_EQ(options_weapon_steps(&o), 3);
  options_step(&o, OPTIONS_SHOTS);
  CHECK_EQ(options_weapon_steps(&o), 1);
  CHECK_EQ(options_start_level(&o), 1);
  for (int l = 2; l <= 5; l++) {
    options_step(&o, OPTIONS_START);
    CHECK_EQ(options_start_level(&o), l);
  }
  options_step(&o, OPTIONS_START);
  CHECK_EQ(options_start_level(&o), 1);
  options_step(&o, OPTIONS_MUSIC);
  CHECK(!o.music);
  options_step(&o, OPTIONS_STE_SOUND);
  CHECK(o.ym);
  options_step(&o, OPTIONS_DEFAULTS);
  CHECK_EQ(options_pack(&o), (uint32_t)atoi(OPTIONS_DEFAULT_PACKED));

  /* The pace: at 17 the sprites' speed is the original's and F2 does
   * nothing; at 50 with the sprites at the original's speed the game steps
   * every 3 VBLs, the world on alone between; at x3 every VBL is a step;
   * back at 17 it is the original's again. */
  options_unpack(&o, 0); /* the original's */
  options_step(&o, OPTIONS_SPEED);
  CHECK(!o.fast);
  CHECK_EQ(strncmp(options_line(&o, OPTIONS_SPEED), "F2 SPRITES  ORIGINAL", OPTIONS_LINE), 0);
  options_step(&o, OPTIONS_FRAME_RATE);
  CHECK(options_between(&o));
  CHECK_EQ(options_vbls_q8(&o), 768u);
  options_step(&o, OPTIONS_SPEED);
  CHECK(!options_between(&o));
  CHECK_EQ(options_vbls_q8(&o), 256u);
  CHECK_EQ(strncmp(options_line(&o, OPTIONS_SPEED), "F2 SPRITES        X3", OPTIONS_LINE), 0);
  options_step(&o, OPTIONS_FRAME_RATE);
  CHECK(!options_between(&o));
  CHECK_EQ(options_vbls_q8(&o), 768u);
  CHECK_EQ(strncmp(options_line(&o, OPTIONS_SPEED), "F2 SPRITES  ORIGINAL", OPTIONS_LINE), 0);

  /* The screen's lines, at every value. */
  for (uint32_t v = 0; v < 4096; v++) {
    options_unpack(&o, v);
    for (int i = 0; i < OPTIONS_COUNT; i++) {
      const char *l = options_line(&o, (options_item_t)i);
      CHECK_EQ((int)strlen(l), OPTIONS_LINE);
      CHECK(small_font(l));
    }
  }
  TEST_END();
}
