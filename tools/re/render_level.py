#!/usr/bin/env python3
"""Render a level's playfield the way the original draws it: the map's cells
(opaque tiles, masked tiles over the backdrop, or the backdrop alone) over a
two-plane backdrop that repeats every 192 lines, from the level's unpacked
data (tools/re/x2disk.py) and the three scroll positions the game keeps.

  render_level.py LEVEL.bin --map-offset N --fine F --backdrop ADDR -o OUT.png
  render_level.py LEVEL.bin ... --compare DUMP.bin
  render_level.py LEVEL.bin --strip -o STRIP.png [--waves]
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
half the map's speed). With --waves the strip carries the level's wave and
scenery tables (tools/re/waves.py): at each wave's trigger line a dashed
red line, and a red square where each member's path starts (the playfield's
top is the trigger line; starts off the playfield are drawn at its edge); a
yellow 32 x 32 box on each wall object's cells; a cyan cross at each
restart point.
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
import waves  # noqa: E402

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


RED, YELLOW, CYAN = 16, 17, 18            # colours added after the level's 16
MARKS = [0x0F00, 0x0FF0, 0x00FF]


def mark_waves(level: bytes, rows: list[bytes]) -> list[bytes]:
    """The strip's rows with the wave and scenery tables drawn over them."""
    pic = [bytearray(r) for r in rows]
    h, w = len(pic), len(pic[0])

    def dot(x: int, y: int, c: int) -> None:
        if 0 <= x < w and 0 <= y < h:
            pic[y][x] = c

    lv = waves.Level(level)
    paths = lv.paths()
    for _a, line, kind, count, path, spacing, _fire, _speed in lv.waves():
        for x in range(0, w, 4):
            dot(x, line, RED)
            dot(x + 1, line, RED)
        start = paths[path - 1]
        sx, sy = (lv.s(start + 2), lv.s(start + 4)) if lv.w(start) == 10 else (0, 0)
        for m in range(count):
            x = min(max(sx + (m * (spacing - 100) if spacing >= 100 else 0), 2), w - 3)
            y = min(max(line + sy, 2), h - 3)
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    dot(x + dx, y + dy, RED)
    for _a, _line, kind, _w4, _travel, x, y, _side in lv.scenery():
        if kind == 0:
            for d in range(-4, 5):
                dot(x + d, y, CYAN)
                dot(x, y + d, CYAN)
            continue
        for d in range(32):
            for px, py in ((x + d, y), (x + d, y + 31), (x, y + d), (x + 31, y + d)):
                dot(px, py, YELLOW)
    return [bytes(r) for r in pic]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("level", type=Path)
    ap.add_argument("--map-offset", type=lambda s: int(s, 0))
    ap.add_argument("--fine", type=int, default=0)
    ap.add_argument("--backdrop", type=lambda s: int(s, 0))
    ap.add_argument("--strip", action="store_true", help="the whole map as one picture")
    ap.add_argument("--waves", action="store_true", help="with --strip: mark the waves and scenery objects")
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
        rows = st_screen.to_indices(s, lines)
        pal = palette(level)
        if args.waves:
            rows, pal = mark_waves(level, rows), pal + MARKS
        st_screen.write_png(args.out, rows, pal)
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
