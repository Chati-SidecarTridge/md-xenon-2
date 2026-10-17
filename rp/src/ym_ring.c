/**
 * File: ym_ring.c
 * Description: The sound chip's register writes for the ST (ym_ring.h).
 */

#include "ym_ring.h"

#include "cart_shared.h"

#define WORDS (CART_YM_RING_SIZE / 2u)

static uint32_t head;

void ym_ring_reset(volatile uint16_t *ring) {
  head = 0;
  ring[0] = CART_YM_RING_END;
}

void ym_ring_push(volatile uint16_t *ring, uint8_t reg, uint8_t value) {
  uint32_t next = (head + 1u) & (WORDS - 1u);
  ring[next] = CART_YM_RING_END;
  ring[head] = (uint16_t)(reg << 8 | value);
  head = next;
}
