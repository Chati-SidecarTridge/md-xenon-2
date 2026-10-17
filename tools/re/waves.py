#!/usr/bin/env python3
"""Decode a level's enemy waves, scenery objects and paths, as the original's
object routine reads them, and step a path as its enemies move.

  waves.py LEVEL.bin                       the tables
  waves.py LEVEL.bin --path N --game GAME.bin [--speed S] [--frames F]
  waves.py LEVEL.bin --check RUN/hatari.out
  waves.py LEVEL.bin --check-paths RUN/hatari.out --game GAME.bin

LEVEL.bin is a level as unpacked at $4F000 (tools/re/x2disk.py), GAME.bin
the game image as loaded at $400.

The level's header names two tables of 14-byte records (+$38: the waves,
+$14: the scenery objects), each record starting with the world line that
triggers it; a negative word ends a table. A wave record: line, kind (0 a
capsule, 1-10 one of the level's enemy types), count, path (1-based),
spacing (below 100 a delay in path steps between members, else value - 100
pixels across), firing (0 none, 1-99 a random direction at that rate, 100+
aimed), and the steps a frame (a capsule's token for kind 0). A scenery
record: line, kind (0 a restart point, 1-3 a wall object in the map, 4 a
wall crawler), a word, the crawler's travel, x, the world line of the
object's top-left cell, the side (0 left, 1 right).

The enemy types are 24-byte records at the addresses the level's wave
handler (header +$10) loads for kinds 1-10: type, worth, update, draw, hit,
animation, a word, two flags (big, chained); a non-zero word after a record
starts another part of the same type.

A path (header +$30: the pointers, +$34 to +$38: the data) is word opcodes:
0 end; 2 an arc (direction 0-255, turn rate, turn acceleration, steps);
4 wait (steps); 6 one of eight offsets at random; 8 go to the path's start
+ offset; 10 the start position x, y. A step moves the object by the
game's 256-entry sine table (y by sin[dir], x by sin[dir + 64], / 64
pixels) in 16.16 fixed point, then turns the direction (8.8) by the rate
(1/256 a step) and the rate by the acceleration (1/65536 a step).

--check reads a run of scripts/waves-run.exp and checks that every wave
and scenery record the original fired is one of the table's, on the frame
its line was passed. --check-paths reads a run of scripts/waves-paths.exp
(every enemy's position after its path step, and the wave records fired)
and steps each wave's members from the frame after their record fired (a
trail's members a delay apart, a row's members apart in x; a capsule at 3
steps a frame): each must match a logged object on every frame it was
logged. Standard library only; nothing of the game is in this
file.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

BASE = 0x4F000
GAME_BASE = 0x400
SINE = 0x9C80
RECORD = 14
TYPE_RECORD = 24


class Level:
    def __init__(self, data: bytes) -> None:
        self.data = data

    def w(self, addr: int) -> int:
        return struct.unpack_from(">H", self.data, addr - BASE)[0]

    def s(self, addr: int) -> int:
        return struct.unpack_from(">h", self.data, addr - BASE)[0]

    def l(self, addr: int) -> int:
        return struct.unpack_from(">I", self.data, addr - BASE)[0]

    def table(self, header: int) -> list[tuple[int, ...]]:
        out, a = [], self.l(BASE + header)
        while not self.w(a) & 0x8000:
            out.append((a,) + struct.unpack_from(">7h", self.data, a - BASE))
            a += RECORD
        return out

    def waves(self) -> list[tuple[int, ...]]:
        return self.table(0x38)

    def scenery(self) -> list[tuple[int, ...]]:
        return self.table(0x14)

    def type_addresses(self) -> list[int]:
        """The type record each wave kind 1-10 loads. The wave handler (the
        bra.w at header +$10) is `add d0,d0` twice, `movea.l (pc,d0.w),a2`,
        `jmp (a2)`, then a table of 11 entries; kinds 1-10 point at a
        `lea abs.l,a2` followed by a jump to the game's spawner."""
        table = BASE + 0x12 + self.s(BASE + 0x12) + 10
        out = []
        for k in range(1, 11):
            entry = self.l(table + 4 * k)
            if self.w(entry) != 0x45F9:
                break
            out.append(self.l(entry + 2))
        return out

    def types(self) -> list[list[tuple[int, ...]]]:
        out = []
        for a in self.type_addresses():
            parts = []
            while True:
                t, worth, upd, draw, hit, anim, w20, flags = struct.unpack_from(
                    ">HHIIIIHH", self.data, a - BASE)
                parts.append((t, worth, upd, draw, hit, anim, w20, flags >> 8, flags & 0xFF))
                a += TYPE_RECORD
                if self.w(a) == 0:
                    break
            out.append(parts)
        return out

    def paths(self) -> list[int]:
        a, end = self.l(BASE + 0x30), self.l(BASE + 0x34)
        return [self.l(p) for p in range(a, end, 4)]

    def path_ops(self, addr: int) -> list[tuple]:
        ops, a = [], addr
        for _ in range(200):
            op = self.w(a)
            if op == 0:
                ops.append(("end",))
                break
            if op == 2:
                ops.append(("arc", self.w(a + 2), self.s(a + 4), self.s(a + 6), self.w(a + 8)))
                a += 10
            elif op == 4:
                ops.append(("wait", self.w(a + 2)))
                a += 4
            elif op == 6:
                ops.append(("random", [self.s(a + 2 + 2 * i) for i in range(8)]))
                break
            elif op == 8:
                ops.append(("goto", self.w(a + 2)))
                break
            elif op == 10:
                ops.append(("at", self.s(a + 2), self.s(a + 4)))
                a += 6
            else:
                ops.append(("?", op))
                break
        return ops


