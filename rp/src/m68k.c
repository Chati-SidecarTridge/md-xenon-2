/**
 * File: m68k.c
 * Description: The 68000 interpreter (m68k.h). One instruction at a time,
 *              decoded from its opcode word; addresses are 24 bits, as on
 *              the ST.
 */

#pragma GCC optimize("O3")

#include "m68k.h"

#include <stdbool.h>

#define CF 0x01u
#define VF 0x02u
#define ZF 0x04u
#define NF 0x08u
#define XF 0x10u
#define RETURN_SENTINEL 0x00FFFFF0u

typedef m68k_t cpu_t;

/* ---- memory ---- */

static uint8_t rb(cpu_t *c, uint32_t a) { return c->read8(c->ctx, a & 0xFFFFFFu); }
static void wb(cpu_t *c, uint32_t a, uint8_t v) { c->write8(c->ctx, a & 0xFFFFFFu, v); }
static uint16_t rw(cpu_t *c, uint32_t a) { return (uint16_t)(rb(c, a) << 8 | rb(c, a + 1)); }
static void ww(cpu_t *c, uint32_t a, uint16_t v) {
  wb(c, a, (uint8_t)(v >> 8));
  wb(c, a + 1, (uint8_t)v);
}
static uint32_t rl(cpu_t *c, uint32_t a) { return (uint32_t)rw(c, a) << 16 | rw(c, a + 2); }
static void wl(cpu_t *c, uint32_t a, uint32_t v) {
  ww(c, a, (uint16_t)(v >> 16));
  ww(c, a + 2, (uint16_t)v);
}
static uint16_t fetch16(cpu_t *c) {
  uint16_t v = rw(c, c->pc);
  c->pc += 2;
  return v;
}
static uint32_t fetch32(cpu_t *c) {
  uint32_t v = rl(c, c->pc);
  c->pc += 4;
  return v;
}

/* ---- sizes: 0 byte, 1 word, 2 long ---- */

static const uint32_t MASK[3] = {0xFFu, 0xFFFFu, 0xFFFFFFFFu};
static const uint32_t MSB[3] = {0x80u, 0x8000u, 0x80000000u};
static const uint32_t BYTES[3] = {1, 2, 4};

static uint32_t sext(uint32_t v, int sz) {
  if (sz == 0) return (uint32_t)(int32_t)(int8_t)v;
  if (sz == 1) return (uint32_t)(int32_t)(int16_t)v;
  return v;
}

static uint32_t mread(cpu_t *c, uint32_t a, int sz) {
  return sz == 0 ? rb(c, a) : sz == 1 ? rw(c, a) : rl(c, a);
}
static void mwrite(cpu_t *c, uint32_t a, int sz, uint32_t v) {
  if (sz == 0) wb(c, a, (uint8_t)v);
  else if (sz == 1) ww(c, a, (uint16_t)v);
  else wl(c, a, v);
}

/* ---- effective addresses ---- */

typedef enum { EA_D, EA_A, EA_MEM, EA_IMM } ea_kind_t;
typedef struct {
  ea_kind_t kind;
  int reg;
  uint32_t addr;
  uint32_t imm;
} ea_t;

static uint32_t index_word(cpu_t *c, uint32_t base) {
  uint16_t ext = fetch16(c);
  int reg = (ext >> 12) & 7;
  uint32_t x = (ext & 0x8000u) ? c->a[reg] : c->d[reg];
  if (!(ext & 0x0800u)) x = sext(x, 1);
  return base + x + sext(ext & 0xFFu, 0);
}

