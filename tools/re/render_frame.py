#!/usr/bin/env python3
"""Render a whole frame of play the way the original composes it: the
playfield (render_level.py), the ship's masked sprite, and the status bar
(its picture, the scores, the lives digit and the shield gauge), from the
converted set's level and game files and the values a capture saved.

  render_frame.py LEVEL.X2 GAME.X2 --dump RUN/start-f1 [-o OUT.png]

--dump takes the files scripts/golden-start.exp saves for one frame:
PREFIX.bin the screen, .stars the 48 stars ($3D1DC, 6 bytes each), .vars the words from $CCE ($CD0 the world line, $CD8
the step just taken: the capture comes after the scroll's update), .ptr the
backdrop pointer,
.ship the ship's object ($3921C, 64 bytes), .game the variables from $C90
(score $C92, shown score $C96, shield $CC6, lives $CCA). The frame is
rendered from them and compared with the screen, line by line.

What the original does (tools/re listings, the game image):
- the stars (`$2B1A`): 48 entries (a screen offset of the pixel's 16-pixel
  group, its bit, a fraction of a line) in four groups of 12, colours 4, 5,
  6 and 7; a star is drawn only where the screen still shows colour 0, after
  the scenery and before the sprites. Each frame star i moves by the last
  scroll step x (384 + 8 i) / 384 lines, wrapping in the 192 lines;
- a masked sprite (draw routine $10A2): four header words (hotspot x,
  hotspot y, width, lines - 1), then each line, plane-major: plane 0 of
  every 16-pixel group, planes 1, 2, 3, then the mask (a set bit keeps the
  screen); drawn at (x - hotspot x, y - hotspot y), clipped to the 192
  playfield lines;
- the status bar: an 8-line picture at $2CA42; the score's 7 digits from
  the 8 x 8 four-plane font at $2C3A2 (32 bytes a digit) at x 24 (player 1)
  and x 240 (player 2), line 192; the lives digit from $2C1A2 + (lives + 59)
  x 32 at x 136, line 193 (its 8th line falls off the screen); the shield,
  39 one-pixel columns from x 91, lines 195-197, colour 14 up to the shield
  and 0 past it.
Standard library only; nothing of the game is in this file.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import render_level as rl  # noqa: E402
import st_screen  # noqa: E402

GAME_BASE = 0x400
SCREEN_LINES = 200
STATUS_PICTURE = 0x2CA42
SCORE_FONT = 0x2C3A2
LIVES_FONT = 0x2C1A2
SHIELD_FULL = 39


def game_at(game: bytes, addr: int, length: int) -> bytes:
    return game[addr - GAME_BASE:addr - GAME_BASE + length]


def put(screen: bytearray, x: int, y: int, colour: int) -> None:
    o = y * 160 + (x // 16) * 8
    bit = 0x8000 >> (x % 16)
    for p in range(4):
        w = struct.unpack_from(">H", screen, o + 2 * p)[0]
        w = (w | bit) if colour >> p & 1 else (w & ~bit)
        struct.pack_into(">H", screen, o + 2 * p, w & 0xFFFF)


def masked_sprite(screen: bytearray, game: bytes, addr: int, x: int, y: int,
                  lines_limit: int = rl.LINES) -> None:
    hx, hy, width, last = struct.unpack(">hhhh", game_at(game, addr, 8))
    groups = (width + 15) // 16
    o = addr + 8
    for line in range(last + 1):
        words = struct.unpack(">%dH" % (5 * groups), game_at(game, o, 10 * groups))
        o += 10 * groups
        sy = y - hy + line
        for g in range(groups):
            planes = [words[k * groups + g] for k in range(5)]
            for b in range(16):
                sx = x - hx + g * 16 + b
                m = 0x8000 >> b
                if planes[4] & m or not 0 <= sy < lines_limit or not 0 <= sx < 320:
                    continue
                put(screen, sx, sy, sum(1 << k for k in range(4) if planes[k] & m))


def stars(screen: bytearray, data: bytes) -> None:
    for i in range(48):
        offset, bit, _ = struct.unpack_from(">hHh", data, 6 * i)
        colour = 4 | (i // 12)
        words = struct.unpack_from(">4H", screen, offset)
        if (words[0] | words[1] | words[2] | words[3]) & bit:
            continue
        y, x = offset // 160, (offset % 160) // 8 * 16 + (15 - bit.bit_length() + 1)
        put(screen, x, y, colour)


def char8(screen: bytearray, glyph: bytes, x: int, y: int) -> None:
    """An 8 x 8 four-plane character as movep writes it: a byte a plane."""
    for line in range(8):
        if y + line >= SCREEN_LINES:
            break
        planes = glyph[line * 4:line * 4 + 4]
        for b in range(8):
            put(screen, x + b, y + line,
                sum(1 << p for p in range(4) if planes[p] & (0x80 >> b)))


def status_bar(screen: bytearray, game: bytes, score: int, lives: int,
               shield: int, score2: int = 0) -> None:
    screen[192 * 160:] = game_at(game, STATUS_PICTURE, 8 * 160)
    for value, x0 in ((score, 24), (score2, 240)):
        digits = "%07d" % value
        for i, d in enumerate(digits):
            char8(screen, game_at(game, SCORE_FONT + int(d) * 32, 32), x0 + 8 * i, 192)
    char8(screen, game_at(game, LIVES_FONT + (min(lives, 9) + 59) * 32, 32), 136, 193)
    for i in range(SHIELD_FULL):
        for line in range(195, 198):
            put(screen, 91 + i, line, 14 if i < shield else 0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("level", type=Path)
    ap.add_argument("game", type=Path)
    ap.add_argument("--dump", required=True, help="PREFIX of a golden-start capture")
    ap.add_argument("-o", "--out", type=Path)
    args = ap.parse_args()
    level, game = args.level.read_bytes(), args.game.read_bytes()
    p = args.dump
    vars_ = Path(p + ".vars").read_bytes()
    ptr = struct.unpack(">I", Path(p + ".ptr").read_bytes()[:4])[0]
    ship = Path(p + ".ship").read_bytes()
    gv = Path(p + ".game").read_bytes()
    word = lambda a: struct.unpack_from(">h", gv, a - 0xC90)[0]
    shown = struct.unpack_from(">I", gv, 0xC96 - 0xC90)[0]

    # The capture runs as the frame ends, after the scroll's update: the
    # screen shows the world line before it ($CD0 + the step $CD8).
    line = struct.unpack_from(">h", vars_, 2)[0] + struct.unpack_from(">h", vars_, 10)[0]
    screen = bytearray(32000)
    screen[:rl.LINES * rl.LINE_BYTES] = rl.render(
        level, line // rl.CELL * rl.COLS * 2, line % rl.CELL, ptr)
    stars(screen, Path(p + ".stars").read_bytes())
    sx, sy = struct.unpack_from(">hh", ship, 32)[0], struct.unpack_from(">h", ship, 36)[0]
    masked_sprite(screen, game, struct.unpack_from(">I", ship, 22)[0], sx, sy)
    status_bar(screen, game, shown, word(0xCCA), word(0xCC6))

    if args.out:
        st_screen.write_png(args.out, st_screen.to_indices(bytes(screen), SCREEN_LINES),
                            rl.palette(level))
        print(f"wrote {args.out}")
    dump = Path(p + ".bin").read_bytes()
    bad = [y for y in range(SCREEN_LINES)
           if screen[y * 160:(y + 1) * 160] != dump[y * 160:(y + 1) * 160]]
    n = sum(a != b for a, b in zip(screen, dump))
    if not bad:
        print("MATCH: 0 of 32000 bytes differ")
        return 0
    print(f"DIFFER: {n} of 32000 bytes differ, in lines {bad[:12]}{'...' if len(bad) > 12 else ''}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
