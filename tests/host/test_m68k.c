// units: rp/src/m68k.c
/* The 68000 interpreter on hand-assembled programs: moves and their flags,
 * add and subtract with carry, overflow and the X flag, ADDX's Z rule, the
 * logic and compare forms, the shifts and rotates, the bit operations,
 * MOVEM in both directions, MOVEP, Bcc / DBcc / BSR / RTS, Scc, MULU,
 * EXT and SWAP, the PC-relative and indexed modes. */
#include <string.h>

#include "m68k.h"
#include "test.h"

static uint8_t mem[0x10000];
static uint8_t r8(void *c, uint32_t a) { (void)c; return a < sizeof(mem) ? mem[a] : 0; }
static void w8(void *c, uint32_t a, uint8_t v) {
  (void)c;
  if (a < sizeof(mem)) mem[a] = v;
}

static m68k_t cpu;

/* Load `words` at $1000 and call it; the program ends in RTS. */
static int run(const uint16_t *words, int n) {
  memset(&cpu, 0, sizeof(cpu));
  cpu.read8 = r8;
  cpu.write8 = w8;
  cpu.a[7] = 0x8000;
  for (int i = 0; i < n; i++) {
    mem[0x1000 + 2 * i] = (uint8_t)(words[i] >> 8);
    mem[0x1001 + 2 * i] = (uint8_t)words[i];
  }
  return m68k_call(&cpu, 0x1000, 10000);
}

#define RUN(...)                                              \
  do {                                                        \
    static const uint16_t p_[] = {__VA_ARGS__, 0x4E75};       \
    CHECK_EQ(run(p_, (int)(sizeof(p_) / sizeof(p_[0]))), 0);  \
  } while (0)

#define C 1
#define V 2
#define Z 4
#define N 8
#define X 16