def sine_table(game: bytes) -> list[int]:
    raw = game[SINE - GAME_BASE:SINE - GAME_BASE + 320]
    return [b - 256 if b > 127 else b for b in raw]


def s32(v: int) -> int:
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


class Mover:
    """One enemy on its path, as the game's $9ACA steps it: x and y in
    16.16, the direction an 8-bit value with a 16-bit fraction, the turn
    rate in 1/65536 of a direction step (the arc's word x 256), the
    acceleration added to the rate every step."""

    def __init__(self, level: Level, sine: list[int], path: int, speed: int,
                 delay: int = 0, dx: int = 0) -> None:
        self.level, self.sine, self.start = level, sine, path
        self.pc = path
        self.x = dx << 16
        self.y = 0
        if level.w(path) == 10:
            self.x, self.y = (level.s(path + 2) + dx) << 16, level.s(path + 4) << 16
            self.pc += 6
        self.dir = self.frac = self.rate = self.acc = 0
        self.count = -delay    # +40: steps left in the arc; negative, a wait
        self.speed = speed     # +84: steps a frame
        self.done = False

    def pos(self) -> tuple[int, int]:
        return self.x >> 16, self.y >> 16

    def steps(self, d7: int) -> int:
        """Steps along the current arc: the steps left this frame when the
        arc ends, or -1 when the frame's steps run out first."""
        while True:
            d7 -= 1
            if d7 < 0:
                return -1
            self.count -= 1
            if self.count < 0:
                return d7 + 1
            self.y = s32(self.y + (self.sine[self.dir] << 10))
            self.x = s32(self.x + (self.sine[self.dir + 64] << 10))
            turned = ((self.dir << 16 | self.frac) + self.rate) & 0xFFFFFFFF
            self.dir, self.frac = (turned >> 16) & 0xFF, turned & 0xFFFF
            self.rate = s32(self.rate + self.acc)

    def frame(self) -> None:
        if self.done or self.speed == 0:
            return
        d7 = self.speed
        if self.count < 0:                         # waiting
            d7 += self.count
            if d7 <= 0:
                self.count = d7
                return
        elif self.count > 0:
            d7 = self.steps(d7)
            if d7 < 0:
                return
        lv = self.level
        while True:                                # the next opcode
            op = lv.w(self.pc)
            if op == 2:
                self.dir, self.frac = lv.w(self.pc + 2) & 0xFF, 0
                self.rate = lv.s(self.pc + 4) << 8
                self.acc = lv.s(self.pc + 6)
                self.count = lv.w(self.pc + 8)
                self.pc += 10
                d7 = self.steps(d7)
                if d7 < 0:
                    return
            elif op == 4:
                d7 -= lv.w(self.pc + 2)
                self.pc += 4
                if d7 <= 0:
                    self.count = d7
                    return
            elif op == 8:
                self.pc = self.start + lv.w(self.pc + 2)
            elif op == 10:
                self.pc += 6
            else:                                  # 0 the end; 6 (random) not modelled
                self.done = True
                return


def check(level: Level, out: Path) -> int:
    """Every record a waves-run fired against the tables: the record must be
    one of them, of its kind, and the world line when it fired at or above
    its line. A run that went on into the next level stops at the frame
    counter's restart."""
    tables = {0xBBB1: {r[0]: r for r in level.waves()},
              0xBBB2: {r[0]: r for r in level.scenery()}}
    values = hatari_values(out)
    fired = bad = 0
    last_frame = 0
    for i, mark in enumerate(values):
        if mark not in tables or i + 4 >= len(values):
            continue
        frame, line, a1, d0 = values[i + 1:i + 5]
        if frame + 100 < last_frame:
            print(f"the frame counter restarted ({last_frame} -> {frame}): the next level, stop")
            break
        last_frame = frame
        rec = tables[mark].get(a1)
        ok = rec is not None and line <= rec[1] and rec[2] == d0
        fired += 1
        bad += not ok
        print(f"{'wave   ' if mark == 0xBBB1 else 'scenery'} frame {frame:5} line {line:5} "
              f"record ${a1:X} kind {d0}"
              + (f" (its line {rec[1]})" if rec else " NOT IN THE TABLE") + ("" if ok else "  <-- !"))
    print(f"{fired} records fired, {bad} not matching")
    return 0 if fired and not bad else 1


