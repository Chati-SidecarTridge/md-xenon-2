#!/usr/bin/env python3
"""LZHUF decoding as in LHarc's -lh1- method: LZSS over a 4,096-byte ring
buffer (match lengths 3-60) with adaptive Huffman coding of literals and
lengths, and a fixed partial code for the upper 6 bits of match positions.
The algorithm is Haruyasu Yoshizaki's LZHUF (1988, after Haruhiko Okumura's
LZSS); -lh1- starts the ring with a fixed pattern instead of spaces.

  lzhuf.py IMAGE OFFSET [-o OUT] [--length N]

decodes the stream at byte OFFSET of a flat disk image (see stx.py flat).
With no --length, the stream starts with its unpacked length as a 32-bit
little-endian word. Standard library only.
"""

from __future__ import annotations

import argparse
import struct
import sys

N = 4096           # ring buffer
F = 60             # longest match
THRESHOLD = 2      # a match is longer than this
N_CHAR = 256 - THRESHOLD + F   # literals and lengths: 314 symbols
T = N_CHAR * 2 - 1             # tree size
R = T - 1                      # root
MAX_FREQ = 0x8000

# Upper 6 bits of a match position: code and the bits it takes (LZHUF's
# d_code / d_len, built from runs).
D_CODE = []
D_LEN = []
for value_count, repeat, bits in ((1, 32, 3), (3, 16, 4), (8, 8, 5), (12, 4, 6), (24, 2, 7), (16, 1, 8)):
    for _ in range(value_count):
        D_CODE.extend([len(set(D_CODE))] * repeat)
        D_LEN.extend([bits] * repeat)
assert len(D_CODE) == 256 and D_CODE[-1] == 63


def lh1_ring() -> bytearray:
    """The ring's starting contents in -lh1-."""
    ring = bytearray()
    for v in range(256):
        ring += bytes([v]) * 13
    ring += bytes(range(256)) + bytes(range(255, -1, -1)) + bytes(128) + b" " * 128
    assert len(ring) == N
    return ring


class Bits:
    def __init__(self, data: bytes, pos: int = 0):
        self.data, self.pos, self.buf, self.n = data, pos, 0, 0

    def _fill(self) -> None:
        while self.n <= 8:
            b = self.data[self.pos] if self.pos < len(self.data) else 0
            self.pos += 1
            self.buf |= b << (8 - self.n)
            self.n += 8

    def bit(self) -> int:
        self._fill()
        v = (self.buf >> 15) & 1
        self.buf = (self.buf << 1) & 0xFFFF
        self.n -= 1
        return v

    def byte(self) -> int:
        self._fill()
        v = (self.buf >> 8) & 0xFF
        self.buf = (self.buf << 8) & 0xFFFF
        self.n -= 8
        return v


class Huffman:
    def __init__(self):
        self.freq = [0] * (T + 1)
        self.prnt = [0] * (T + N_CHAR)
        self.son = [0] * T
        for i in range(N_CHAR):
            self.freq[i] = 1
            self.son[i] = i + T
            self.prnt[i + T] = i
        i, j = 0, N_CHAR
        while j <= R:
            self.freq[j] = self.freq[i] + self.freq[i + 1]
            self.son[j] = i
            self.prnt[i] = self.prnt[i + 1] = j
            i += 2
            j += 1
        self.freq[T] = 0xFFFF
        self.prnt[R] = 0

    def reconst(self) -> None:
        freq, son, prnt = self.freq, self.son, self.prnt
        j = 0
        for i in range(T):
            if son[i] >= T:
                freq[j] = (freq[i] + 1) // 2
                son[j] = son[i]
                j += 1
        i, j = 0, N_CHAR
        while j < T:
            k = i + 1
            f = freq[j] = freq[i] + freq[k]
            k = j - 1
            while f < freq[k]:
                k -= 1
            k += 1
            freq[k + 1:j + 1] = freq[k:j]
            freq[k] = f
            son[k + 1:j + 1] = son[k:j]
            son[k] = i
            i += 2
            j += 1
        for i in range(T):
            k = son[i]
            if k >= T:
                prnt[k] = i
            else:
                prnt[k] = prnt[k + 1] = i

    def update(self, c: int) -> None:
        freq, son, prnt = self.freq, self.son, self.prnt
        if freq[R] == MAX_FREQ:
            self.reconst()
        c = prnt[c + T]
        while True:
            freq[c] += 1
            k = freq[c]
            l = c + 1
            if k > freq[l]:
                while k > freq[l + 1]:
                    l += 1
                freq[c] = freq[l]
                freq[l] = k
                i = son[c]
                prnt[i] = l
                if i < T:
                    prnt[i + 1] = l
                j = son[l]
                son[l] = i
                prnt[j] = c
                if j < T:
                    prnt[j + 1] = c
                son[c] = j
                c = l
            c = prnt[c]
            if c == 0:
                break

    def decode_char(self, bits: Bits) -> int:
        c = self.son[R]
        while c < T:
            c = self.son[c + bits.bit()]
        c -= T
        self.update(c)
        return c


def decode_position(bits: Bits) -> int:
    i = bits.byte()
    c = D_CODE[i] << 6
    for _ in range(D_LEN[i] - 2):
        i = (i << 1) + bits.bit()
    return c | (i & 0x3F)


def decode(data: bytes, pos: int, length: int) -> tuple[bytes, int]:
    """Unpack `length` bytes from `data` starting at byte `pos`; returns the
    bytes and the input position after the last byte consumed."""
    ring = lh1_ring()
    r = N - F
    h = Huffman()
    bits = Bits(data, pos)
    out = bytearray()
    while len(out) < length:
        c = h.decode_char(bits)
        if c < 256:
            out.append(c)
            ring[r] = c
            r = (r + 1) & (N - 1)
        else:
            i = (r - decode_position(bits) - 1) & (N - 1)
            for k in range(c - 255 + THRESHOLD):
                b = ring[(i + k) & (N - 1)]
                out.append(b)
                ring[r] = b
                r = (r + 1) & (N - 1)
    return bytes(out[:length]), bits.pos - (bits.n // 8)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image")
    ap.add_argument("offset", type=lambda s: int(s, 0))
    ap.add_argument("-o", "--out")
    ap.add_argument("--length", type=lambda s: int(s, 0))
    args = ap.parse_args()
    data = open(args.image, "rb").read()
    pos = args.offset
    length = args.length
    if length is None:
        (length,) = struct.unpack_from("<I", data, pos)
        pos += 4
    out, end = decode(data, pos, length)
    if args.out:
        open(args.out, "wb").write(out)
    print(f"{length} bytes unpacked from {end - pos} bytes at {args.offset:#x}; input ends near {end:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
