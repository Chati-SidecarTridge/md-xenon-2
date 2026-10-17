/**
 * File: ym_ring.h
 * Description: The sound chip's register writes sent to the ST
 *              (cart_shared.h: CART_YM_RING_OFFSET): words of a register
 *              (high byte) and its value (low byte), the last followed by
 *              CART_YM_RING_END; the ST writes them to its YM2149 every
 *              VBL. Plain C, no SDK.
 */

#ifndef YM_RING_H
#define YM_RING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Empty the ring: the ST, starting again from its first word, finds the
 * end there. Call when the ST boots (its position starts at 0). */
void ym_ring_reset(volatile uint16_t *ring);

/* Append one register write. The end is written after it first, so the ST
 * never reads past a new word into an old one. */
void ym_ring_push(volatile uint16_t *ring, uint8_t reg, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* YM_RING_H */