/* Decode mode/reg for an access of size `sz`; false if invalid. */
static bool ea(cpu_t *c, int mode, int reg, int sz, ea_t *e) {
  uint32_t step;
  switch (mode) {
    case 0:
      e->kind = EA_D;
      e->reg = reg;
      return true;
    case 1:
      e->kind = EA_A;
      e->reg = reg;
      return true;
    case 2:
      e->kind = EA_MEM;
      e->addr = c->a[reg];
      return true;
    case 3:
      step = (sz == 0 && reg == 7) ? 2 : BYTES[sz];
      e->kind = EA_MEM;
      e->addr = c->a[reg];
      c->a[reg] += step;
      return true;
    case 4:
      step = (sz == 0 && reg == 7) ? 2 : BYTES[sz];
      c->a[reg] -= step;
      e->kind = EA_MEM;
      e->addr = c->a[reg];
      return true;
    case 5:
      e->kind = EA_MEM;
      e->addr = c->a[reg] + sext(fetch16(c), 1);
      return true;
    case 6:
      e->kind = EA_MEM;
      e->addr = index_word(c, c->a[reg]);
      return true;
    default:
      break;
  }
  e->kind = EA_MEM;
  switch (reg) {
    case 0:
      e->addr = sext(fetch16(c), 1);
      return true;
    case 1:
      e->addr = fetch32(c);
      return true;
    case 2: {
      uint32_t base = c->pc;
      e->addr = base + sext(fetch16(c), 1);
      return true;
    }
    case 3:
      e->addr = index_word(c, c->pc);
      return true;
    case 4:
      e->kind = EA_IMM;
      e->imm = sz == 2 ? fetch32(c) : (fetch16(c) & MASK[sz]);
      return true;
    default:
      return false;
  }
}

static uint32_t ea_read(cpu_t *c, const ea_t *e, int sz) {
  switch (e->kind) {
    case EA_D: return c->d[e->reg] & MASK[sz];
    case EA_A: return c->a[e->reg] & MASK[sz];
    case EA_IMM: return e->imm;
    default: return mread(c, e->addr, sz);
  }
}

static void ea_write(cpu_t *c, const ea_t *e, int sz, uint32_t v) {
  v &= MASK[sz];
  switch (e->kind) {
    case EA_D:
      c->d[e->reg] = (c->d[e->reg] & ~MASK[sz]) | v;
      break;
    case EA_A:
      c->a[e->reg] = sext(v, sz);
      break;
    case EA_MEM:
      mwrite(c, e->addr, sz, v);
      break;
    default:
      break;
  }
}

/* ---- flags ---- */

static void flags_logic(cpu_t *c, uint32_t r, int sz) {
  r &= MASK[sz];
  c->sr = (uint16_t)((c->sr & ~(NF | ZF | VF | CF)) | (r & MSB[sz] ? NF : 0) |
                     (r == 0 ? ZF : 0));
}

static uint32_t do_add(cpu_t *c, uint32_t d, uint32_t s, int sz, bool x_in,
                       bool keep_z) {
  uint64_t r64 = (uint64_t)(d & MASK[sz]) + (s & MASK[sz]) + (x_in ? 1 : 0);
  uint32_t r = (uint32_t)r64 & MASK[sz];
  bool carry = (r64 >> (8 * BYTES[sz])) & 1;
  bool ovf = ((~(d ^ s) & (d ^ r)) & MSB[sz]) != 0;
  uint16_t sr = c->sr & ~(NF | VF | CF | XF | (keep_z ? 0 : ZF));
  if (r & MSB[sz]) sr |= NF;
  if (keep_z ? r != 0 : r == 0) sr = keep_z ? (uint16_t)(sr & ~ZF) : (sr | ZF);
  if (ovf) sr |= VF;
  if (carry) sr |= CF | XF;
  c->sr = sr;
  return r;
}

