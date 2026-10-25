"""tools/re/waves.py and render_level.py --waves on a synthetic level laid
out like the original's: a header naming the wave and scenery tables, the
path table and the wave handler (whose jump table names the enemy types),
records of 14 bytes, types of 24, paths of word opcodes; and a made-up
direction table. No game data."""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "re"))
import render_level as rl  # noqa: E402
import waves  # noqa: E402

B = waves.BASE


def synthetic() -> bytes:
    lv = bytearray(0x4000)

    def w(off, *vals):
        struct.pack_into(">%dh" % len(vals), lv, off, *vals)

    def l(off, val):
        struct.pack_into(">I", lv, off, val)

    handler, types, paths, path_data, wave_t, scen_t = 0x100, 0x200, 0x300, 0x320, 0x400, 0x500
    # +$10: bra.w to the handler; the handler: add.w d0,d0 x2, movea.l
    # (pc,d0.w),a2, jmp (a2), then 11 entries; kinds 1 and 2 point at a
    # `lea abs.l,a2` (then a jump, not needed here)
    w(0x10, 0x6000, handler - 0x12)
    l(0x14, B + scen_t)
    l(0x30, B + paths)
    l(0x34, B + path_data)
    l(0x38, B + wave_t)
    table = handler + 10
    for k, (stub, typ) in enumerate(((0x180, types), (0x188, types + 26)), 1):
        l(table + 4 * k, B + stub)
        w(stub, 0x45F9)
        l(stub + 2, B + typ)
    l(table + 12, B + 0x190)              # kind 3: not a lea, the list stops
    # type 1: one part, worth 400, big; type 2: two parts, chained
    struct.pack_into(">HHIIIIHBB", lv, types, 0xC8, 400, 0xE1E, 0xE24, 0xE2A, B + 0x600, 2, 0xFF, 0)
    w(types + 24, 0)
    struct.pack_into(">HHIIIIHBB", lv, types + 26, 0x108, 200, 0xE1E, 0xE24, 0xE30, B + 0x610, 1, 0, 0xFF)
    struct.pack_into(">HHIIIIHBB", lv, types + 50, 0x108, 0, 0xE36, 0xE24, 0xE30, B + 0x620, 1, 0, 0xFF)
    w(types + 74, 0)
    # paths: 1 a straight line right then a wait then down; 2 a turn
    l(paths, B + path_data)
    l(paths + 4, B + path_data + 0x40)
    w(path_data, 10, 100, 20, 2, 0, 0, 0, 4, 4, 3, 2, 64, 0, 0, 2, 0)
    w(path_data + 0x40, 10, 0, 0, 2, 0, 256, 0, 3, 0)
    # waves: line 1000 kind 1 x3 path 1, trail 2 steps, speed 2;
    #        line 900 kind 2 x2 path 2, a row 20 px apart, aimed fire 5;
    #        line 800 a capsule with token 6
    w(wave_t, 1000, 1, 3, 1, 2, 0, 2)
    w(wave_t + 14, 900, 2, 2, 2, 120, 105, 1)
    w(wave_t + 28, 800, 0, 1, 1, 32, 0, 6)
    w(wave_t + 42, -1)
    # scenery: a restart point and a left wall object
    w(scen_t, 1200, 0, 0, 0, 152, 1100, 0)
    w(scen_t + 14, 1100, 1, 0, 0, 24, 1088, 0)
    w(scen_t + 28, -1)
    return bytes(lv)


def sine() -> list[int]:
    """A made-up direction table (y moves by entry [dir], x by [dir + 64],
    64 a pixel): direction 0 moves right a pixel a step, 64 down."""
    t = [0] * 320
    t[64] = 64
    return t


