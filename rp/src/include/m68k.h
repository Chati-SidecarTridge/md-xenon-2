/**
 * File: m68k.h
 * Description: A small interpreter of the 68000's user-level integer
 *              instructions, to run routines of the original game (its
 *              sound driver) from the user's own game image on the RP. Memory
 *              and I/O go through the caller's byte callbacks. Plain C, no
 *              SDK.
 *
 * Covered: every addressing mode; MOVE, MOVEA, MOVEQ, MOVEM, MOVEP, LEA,
 * PEA, CLR, TST, NOT, NEG, NEGX, EXT, SWAP, EXG, ADD(A/I/Q/X), SUB(A/I/Q/X),
 * CMP(A/I/M), AND(I), OR(I), EOR(I), MULU, MULS, DIVU, DIVS, the shifts and
 * rotates (register and memory), BTST/BCHG/BCLR/BSET, Bcc/BRA/BSR, DBcc,
 * Scc, JMP, JSR, RTS, NOP, LINK, UNLK, the moves to and from SR and CCR,
 * ANDI/ORI/EORI to SR and CCR. Not covered (the routine stops with an
 * error): exceptions, traps, CHK, TAS, BCD arithmetic, RTE, RTR, STOP,
 * RESET, MOVE USP. The status register's mask is kept but never acted on.
 */

#ifndef M68K_H
#define M68K_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t d[8];
  uint32_t a[8]; /* a[7] the stack pointer */
  uint32_t pc;
  uint16_t sr;   /* the condition codes in bits 0-4: C V Z N X */
  void *ctx;
  uint8_t (*read8)(void *ctx, uint32_t addr);
  void (*write8)(void *ctx, uint32_t addr, uint8_t value);
  uint32_t error_pc;   /* where an instruction it does not run was met */
  uint16_t error_op;
} m68k_t;

#define M68K_OK 0
#define M68K_ERR_OPCODE (-1) /* an instruction it does not run */
#define M68K_ERR_STEPS (-2)  /* still running after the step limit */

/* Call the routine at `entry` as a JSR would and run it until it returns
 * (RTS to the return address pushed here), at most `max_steps`
 * instructions. The registers and the stack pointer are the caller's. */
int m68k_call(m68k_t *cpu, uint32_t entry, uint32_t max_steps);

/* Run one instruction at pc. M68K_OK or M68K_ERR_OPCODE. */
int m68k_step(m68k_t *cpu);

#ifdef __cplusplus
}
#endif

#endif /* M68K_H */