def hatari_values(out: Path) -> list[int]:
    """Every value a run's debugger printed, in order: Hatari prints each
    as "= %<bin> (bin), #<dec> (dec), $<hex> (hex)"."""
    hexval = re.compile(r"^= %.*\$([0-9a-f]+) \(hex\)")
    lines = out.read_text(errors="replace").splitlines()
    return [int(m.group(1), 16) for m in map(hexval.match, lines) if m]


def check_paths(level: Level, sine: list[int], out: Path) -> int:
    def s16(v: int) -> int:
        return v - 0x10000 if v & 0x8000 else v

    values = hatari_values(out)
    logged: dict[int, dict[int, tuple[int, int]]] = {}   # frame -> object, type -> x, y
    fired = []
    i = 0
    while i < len(values):
        if values[i] == 0xBBB7 and i + 5 < len(values):
            frame, obj, kind, x, y = values[i + 1:i + 6]
            # an object's slot is reused once it dies: the type tells them apart
            logged.setdefault(frame, {})[obj << 16 | kind] = (s16(x), s16(y))
            i += 6
        elif values[i] == 0xBBB1 and i + 4 < len(values):
            fired.append((values[i + 1], values[i + 3]))
            i += 5
        else:
            i += 1
    last = max(logged, default=0)
    records = {r[0]: r for r in level.waves()}
    members = good = 0
    for frame, addr in fired:
        if frame >= last or addr not in records:
            continue
        _, _line, kind, count, path, spacing, _fire, speed = records[addr]
        for m in range(count):
            mv = Mover(level, sine, level.paths()[path - 1], 3 if kind == 0 else speed,
                       m * spacing if spacing < 100 else 0,
                       m * (spacing - 100) if spacing >= 100 else 0)
            sim = []
            for f in range(frame + 1, last + 1):
                mv.frame()
                if mv.done:
                    break
                sim.append((f, mv.pos()))
            best = (0, 0)
            for obj in {o for f, _ in sim for o in logged.get(f, {})}:
                seen = [(logged[f][obj] == p) for f, p in sim if obj in logged.get(f, {})]
                if sum(seen) > best[0]:
                    best = (sum(seen), len(seen))
            members += 1
            good += best[0] > 0 and best[0] == best[1]
            print(f"record ${addr:X} member {m}: {best[0]} of {best[1]} logged frames match")
    print(f"{good} of {members} members match on every frame they were logged")
    return 0 if members and good == members else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("level", type=Path)
    ap.add_argument("--game", type=Path, help="the game image (for --path)")
    ap.add_argument("--path", type=int, help="step path N (1-based)")
    ap.add_argument("--speed", type=int, default=7)
    ap.add_argument("--frames", type=int, default=100)
    ap.add_argument("--check", type=Path, help="a waves-run hatari.out")
    ap.add_argument("--check-paths", type=Path, help="a waves-paths hatari.out (needs --game)")
    args = ap.parse_args()
    level = Level(args.level.read_bytes())
    if args.check:
        return check(level, args.check)
    if args.check_paths:
        if not args.game:
            ap.error("--check-paths needs --game")
        return check_paths(level, sine_table(args.game.read_bytes()), args.check_paths)
    if args.path:
        if not args.game:
            ap.error("--path needs --game")
        mv = Mover(level, sine_table(args.game.read_bytes()), level.paths()[args.path - 1], args.speed)
        for f in range(args.frames):
            print(f, *mv.pos())
            mv.frame()
            if mv.done:
                print("end", *mv.pos())
                break
        return 0
    types = level.types()
    print(f"enemy types ({len(types)}):")
    for k, parts in enumerate(types, 1):
        print(f"  {k}: " + " | ".join(
            f"type ${t:X} worth {worth} update ${u:X} hit ${h:X} anim ${a:X} +20 {w20}"
            f"{' big' if big else ''}{' chained' if ch else ''}"
            for t, worth, u, d, h, a, w20, big, ch in parts))
    print(f"waves ({len(level.waves())}):")
    for a, line, kind, count, path, spacing, fire, speed in level.waves():
        what = f"capsule, token {speed}" if kind == 0 else f"type {kind} x{count}, {speed} steps a frame"
        sp = f"row {spacing - 100} px" if spacing >= 100 else f"trail {spacing} steps"
        fi = "no fire" if fire == 0 else f"aimed fire {fire - 100}" if fire >= 100 else f"random fire {fire}"
        print(f"  ${a:X} line {line}: {what}, path {path}, {sp}, {fi}")
    print(f"scenery ({len(level.scenery())}):")
    names = {0: "restart point", 1: "wall object 1", 2: "wall object 2", 3: "wall object 3",
             4: "wall crawler"}
    for a, line, kind, w4, travel, x, y, side in level.scenery():
        print(f"  ${a:X} line {line}: {names.get(kind, kind)} at {x},{y}"
              f"{' right' if side else ' left'}" + (f", travel {travel}" if kind == 4 else ""))
    paths = level.paths()
    print(f"paths ({len(paths)}):")
    for n, p in enumerate(paths, 1):
        print(f"  {n} ${p:X}: " + "; ".join(" ".join(str(x) for x in op) for op in level.path_ops(p)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