/* d - s; `with_x` sets X too (not for CMP). */
static uint32_t do_sub(cpu_t *c, uint32_t d, uint32_t s, int sz, bool x_in,
                       bool keep_z, bool with_x) {
  uint32_t dm = d & MASK[sz], sm = s & MASK[sz];
  uint64_t r64 = (uint64_t)dm - sm - (x_in ? 1 : 0);
  uint32_t r = (uint32_t)r64 & MASK[sz];
  bool borrow = (uint64_t)sm + (x_in ? 1 : 0) > dm;
  bool ovf = (((d ^ s) & (d ^ r)) & MSB[sz]) != 0;
  uint16_t sr = c->sr & ~(NF | VF | CF | (with_x ? XF : 0) | (keep_z ? 0 : ZF));
  if (r & MSB[sz]) sr |= NF;
  if (keep_z) {
    if (r != 0) sr &= (uint16_t)~ZF;
  } else if (r == 0) {
    sr |= ZF;
  }
  if (ovf) sr |= VF;
  if (borrow) sr |= CF | (with_x ? XF : 0);
  c->sr = sr;
  return r;
}

static bool cond(cpu_t *c, int cc) {
  uint16_t s = c->sr;
  bool C = s & CF, V = s & VF, Z = s & ZF, N = s & NF;
  switch (cc) {
    case 0: return true;
    case 1: return false;
    case 2: return !C && !Z;
    case 3: return C || Z;
    case 4: return !C;
    case 5: return C;
    case 6: return !Z;
    case 7: return Z;
    case 8: return !V;
    case 9: return V;
    case 10: return !N;
    case 11: return N;
    case 12: return N == V;
    case 13: return N != V;
    case 14: return !Z && N == V;
    default: return Z || N != V;
  }
}

static void push32(cpu_t *c, uint32_t v) {
  c->a[7] -= 4;
  wl(c, c->a[7], v);
}
static uint32_t pop32(cpu_t *c) {
  uint32_t v = rl(c, c->a[7]);
  c->a[7] += 4;
  return v;
}

/* ---- shifts and rotates: type 0 AS, 1 LS, 2 ROX, 3 RO ---- */

static uint32_t shift(cpu_t *c, uint32_t v, int sz, int type, bool left,
                      unsigned count) {
  uint32_t m = MASK[sz], top = MSB[sz];
  v &= m;
  bool carry = false, ovf = false;
  bool x = c->sr & XF;
  for (unsigned i = 0; i < count; i++) {
    uint32_t before = v;
    if (left) {
      carry = v & top;
      v = (v << 1) & m;
      if (type == 2 && x) v |= 1;
      if (type == 3 && carry) v |= 1;
      if (type == 0 && ((before ^ v) & top)) ovf = true;
    } else {
      carry = v & 1;
      uint32_t in = 0;
      if (type == 0) in = v & top;
      if (type == 2 && x) in = top;
      if (type == 3 && carry) in = top;
      v = (v >> 1) | in;
    }
    if (type == 2) x = carry;
  }
  uint16_t sr = c->sr & ~(NF | ZF | VF | CF);
  if (v & top) sr |= NF;
  if (v == 0) sr |= ZF;
  if (ovf) sr |= VF;
  if (count == 0) {
    if (type == 2 && (c->sr & XF)) sr |= CF;
  } else {
    if (carry) sr |= CF;
    if (type != 3) sr = (uint16_t)((sr & ~XF) | (carry ? XF : 0));
  }
  c->sr = sr;
  return v;
}

/* ---- groups ---- */

static int bit_op(cpu_t *c, int type, uint32_t bit, int mode, int reg) {
  ea_t e;
  int sz = mode == 0 ? 2 : 0;
  if (!ea(c, mode, reg, sz, &e)) return M68K_ERR_OPCODE;
  bit &= sz == 2 ? 31u : 7u;
  uint32_t v = ea_read(c, &e, sz);
  c->sr = (uint16_t)((c->sr & ~ZF) | ((v >> bit) & 1 ? 0 : ZF));
  if (type == 0) return M68K_OK;
  if (type == 1) v ^= 1u << bit;
  if (type == 2) v &= ~(1u << bit);
  if (type == 3) v |= 1u << bit;
  ea_write(c, &e, sz, v);
  return M68K_OK;
}

