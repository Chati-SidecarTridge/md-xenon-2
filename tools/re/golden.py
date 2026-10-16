#!/usr/bin/env python3
"""Golden frames: the original's screen at a named state, as a PNG, for the
port's frames to be compared with.

  golden.py RUN NAME [-o OUT.png] [--check SHOT.png]

reads NAME.bin (32,000 bytes of screen memory) and NAME.pal (the 16 colour
registers, 32 bytes) from the run folder RUN, as a `scripts/golden-*.ini`
command file saves them, and writes the screen as a 320x200 indexed PNG
(default RUN/NAME.png). With --check it compares the result, pixel for pixel,
with a Hatari screenshot of the same screen: Hatari's PNG has the borders
and doubles every pixel, so the 640x400 picture is found inside it and every
second pixel is read; since Hatari's RGB scale differs from ours, colours
are compared through the mapping between the two, which must be one to one
(palette entries with the same colour word count as one colour).
Standard library only.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import st_screen  # noqa: E402


def load(run: Path, name: str) -> tuple[list[bytes], list[int]]:
    rows = st_screen.to_indices((run / f"{name}.bin").read_bytes())
    palette = list(struct.unpack(">16H", (run / f"{name}.pal").read_bytes()[:32]))
    return rows, palette


def check(rows: list[bytes], palette: list[int], shot: Path) -> tuple[bool, str]:
    w, h, rgb = st_screen.read_png(shot)
    # Indices with the same colour word are the same colour on screen.
    canon = {i: palette.index(palette[i]) for i in range(16)}
    rows = [bytes(canon[v] for v in row) for row in rows]
    best = None
    # Find the 640x400 picture inside the borders: the offset where the
    # index -> colour mapping is consistent over a sample of pixels.
    for oy in range(0, h - 399, 2):
        for ox in range(0, w - 639, 2):
            mapping, ok = {}, True
            for y in range(0, 200, 7):
                for x in range(0, 320, 5):
                    c = rgb[oy + 2 * y][ox + 2 * x]
                    if mapping.setdefault(rows[y][x], c) != c:
                        ok = False
                        break
                if not ok:
                    break
            if ok:
                best = (ox, oy)
                break
        if best:
            break
    if best is None:
        return False, "no placement of the screen inside the screenshot matches"
    ox, oy = best
    fwd, back, bad = {}, {}, 0
    for y in range(200):
        for x in range(320):
            c = rgb[oy + 2 * y][ox + 2 * x]
            i = rows[y][x]
            if fwd.setdefault(i, c) != c or back.setdefault(c, i) != i:
                bad += 1
    return bad == 0, f"screen at ({ox},{oy}) in the screenshot; {bad} pixels differ; {len(fwd)} colours used"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run", type=Path)
    ap.add_argument("name")
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("--check", type=Path, help="a Hatari screenshot of the same screen")
    args = ap.parse_args()
    rows, palette = load(args.run, args.name)
    out = args.out or args.run / f"{args.name}.png"
    st_screen.write_png(out, rows, palette)
    print(f"{out}: palette {' '.join(f'{c:03x}' for c in palette)}")
    if args.check:
        ok, text = check(rows, palette, args.check)
        print(("MATCH " if ok else "DIFFER ") + text)
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
