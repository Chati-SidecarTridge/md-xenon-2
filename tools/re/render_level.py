#!/usr/bin/env python3
"""Render a level's playfield the way the original draws it: the map's cells
(opaque tiles, masked tiles over the backdrop, or the backdrop alone) over a
two-plane backdrop that repeats every 192 lines, from the level's unpacked
data (tools/re/x2disk.py) and the three scroll positions the game keeps.

  render_level.py LEVEL.bin --map-offset N --fine F --backdrop ADDR -o OUT.png
  render_level.py LEVEL.bin ... --compare DUMP.bin
  render_level.py LEVEL.bin --strip -o STRIP.png
  render_level.py LEVEL.bin --dump RUN/scenery-f600 [-o OUT.png]

LEVEL.bin is a level as unpacked at $4F000. --map-offset is the byte offset
into the map of the playfield's top row of cells, --fine the first line of
that row that shows (0-15), --backdrop the address the backdrop is read
from for the top line. Out: the 192 playfield lines, as ST screen memory
(planar, 160 bytes a line) rendered to PNG with the level's palette, or
compared byte for byte with the same lines of a screen dump. --strip draws
the whole map as one tall picture (the level from its start at the bottom
to its end at the top) over the backdrop, repeating every 192 lines down the
picture: for looking at, not for comparing (in play the backdrop moves at
half the map's speed).
--dump takes the files scripts/golden-scenery.exp saves for one frame
(PREFIX.bin the screen, PREFIX.vars the words from $CCE, PREFIX.ptr the
backdrop pointer at $436, PREFIX.map the map as it was in RAM): the scroll
positions are read from them and the render compared with the screen. The
game rewrites map words while the level plays (scenery objects change their
tiles), so the captured map replaces the level's own when there is one.

The level's header (its first longs) gives the backdrop (+$00), the map
(+$04), the tile block (+$08) and the palette (+$1C). The map is 20 words a
row: 0 shows the backdrop; positive, an opaque tile (4 planes, 8 bytes a
line) at the tile block + word * 16; negative, a masked tile (a mask for
planes 0-1, planes 0-1, planes 2-3: 12 bytes a line) at the tile block +
(word & $7FFF) * 16, laid over the backdrop's planes 0-1. The backdrop is
stored as 20 columns of 192 lines of planes 0-1 (4 bytes a line); the game
doubles each column (384 lines) and reads from a pointer into the first.
Standard library only; nothing of the game is in this file.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import st_screen  # noqa: E402

BASE = 0x4F000
COLS = 20
CELL = 16
LINES = 192
LINE_BYTES = 160
BACKDROP_LINES = 192
COLUMN_BYTES = BACKDROP_LINES * 4


def header(level: bytes, offset: int) -> int:
    return struct.unpack_from(">I", level, offset)[0]


def palette(level: bytes) -> list[int]:
    return list(struct.unpack_from(">16H", level, header(level, 0x1C) - BASE))


def render(level: bytes, map_offset: int, fine: int, backdrop: int) -> bytes:
    bd, mp, tiles = header(level, 0) - BASE, header(level, 4) - BASE, header(level, 8) - BASE
    # The pointer reads the doubled columns (1,536 bytes each) from the
    # stored ones' start: its line in a column.
    start = (backdrop - header(level, 0)) // 4
    out = bytearray(LINES * LINE_BYTES)
    for y in range(LINES):
        cell_row = (y + fine) // CELL      # row of cells, from the top one
        tile_line = (y + fine) % CELL
        b_line = (start + y) % BACKDROP_LINES
        for c in range(COLS):
            w = struct.unpack_from(">H", level, mp + map_offset + 2 * (cell_row * COLS + c))[0]
            bd01 = level[bd + c * COLUMN_BYTES + b_line * 4: bd + c * COLUMN_BYTES + b_line * 4 + 4]
            if w == 0:
                p01, p23 = bd01, bytes(4)
            elif not w & 0x8000:
                t = tiles + w * 16 + tile_line * 8
                p01, p23 = level[t:t + 4], level[t + 4:t + 8]
            else:
                t = tiles + (w & 0x7FFF) * 16 + tile_line * 12
                mask = int.from_bytes(level[t:t + 4], "big")
                d01 = int.from_bytes(level[t + 4:t + 8], "big")
                p01 = ((mask & int.from_bytes(bd01, "big")) | d01).to_bytes(4, "big")
                p23 = level[t + 8:t + 12]
            o = y * LINE_BYTES + c * 8
            out[o:o + 4] = p01
            out[o + 4:o + 8] = p23
    return bytes(out)


def map_rows(level: bytes) -> int:
    """The map's rows: from header +$04 to the next block the header names."""
    mp = header(level, 4)
    above = [header(level, o) for o in range(0, 0x3C, 4)]
    above = [a for a in above if mp < a < BASE + len(level)]
    return (min(above) - mp) // (COLS * 2)