static int group0(cpu_t *c, uint16_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  if ((op & 0x0138) == 0x0108) { /* MOVEP */
    int dn = (op >> 9) & 7, opmode = (op >> 6) & 7;
    uint32_t a = c->a[reg] + sext(fetch16(c), 1);
    int n = (opmode & 1) ? 4 : 2;
    if (opmode < 6) {
      uint32_t v = 0;
      for (int i = 0; i < n; i++) v = v << 8 | rb(c, a + 2u * (uint32_t)i);
      if (n == 2) c->d[dn] = (c->d[dn] & 0xFFFF0000u) | v;
      else c->d[dn] = v;
    } else {
      for (int i = 0; i < n; i++) {
        wb(c, a + 2u * (uint32_t)i, (uint8_t)(c->d[dn] >> (8 * (n - 1 - i))));
      }
    }
    return M68K_OK;
  }
  if (op & 0x0100) { /* BTST/BCHG/BCLR/BSET Dn,<ea> */
    return bit_op(c, (op >> 6) & 3, c->d[(op >> 9) & 7], mode, reg);
  }
  int kind = (op >> 9) & 7;
  if (kind == 4) { /* static bit op */
    uint32_t bit = fetch16(c) & 0xFFu;
    return bit_op(c, (op >> 6) & 3, bit, mode, reg);
  }
  int sz = (op >> 6) & 3;
  if (sz == 3) return M68K_ERR_OPCODE;
  if ((op & 0x3F) == 0x3C && (kind == 0 || kind == 1 || kind == 5)) {
    /* ORI/ANDI/EORI to CCR (byte) or SR (word) */
    uint16_t imm = fetch16(c);
    uint16_t m = sz == 0 ? 0x00FFu : 0xFFFFu;
    if (kind == 0) c->sr |= imm & m;
    if (kind == 1) c->sr = (uint16_t)(c->sr & (imm | (uint16_t)~m));
    if (kind == 5) c->sr ^= imm & m;
    return M68K_OK;
  }
  uint32_t imm = sz == 2 ? fetch32(c) : (fetch16(c) & MASK[sz]);
  ea_t e;
  if (!ea(c, mode, reg, sz, &e)) return M68K_ERR_OPCODE;
  uint32_t d = ea_read(c, &e, sz), r;
  switch (kind) {
    case 0: r = d | imm; flags_logic(c, r, sz); break;
    case 1: r = d & imm; flags_logic(c, r, sz); break;
    case 2: r = do_sub(c, d, imm, sz, false, false, true); break;
    case 3: r = do_add(c, d, imm, sz, false, false); break;
    case 5: r = d ^ imm; flags_logic(c, r, sz); break;
    case 6: do_sub(c, d, imm, sz, false, false, false); return M68K_OK;
    default: return M68K_ERR_OPCODE;
  }
  ea_write(c, &e, sz, r);
  return M68K_OK;
}

static int move(cpu_t *c, uint16_t op) {
  int sz = (op >> 12) == 1 ? 0 : (op >> 12) == 3 ? 1 : 2;
  ea_t s, d;
  if (!ea(c, (op >> 3) & 7, op & 7, sz, &s)) return M68K_ERR_OPCODE;
  uint32_t v = ea_read(c, &s, sz);
  int dmode = (op >> 6) & 7, dreg = (op >> 9) & 7;
  if (dmode == 1) { /* MOVEA */
    c->a[dreg] = sext(v, sz);
    return M68K_OK;
  }
  if (!ea(c, dmode, dreg, sz, &d)) return M68K_ERR_OPCODE;
  ea_write(c, &d, sz, v);
  flags_logic(c, v, sz);
  return M68K_OK;
}

