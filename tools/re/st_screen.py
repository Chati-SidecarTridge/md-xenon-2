#!/usr/bin/env python3
"""Atari ST low-resolution screens: planar memory to palette indices, and
indices plus an ST palette to a PNG.

  st_screen.py SCREEN.bin PALETTE -o OUT.png [--lines N] [--scale S]

SCREEN.bin is screen memory as the shifter reads it (160 bytes a line: 20
groups of four 16-bit plane words, plane 0 first); PALETTE is the 16 colour
registers as hex words ("0000 0777 ...", as Hatari's `m w $ff8240 16`
prints them) or a file holding them. ST colours are 0RRR0GGG0BBB; STE
colours add a fourth, low bit per gun in bit 3 of each nibble. Standard
library only.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

WIDTH = 320
LINE_BYTES = 160


def to_indices(screen: bytes, lines: int = 200) -> list[bytes]:
    """One bytes object of 320 palette indices per line."""
    rows = []
    for y in range(lines):
        row = bytearray(WIDTH)
        base = y * LINE_BYTES
        for g in range(20):
            p0, p1, p2, p3 = struct.unpack_from(">4H", screen, base + g * 8)
            for bit in range(16):
                m = 0x8000 >> bit
                row[g * 16 + bit] = (bool(p0 & m) | (bool(p1 & m) << 1)
                                     | (bool(p2 & m) << 2) | (bool(p3 & m) << 3))
        rows.append(bytes(row))
    return rows


def st_rgb(word: int) -> tuple[int, int, int]:
    """An ST/STE colour word to 8-bit RGB (the STE's low bit is bit 3)."""
    out = []
    for shift in (8, 4, 0):
        n = (word >> shift) & 0xF
        v = ((n & 7) << 1) | (n >> 3)          # 4-bit STE value
        out.append(v * 17)
    return tuple(out)


def parse_palette(text: str) -> list[int]:
    words = [int(w, 16) for w in re.findall(r"\b[0-9a-fA-F]{4}\b", text.split(":", 1)[-1])]
    if len(words) < 16:
        raise ValueError("a palette needs 16 colour words")
    return words[:16]


def write_png(path: str | Path, rows: list[bytes], palette: list[int], scale: int = 1) -> None:
    """An indexed PNG (8-bit, the 16 colours) of the rows."""
    h, w = len(rows) * scale, len(rows[0]) * scale
    raw = bytearray()
    for row in rows:
        line = bytes(b for b in row for _ in range(scale))
        for _ in range(scale):
            raw += b"\0" + line
    plte = b"".join(bytes(st_rgb(c)) for c in palette)

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
           + chunk(b"PLTE", plte) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    Path(path).write_bytes(png)


def read_png(path: str | Path) -> tuple[int, int, list[list[tuple[int, int, int]]]]:
    """Width, height and RGB rows of an 8-bit, non-interlaced PNG (RGB, RGBA,
    grey or indexed): enough for Hatari's screenshots."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, plte = 8, b"", b""
    while pos < len(data):
        (n,) = struct.unpack_from(">I", data, pos)
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if depth != 8 or interlace:
        raise ValueError("only 8-bit, non-interlaced PNGs")
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if ctype == 3:
            rows.append([tuple(plte[3 * v:3 * v + 3]) for v in line])
        elif ctype in (0, 4):
            rows.append([(v, v, v) for v in line[::bpp]])
        else:
            rows.append([tuple(line[x:x + 3]) for x in range(0, stride, bpp)])
    return w, h, rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("screen")
    ap.add_argument("palette", help="16 hex words, or a file holding them")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--lines", type=int, default=200)
    ap.add_argument("--scale", type=int, default=1)
    args = ap.parse_args()
    text = Path(args.palette).read_text() if Path(args.palette).is_file() else args.palette
    rows = to_indices(Path(args.screen).read_bytes(), args.lines)
    write_png(args.out, rows, parse_palette(text), args.scale)
    return 0


if __name__ == "__main__":
    sys.exit(main())