class WavesTest(unittest.TestCase):
    def setUp(self):
        self.lv = waves.Level(synthetic())

    def test_tables(self):
        wv = self.lv.waves()
        self.assertEqual([r[1:] for r in wv],
                         [(1000, 1, 3, 1, 2, 0, 2), (900, 2, 2, 2, 120, 105, 1),
                          (800, 0, 1, 1, 32, 0, 6)])
        self.assertEqual(wv[1][0], B + 0x400 + 14)
        self.assertEqual([r[1:] for r in self.lv.scenery()],
                         [(1200, 0, 0, 0, 152, 1100, 0), (1100, 1, 0, 0, 24, 1088, 0)])

    def test_types(self):
        t = self.lv.types()
        self.assertEqual(len(t), 2)
        self.assertEqual(t[0], [(0xC8, 400, 0xE1E, 0xE24, 0xE2A, B + 0x600, 2, 0xFF, 0)])
        self.assertEqual([p[0:2] + p[7:] for p in t[1]], [(0x108, 200, 0, 0xFF), (0x108, 0, 0, 0xFF)])

    def test_path_ops(self):
        p = self.lv.paths()
        self.assertEqual(self.lv.path_ops(p[0]),
                         [("at", 100, 20), ("arc", 0, 0, 0, 4), ("wait", 3), ("arc", 64, 0, 0, 2), ("end",)])
        self.assertEqual(self.lv.path_ops(p[1]), [("at", 0, 0), ("arc", 0, 256, 0, 3), ("end",)])

    def test_mover_line_wait_and_end(self):
        mv = waves.Mover(self.lv, sine(), self.lv.paths()[0], speed=2)
        self.assertEqual(mv.pos(), (100, 20))
        out = []
        for _ in range(8):
            mv.frame()
            out.append((mv.pos(), mv.done))
        # 2 steps a frame, a pixel a step: the 4 steps right; the wait of 3
        # takes the third frame's 2 steps and the fourth frame's first; the
        # 2 steps down; the end, read in the frame of the last step
        self.assertEqual(out[0], ((102, 20), False))
        self.assertEqual(out[1], ((104, 20), False))
        self.assertEqual(out[2], ((104, 20), False))
        self.assertEqual(out[3], ((104, 21), False))
        self.assertEqual(out[4], ((104, 22), True))

    def test_trail_delay_and_row_offset(self):
        lead = waves.Mover(self.lv, sine(), self.lv.paths()[0], speed=2)
        third = waves.Mover(self.lv, sine(), self.lv.paths()[0], speed=2, delay=4)
        row = waves.Mover(self.lv, sine(), self.lv.paths()[0], speed=2, dx=20)
        self.assertEqual(row.pos(), (120, 20))
        for _ in range(2):
            lead.frame()
            third.frame()
        self.assertEqual(lead.pos(), (104, 20))
        self.assertEqual(third.pos(), (100, 20))       # 4 steps of wait used
        third.frame()
        self.assertEqual(third.pos(), (102, 20))

    def test_turn(self):
        # a rate of 256 turns the direction by one a step: it moves on
        # directions 0, 1, 2 and ends the frame on 3
        t = sine()
        t[1], t[65] = 8, 64
        t[2], t[66] = 12, 64
        mv = waves.Mover(self.lv, t, self.lv.paths()[1], speed=3)
        mv.frame()
        self.assertEqual((mv.x, mv.y), (3 << 16, 20 << 10))
        self.assertEqual(mv.dir, 3)

    def test_strip_marks(self):
        lv = bytearray(synthetic())
        rows = [bytes(320) for _ in range(1300)]
        out = rl.mark_waves(bytes(lv), rows)
        self.assertEqual(out[1000][0], rl.RED)          # the trigger line, dashed
        self.assertEqual(out[1000][2], 0)
        self.assertEqual(out[1020][100], rl.RED)        # path 1 starts at 100, 20
        self.assertEqual(out[900][20], rl.RED)          # path 2 at 0, 0: the row's second member at x 20
        self.assertEqual(out[820][100], rl.RED)         # the capsule on path 1
        self.assertEqual(out[1088][24], rl.YELLOW)      # the wall object's box
        self.assertEqual(out[1088 + 31][24 + 31], rl.YELLOW)
        self.assertEqual(out[1100][152], rl.CYAN)       # the restart point


if __name__ == "__main__":
    unittest.main()