static int movem(cpu_t *c, uint16_t op) {
  uint16_t list = fetch16(c);
  int sz = (op & 0x40) ? 2 : 1, mode = (op >> 3) & 7, reg = op & 7;
  bool to_regs = op & 0x0400;
  uint32_t step = BYTES[sz];
  if (mode == 4) { /* -(An): registers stored A7 down to D0 */
    uint32_t a = c->a[reg];
    for (int i = 15; i >= 0; i--) {
      if (!(list & (1u << (15 - i)))) continue;
      a -= step;
      uint32_t v = i < 8 ? c->d[i] : c->a[i - 8];
      mwrite(c, a, sz, v);
    }
    c->a[reg] = a;
    return M68K_OK;
  }
  uint32_t a;
  if (mode == 3) {
    a = c->a[reg];
  } else {
    ea_t e;
    if (mode < 2 || !ea(c, mode, reg, sz, &e) || e.kind != EA_MEM) {
      return M68K_ERR_OPCODE;
    }
    a = e.addr;
  }
  for (int i = 0; i < 16; i++) {
    if (!(list & (1u << i))) continue;
    if (to_regs) {
      uint32_t v = sext(mread(c, a, sz), sz);
      if (i < 8) c->d[i] = v;
      else c->a[i - 8] = v;
    } else {
      mwrite(c, a, sz, i < 8 ? c->d[i] : c->a[i - 8]);
    }
    a += step;
  }
  if (mode == 3) c->a[reg] = a;
  return M68K_OK;
}

static int group4(cpu_t *c, uint16_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  if (op == 0x4E75) { /* RTS */
    c->pc = pop32(c);
    return M68K_OK;
  }
  if (op == 0x4E71) return M68K_OK; /* NOP */
  if ((op & 0xFFF8) == 0x4E50) {      /* LINK */
    int32_t disp = (int16_t)fetch16(c);
    push32(c, c->a[reg]);
    c->a[reg] = c->a[7];
    c->a[7] += (uint32_t)disp;
    return M68K_OK;
  }
  if ((op & 0xFFF8) == 0x4E58) { /* UNLK */
    c->a[7] = c->a[reg];
    c->a[reg] = pop32(c);
    return M68K_OK;
  }
  if ((op & 0xFF80) == 0x4E80) { /* JSR / JMP */
    ea_t e;
    if (!ea(c, mode, reg, 2, &e) || e.kind != EA_MEM) return M68K_ERR_OPCODE;
    if (!(op & 0x40)) push32(c, c->pc);
    c->pc = e.addr;
    return M68K_OK;
  }
  if ((op & 0xF1C0) == 0x41C0) { /* LEA */
    ea_t e;
    if (!ea(c, mode, reg, 2, &e) || e.kind != EA_MEM) return M68K_ERR_OPCODE;
    c->a[(op >> 9) & 7] = e.addr;
    return M68K_OK;
  }
  if ((op & 0xFFF8) == 0x4840) { /* SWAP */
    uint32_t v = c->d[reg];
    v = v << 16 | v >> 16;
    c->d[reg] = v;
    flags_logic(c, v, 2);
    return M68K_OK;
  }
  if ((op & 0xFFC0) == 0x4840) { /* PEA */
    ea_t e;
    if (!ea(c, mode, reg, 2, &e) || e.kind != EA_MEM) return M68K_ERR_OPCODE;
    push32(c, e.addr);
    return M68K_OK;
  }
  if ((op & 0xFFB8) == 0x4880) { /* EXT */
    if (op & 0x40) {
      c->d[reg] = sext(c->d[reg] & 0xFFFFu, 1);
      flags_logic(c, c->d[reg], 2);
    } else {
      uint32_t w = sext(c->d[reg] & 0xFFu, 0) & 0xFFFFu;
      c->d[reg] = (c->d[reg] & 0xFFFF0000u) | w;
      flags_logic(c, w, 1);
    }
    return M68K_OK;
  }
  if ((op & 0xFB80) == 0x4880) return movem(c, op);
  if ((op & 0xFFC0) == 0x40C0) { /* MOVE from SR */
    ea_t e;
    if (!ea(c, mode, reg, 1, &e)) return M68K_ERR_OPCODE;
    ea_write(c, &e, 1, c->sr);
    return M68K_OK;
  }
  if ((op & 0xFFC0) == 0x44C0 || (op & 0xFFC0) == 0x46C0) { /* to CCR / SR */
    ea_t e;
    if (!ea(c, mode, reg, 1, &e)) return M68K_ERR_OPCODE;
    uint16_t v = (uint16_t)ea_read(c, &e, 1);
    c->sr = (op & 0x0200) ? v : (uint16_t)((c->sr & 0xFF00u) | (v & 0xFFu));
    return M68K_OK;
  }
  int sz = (op >> 6) & 3;
  if (sz == 3) return M68K_ERR_OPCODE;
  int kind = (op >> 8) & 0xF;
  ea_t e;
  if (kind != 0x0 && kind != 0x2 && kind != 0x4 && kind != 0x6 && kind != 0xA) {
    return M68K_ERR_OPCODE;
  }
  if (!ea(c, mode, reg, sz, &e)) return M68K_ERR_OPCODE;
  uint32_t v = ea_read(c, &e, sz);
  switch (kind) {
    case 0x0: /* NEGX */
      ea_write(c, &e, sz, do_sub(c, 0, v, sz, c->sr & XF, true, true));
      break;
    case 0x2: /* CLR */
      ea_write(c, &e, sz, 0);
      flags_logic(c, 0, sz);
      break;
    case 0x4: /* NEG */
      ea_write(c, &e, sz, do_sub(c, 0, v, sz, false, false, true));
      break;
    case 0x6: /* NOT */
      ea_write(c, &e, sz, ~v);
      flags_logic(c, ~v, sz);
      break;
    default: /* TST */
      flags_logic(c, v, sz);
      break;
  }
  return M68K_OK;
}