def strip(level: bytes) -> bytes:
    rows = map_rows(level)
    out = bytearray()
    for r in range(rows):
        cells = render_rows(level, r)
        out += cells
    return bytes(out)


def render_rows(level: bytes, row: int) -> bytes:
    """One row of cells (16 lines), the backdrop continuing down the strip."""
    line = row * CELL % 192
    one = render(level, row * COLS * 2, 0, header(level, 0) + 4 * line)
    return one[:CELL * LINE_BYTES]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("level", type=Path)
    ap.add_argument("--map-offset", type=lambda s: int(s, 0))
    ap.add_argument("--fine", type=int, default=0)
    ap.add_argument("--backdrop", type=lambda s: int(s, 0))
    ap.add_argument("--strip", action="store_true", help="the whole map as one picture")
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("--compare", type=Path, help="a screen dump to compare the 192 lines with")
    ap.add_argument("--dump", help="PREFIX of a golden-scenery capture: .bin, .vars, .ptr")
    args = ap.parse_args()
    level = args.level.read_bytes()
    if args.strip:
        if not args.out:
            ap.error("--strip needs -o")
        s = strip(level)
        lines = len(s) // LINE_BYTES
        st_screen.write_png(args.out, st_screen.to_indices(s, lines), palette(level))
        print(f"wrote {args.out} ({lines} lines, {map_rows(level)} rows)")
        return 0
    if args.dump:
        vars_ = Path(args.dump + ".vars").read_bytes()
        args.map_offset = struct.unpack_from(">H", vars_, 0)[0]
        args.fine = struct.unpack_from(">H", vars_, 8)[0]
        args.backdrop = struct.unpack(">I", Path(args.dump + ".ptr").read_bytes()[:4])[0]
        args.compare = Path(args.dump + ".bin")
        if Path(args.dump + ".map").exists():
            ram = Path(args.dump + ".map").read_bytes()
            mp = header(level, 4) - BASE
            changed = sum(ram[i:i + 2] != level[mp + i:mp + i + 2] for i in range(0, len(ram), 2))
            level = level[:mp] + ram + level[mp + len(ram):]
            print(f"map from RAM: {changed} words differ from the level's")
        print(f"map offset {args.map_offset}, fine {args.fine}, backdrop ${args.backdrop:X}")
    if args.map_offset is None or args.backdrop is None:
        ap.error("--map-offset and --backdrop are needed (or --strip)")
    pf = render(level, args.map_offset, args.fine, args.backdrop)
    if args.out:
        st_screen.write_png(args.out, st_screen.to_indices(pf + bytes(8 * LINE_BYTES))[:LINES], palette(level))
        print(f"wrote {args.out}")
    if args.compare:
        dump = args.compare.read_bytes()[:LINES * LINE_BYTES]
        bad = [i for i in range(len(pf)) if pf[i] != dump[i]]
        lines = sorted({i // LINE_BYTES for i in bad})
        print(("MATCH" if not bad else "DIFFER") + f": {len(bad)} of {len(pf)} bytes differ"
              + (f", in lines {lines[:8]}{'...' if len(lines) > 8 else ''}" if bad else ""))
        return 0 if not bad else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