int main(void) {
  /* moveq #-1,d0; move.w d0,d1 (N); moveq #0,d2 (Z) */
  RUN(0x70FF, 0x3200, 0x7400);
  CHECK_EQ(cpu.d[0], 0xFFFFFFFFu);
  CHECK_EQ(cpu.d[1], 0x0000FFFFu);
  CHECK_EQ(cpu.sr & 0x1F, Z);

  /* add.b #$80 to $80: carry, overflow, X, zero */
  RUN(0x7080, 0x0600, 0x0080); /* moveq #-128,d0; addi.b #$80,d0 */
  CHECK_EQ(cpu.d[0] & 0xFF, 0);
  CHECK_EQ(cpu.sr & 0x1F, C | V | Z | X);

  /* sub.w: 1 - 2 = $FFFF, borrow */
  RUN(0x7001, 0x7202, 0x9041); /* moveq #1,d0; moveq #2,d1; sub.w d1,d0 */
  CHECK_EQ(cpu.d[0], 0x0000FFFFu);
  CHECK_EQ(cpu.sr & 0x1F, N | C | X);

  /* cmp.b leaves X: moveq #0,d0; moveq #1,d1; ori #$10,ccr; cmp.b d1,d0 */
  RUN(0x7000, 0x7201, 0x003C, 0x0010, 0xB001);
  CHECK_EQ(cpu.sr & 0x1F, N | C | X);

  /* addx.b keeps Z when the result is 0: ori #$14,ccr (X Z); moveq #-1,d0;
   * moveq #0,d1; addx.b d1,d0 -> 0 with carry: Z stays, C X set */
  RUN(0x003C, 0x0014, 0x70FF, 0x7200, 0xD101);
  CHECK_EQ(cpu.d[0] & 0xFF, 0);
  CHECK_EQ(cpu.sr & 0x1F, Z | C | X);

  /* logic: moveq #$0F,d0; andi.b #$3C,d0; eori.b #$FF,d0; or.b d0,d0 */
  RUN(0x700F, 0x0200, 0x003C, 0x0A00, 0x00FF, 0x8000);
  CHECK_EQ(cpu.d[0] & 0xFF, 0xF3);
  CHECK_EQ(cpu.sr & 0x0F, N);

  /* shifts: moveq #$41,d0; lsl.b #2,d0 -> $04, C from bit 6 = 1;
   * asr.w: moveq #-4,d1; asr.w #1,d1 -> $FFFE; rol.b #1 of $81 -> $03 C */
  RUN(0x7041, 0xE508, 0x72FC, 0xE241, 0x7481, 0xE31A);
  CHECK_EQ(cpu.d[0] & 0xFF, 0x04);
  CHECK_EQ(cpu.d[1] & 0xFFFF, 0xFFFE);
  CHECK_EQ(cpu.d[2] & 0xFF, 0x03);
  CHECK_EQ(cpu.sr & C, C);

  /* roxl.w #1 with X set: moveq #0,d0; ori #$10,ccr; roxl.w #1,d0 -> 1 */
  RUN(0x7000, 0x003C, 0x0010, 0xE350);
  CHECK_EQ(cpu.d[0], 1);
  CHECK_EQ(cpu.sr & (X | C), 0);

  /* bits: moveq #0,d0; bset #5,d0; btst #5,d0 (Z clear); bchg #0,d0 */
  RUN(0x7000, 0x08C0, 0x0005, 0x0800, 0x0005, 0x0840, 0x0000);
  CHECK_EQ(cpu.d[0], 0x21);
  /* on memory, a byte: lea $2000,a0; move.b #$80,(a0); bclr #7,(a0) */
  mem[0x2000] = 0;
  RUN(0x41F8, 0x2000, 0x10BC, 0x0080, 0x0890, 0x0007);
  CHECK_EQ(mem[0x2000], 0);
  CHECK_EQ(cpu.sr & Z, 0); /* the bit was set */

  /* movem: moveq #1,d0; moveq #2,d1; lea $3000,a0;
   * movem.l d0-d1,-(a0); movem.w (a0)+,d2-d3 (words $0000 $0001) */
  RUN(0x7001, 0x7202, 0x41F8, 0x3000, 0x48E0, 0xC000, 0x4C98, 0x000C);
  CHECK_EQ(cpu.a[0], 0x2FFC);
  CHECK_EQ(cpu.d[2], 0);
  CHECK_EQ(cpu.d[3], 1);
  CHECK_EQ(mem[0x2FFB], 1);
  CHECK_EQ(mem[0x2FFF], 2);

  /* movep.l d0,0(a0): bytes to every other address */
  RUN(0x203C, 0x1122, 0x3344, 0x41F8, 0x4000, 0x01C8, 0x0000);
  CHECK_EQ(mem[0x4000], 0x11);
  CHECK_EQ(mem[0x4002], 0x22);
  CHECK_EQ(mem[0x4004], 0x33);
  CHECK_EQ(mem[0x4006], 0x44);
  /* and back: movep.w 0(a0),d1 */
  RUN(0x41F8, 0x4000, 0x0308, 0x0000);
  CHECK_EQ(cpu.d[1] & 0xFFFF, 0x1122);

  /* loop: moveq #0,d0; moveq #4,d1; L: addq.w #2,d0; dbf d1,L */
  RUN(0x7000, 0x7204, 0x5440, 0x51C9, 0xFFFC);
  CHECK_EQ(cpu.d[0], 10);
  CHECK_EQ(cpu.d[1] & 0xFFFF, 0xFFFF);

  /* bsr to a routine that sets d0: bsr.s +2; bra.s +4; moveq #7,d0; rts */
  RUN(0x6102, 0x6004, 0x7007, 0x4E75);
  CHECK_EQ(cpu.d[0], 7);

  /* bcc taken / not: moveq #1,d0; cmpi.b #2,d0; bcs.s +2; moveq #9,d0 */
  RUN(0x7001, 0x0C00, 0x0002, 0x6502, 0x7009);
  CHECK_EQ(cpu.d[0], 1);

  /* scc: moveq #0,d0; tst.b d0; seq d1 */
  RUN(0x7000, 0x4A00, 0x57C1);
  CHECK_EQ(cpu.d[1] & 0xFF, 0xFF);

  /* mulu.w #300,d0 with d0 = 300; ext.w of $80; swap */
  RUN(0x303C, 0x012C, 0xC0FC, 0x012C, 0x7280, 0x0281, 0x0000, 0x00FF, 0x4881, 0x4842);
  CHECK_EQ(cpu.d[0], 90000);
  CHECK_EQ(cpu.d[1] & 0xFFFF, 0xFF80);

  /* pc-relative and indexed: lea table(pc),a0; moveq #2,d1;
   * move.b 0(a0,d1.w),d0; rts; table: $10,$20,$30 */
  RUN(0x41FA, 0x000A, 0x7202, 0x1030, 0x1000, 0x4E75, 0x1020, 0x3000);
  CHECK_EQ(cpu.d[0] & 0xFF, 0x30);

  /* an unknown instruction stops with an error at its address */
  static const uint16_t bad[] = {0x7000, 0xA000};
  CHECK_EQ(run(bad, 2), M68K_ERR_OPCODE);
  CHECK_EQ(cpu.error_pc, 0x1002);
  TEST_END();
}