static int group5(cpu_t *c, uint16_t op) {
  int mode = (op >> 3) & 7, reg = op & 7, sz = (op >> 6) & 3;
  if (sz == 3) {
    int cc = (op >> 8) & 15;
    if (mode == 1) { /* DBcc */
      uint32_t base = c->pc;
      int32_t disp = (int16_t)fetch16(c);
      if (!cond(c, cc)) {
        uint16_t w = (uint16_t)(c->d[reg] - 1);
        c->d[reg] = (c->d[reg] & 0xFFFF0000u) | w;
        if (w != 0xFFFFu) c->pc = base + (uint32_t)disp;
      }
      return M68K_OK;
    }
    ea_t e; /* Scc */
    if (!ea(c, mode, reg, 0, &e)) return M68K_ERR_OPCODE;
    ea_write(c, &e, 0, cond(c, cc) ? 0xFF : 0);
    return M68K_OK;
  }
  uint32_t q = (op >> 9) & 7;
  if (q == 0) q = 8;
  ea_t e;
  if (!ea(c, mode, reg, sz, &e)) return M68K_ERR_OPCODE;
  if (e.kind == EA_A) { /* ADDQ/SUBQ to An: whole register, no flags */
    c->a[reg] += (op & 0x0100) ? (uint32_t)-q : q;
    return M68K_OK;
  }
  uint32_t v = ea_read(c, &e, sz);
  uint32_t r = (op & 0x0100) ? do_sub(c, v, q, sz, false, false, true)
                             : do_add(c, v, q, sz, false, false);
  ea_write(c, &e, sz, r);
  return M68K_OK;
}

static int branch(cpu_t *c, uint16_t op) {
  uint32_t base = c->pc;
  int32_t disp = (int8_t)(op & 0xFF);
  if (disp == 0) disp = (int16_t)fetch16(c);
  int cc = (op >> 8) & 15;
  if (cc == 1) { /* BSR */
    push32(c, c->pc);
    c->pc = base + (uint32_t)disp;
    return M68K_OK;
  }
  if (cond(c, cc)) c->pc = base + (uint32_t)disp;
  return M68K_OK;
}

/* OR, SUB, CMP/EOR, AND, ADD and their address, extended and multiply
 * forms (groups 8, 9, B, C, D). */
