/**
 * File: flow.h
 * Description: The game's screens outside play and the shop, ported from
 *              the original's: the attract ($82D2: the logo zooming over
 *              3-D stars, the six credit pairs, the high-score table),
 *              SELECT ($8CAA), the messages over the stars ("GET READY
 *              PLAYER n", "GAME OVER": $84C4), CONTINUE ($8E7C) and the
 *              high-score entry ($8A36). Their texts, fonts, logo and
 *              tables are the original's, read from the store's screens
 *              section (x2screens.h); their pace is the original's on the
 *              ST's clock (screen.h). Plain C, no SDK.
 *
 * One screen runs at a time: a flow_* call starts it, flow_vbl steps it a
 * VBL at a time until it ends, flow_draw draws it, flow_result says how.
 * The original's frames keep its pace; they are shown at 50 frames a
 * second, each VBL a step of the way from one frame to the next.
 */

#ifndef FLOW_H
#define FLOW_H

#include <stdbool.h>
#include <stdint.h>

#include "x2global.h"
#include "x2screens.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLOW_SCORES 10
#define FLOW_SCORE_TEXT 15 /* "RR. SSSSSSS NNN" */

/* The high-score table as the original keeps it: the scores ($893E) and
 * the lines shown ($8966), highest first. */
typedef struct {
  uint32_t score[FLOW_SCORES];
  char text[FLOW_SCORES][FLOW_SCORE_TEXT];
} flow_scores_t;

/* What the screens ask of the game. */
typedef struct {
  void *ctx;
  void (*song)(void *ctx, int tempo);   /* song 0 ($2CF42) at `tempo` */
  void (*stop)(void *ctx);              /* the driver stopped ($2CF5E) */
  void (*fade)(void *ctx);              /* the music faded ($2CF96) */
  void (*effect)(void *ctx, int effect); /* $414 */
} flow_hooks_t;

/* The screens' images, their random generator's seed, the table (the
 * original's empty one if `scores` is all zero), the game's hooks. */
void flow_init(const x2screens_header_t *image, const x2global_header_t *global,
               uint32_t seed, flow_scores_t *scores, const flow_hooks_t *hooks);

/* The original's empty table. */
void flow_scores_clear(flow_scores_t *scores);

/* The attract until fire. */
void flow_attract(void);

/* SELECT: flow_result() 1 or 2 players. `music` is the game's music flag
 * ($9FC), which SELECT may toggle. */
void flow_select(int credits, bool *music);

/* The options (options.h) until fire; the game changes them meanwhile. */
void flow_options(void);
/* "GET READY PLAYER n" (waits for fire at its middle) or "GAME OVER". */
void flow_get_ready(int player);
void flow_game_over(void);

/* CONTINUE GAME with the credits left: flow_result() 1 to go on. */
void flow_continue(int credits);

/* A score entered in the table if it beats the tenth: false if not (no
 * screen), else the entry runs. */
bool flow_high_score(uint32_t score);

/* One VBL: the stick as the IKBD reports it (up 1, down 2, left 4, right
 * 8, fire $80) and a fire pressed since the last call. True while the
 * screen runs. */
bool flow_vbl(uint8_t stick, bool fire);

/* The screen as it stands at this VBL into the framebuffer: the last frame
 * moving toward the next one, the stars over it. */
void flow_draw(void);

int flow_result(void);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_H */