static int arith(cpu_t *c, uint16_t op) {
  int group = op >> 12, dn = (op >> 9) & 7, opmode = (op >> 6) & 7;
  int mode = (op >> 3) & 7, reg = op & 7;
  ea_t e;
  if (opmode == 3 || opmode == 7) {
    int sz = opmode == 3 ? 1 : 2;
    if (group == 0x8 || group == 0xC) { /* DIVU/DIVS, MULU/MULS (word) */
      if (!ea(c, mode, reg, 1, &e)) return M68K_ERR_OPCODE;
      uint32_t s = ea_read(c, &e, 1);
      bool sign = opmode == 7;
      if (group == 0xC) {
        uint32_t r = sign ? (uint32_t)((int32_t)(int16_t)s *
                                       (int32_t)(int16_t)(c->d[dn] & 0xFFFFu))
                          : s * (c->d[dn] & 0xFFFFu);
        c->d[dn] = r;
        flags_logic(c, r, 2);
        return M68K_OK;
      }
      if (s == 0) return M68K_ERR_OPCODE;
      if (sign) {
        int32_t q = (int32_t)c->d[dn] / (int16_t)s;
        int32_t rem = (int32_t)c->d[dn] % (int16_t)s;
        if (q < -32768 || q > 32767) {
          c->sr |= VF;
          return M68K_OK;
        }
        c->d[dn] = ((uint32_t)rem & 0xFFFFu) << 16 | ((uint32_t)q & 0xFFFFu);
        flags_logic(c, (uint32_t)q, 1);
      } else {
        uint32_t q = c->d[dn] / s, rem = c->d[dn] % s;
        if (q > 0xFFFFu) {
          c->sr |= VF;
          return M68K_OK;
        }
        c->d[dn] = rem << 16 | q;
        flags_logic(c, q, 1);
      }
      return M68K_OK;
    }
    /* ADDA / SUBA / CMPA */
    if (!ea(c, mode, reg, sz, &e)) return M68K_ERR_OPCODE;
    uint32_t s = sext(ea_read(c, &e, sz), sz);
    if (group == 0xD) c->a[dn] += s;
    else if (group == 0x9) c->a[dn] -= s;
    else if (group == 0xB) do_sub(c, c->a[dn], s, 2, false, false, false);
    else return M68K_ERR_OPCODE;
    return M68K_OK;
  }
  int sz = opmode & 3;
  bool to_ea = opmode & 4;
  if (to_ea && (mode == 0 || mode == 1)) {
    if (group == 0xD || group == 0x9) { /* ADDX / SUBX */
      uint32_t s, d;
      ea_t de;
      if (mode == 0) {
        s = c->d[reg] & MASK[sz];
        d = c->d[dn] & MASK[sz];
        de.kind = EA_D;
        de.reg = dn;
      } else {
        ea_t se;
        ea(c, 4, reg, sz, &se);
        s = ea_read(c, &se, sz);
        ea(c, 4, dn, sz, &de);
        d = ea_read(c, &de, sz);
      }
      uint32_t r = group == 0xD ? do_add(c, d, s, sz, c->sr & XF, true)
                                : do_sub(c, d, s, sz, c->sr & XF, true, true);
      ea_write(c, &de, sz, r);
      return M68K_OK;
    }
    if (group == 0xB && mode == 1) { /* CMPM (Ay)+,(Ax)+ */
      ea_t se, de;
      ea(c, 3, reg, sz, &se);
      ea(c, 3, dn, sz, &de);
      uint32_t s = ea_read(c, &se, sz), d = ea_read(c, &de, sz);
      do_sub(c, d, s, sz, false, false, false);
      return M68K_OK;
    }
    if (group == 0xC && (opmode == 5 || opmode == 6)) { /* EXG */
      uint32_t t;
      if (opmode == 5 && mode == 0) {
        t = c->d[dn]; c->d[dn] = c->d[reg]; c->d[reg] = t;
      } else if (opmode == 5) {
        t = c->a[dn]; c->a[dn] = c->a[reg]; c->a[reg] = t;
      } else {
        t = c->d[dn]; c->d[dn] = c->a[reg]; c->a[reg] = t;
      }
      return M68K_OK;
    }
    if (group != 0xB) return M68K_ERR_OPCODE; /* ABCD, SBCD */
  }
  if (!ea(c, mode, reg, sz, &e)) return M68K_ERR_OPCODE;
  uint32_t v = ea_read(c, &e, sz), d = c->d[dn] & MASK[sz], r;
  if (group == 0xB) {
    if (!to_ea) { /* CMP <ea>,Dn */
      do_sub(c, d, v, sz, false, false, false);
      return M68K_OK;
    }
    r = v ^ d; /* EOR Dn,<ea> */
    flags_logic(c, r, sz);
    ea_write(c, &e, sz, r);
    return M68K_OK;
  }
  uint32_t a = to_ea ? v : d, b = to_ea ? d : v;
  switch (group) {
    case 0x8: r = a | b; flags_logic(c, r, sz); break;
    case 0xC: r = a & b; flags_logic(c, r, sz); break;
    case 0xD: r = do_add(c, a, b, sz, false, false); break;
    default: r = do_sub(c, a, b, sz, false, false, true); break;
  }
  if (to_ea) {
    ea_write(c, &e, sz, r);
  } else {
    c->d[dn] = (c->d[dn] & ~MASK[sz]) | (r & MASK[sz]);
  }
  return M68K_OK;
}

static int groupE(cpu_t *c, uint16_t op) {
  int sz = (op >> 6) & 3;
  bool left = op & 0x0100;
  if (sz == 3) { /* memory, a word by one bit */
    ea_t e;
    if (!ea(c, (op >> 3) & 7, op & 7, 1, &e) || e.kind != EA_MEM) {
      return M68K_ERR_OPCODE;
    }
    uint32_t v = ea_read(c, &e, 1);
    ea_write(c, &e, 1, shift(c, v, 1, (op >> 9) & 3, left, 1));
    return M68K_OK;
  }
  int reg = op & 7, type = (op >> 3) & 3;
  unsigned count = (op >> 9) & 7;
  if (op & 0x20) count = c->d[count] & 63u;
  else if (count == 0) count = 8;
  uint32_t r = shift(c, c->d[reg], sz, type, left, count);
  c->d[reg] = (c->d[reg] & ~MASK[sz]) | r;
  return M68K_OK;
}

int m68k_step(m68k_t *c) {
  uint32_t at = c->pc;
  uint16_t op = fetch16(c);
  int e;
  switch (op >> 12) {
    case 0x0: e = group0(c, op); break;
    case 0x1:
    case 0x2:
    case 0x3: e = move(c, op); break;
    case 0x4: e = group4(c, op); break;
    case 0x5: e = group5(c, op); break;
    case 0x6: e = branch(c, op); break;
    case 0x7:
      if (op & 0x0100) {
        e = M68K_ERR_OPCODE;
        break;
      }
      c->d[(op >> 9) & 7] = sext(op & 0xFFu, 0);
      flags_logic(c, c->d[(op >> 9) & 7], 2);
      e = M68K_OK;
      break;
    case 0x8:
    case 0x9:
    case 0xB:
    case 0xC:
    case 0xD: e = arith(c, op); break;
    case 0xE: e = groupE(c, op); break;
    default: e = M68K_ERR_OPCODE; break;
  }
  if (e != M68K_OK) {
    c->error_pc = at;
    c->error_op = op;
  }
  return e;
}

int m68k_call(m68k_t *c, uint32_t entry, uint32_t max_steps) {
  push32(c, RETURN_SENTINEL);
  c->pc = entry;
  while (max_steps--) {
    if ((c->pc & 0xFFFFFFu) == RETURN_SENTINEL) return M68K_OK;
    int e = m68k_step(c);
    if (e != M68K_OK) return e;
  }
  return M68K_ERR_STEPS;
}
