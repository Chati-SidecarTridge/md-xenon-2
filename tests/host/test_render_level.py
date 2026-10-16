"""tools/re/render_level.py on a synthetic level laid out like the original's
(a header of addresses, a 20-word map, opaque and masked tile records, a
two-plane backdrop of 20 columns of 192 lines): each cell kind, the masking,
the fine scroll and the backdrop's 192-line repeat. No game data."""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "re"))
import render_level as rl  # noqa: E402

B = rl.BASE


def synthetic():
    level = bytearray(0x10000)
    bd, mp, tiles, pal = 0x1000, 0x6000, 0x8000, 0x7FE0
    for off, addr in ((0x00, bd), (0x04, mp), (0x08, tiles), (0x1C, pal), (0x20, B + 0xC000)):
        struct.pack_into(">I", level, off, B + addr if off != 0x20 else addr)
    for c in range(20):                  # backdrop: column c, line l -> bytes (c, l)
        for l in range(192):
            o = bd + c * 768 + l * 4
            level[o:o + 4] = bytes([c, l, 0xF0, 0x0F])
    # an opaque tile at word 1 (offset 16), a masked one at word 9 (offset 144)
    for line in range(16):
        level[tiles + 16 + line * 8: tiles + 16 + line * 8 + 8] = bytes([0xA0 + line] * 8)
        t = tiles + 144 + line * 12
        level[t:t + 4] = b"\xFF\x00\xFF\x00"          # keep the backdrop's bytes 0 and 2
        level[t + 4:t + 8] = bytes([0, 0x11, 0, 0x22])
        level[t + 8:t + 12] = bytes([0x33] * 4)
    for r in range(30):                   # map rows: cells 0 backdrop, 1 opaque, 2 masked
        for c in range(20):
            w = (0, 1, 0x8009)[c % 3]
            struct.pack_into(">H", level, mp + 2 * (r * 20 + c), w)
    return bytes(level), B + bd


class RenderLevelTest(unittest.TestCase):
    def setUp(self):
        self.level, self.bd = synthetic()

    def group(self, pf, y, c):
        o = y * 160 + c * 8
        return pf[o:o + 4], pf[o + 4:o + 8]

    def test_cells_and_mask(self):
        pf = rl.render(self.level, 0, 0, self.bd)
        self.assertEqual(self.group(pf, 5, 0), (bytes([0, 5, 0xF0, 0x0F]), bytes(4)))   # backdrop
        self.assertEqual(self.group(pf, 5, 1), (bytes([0xA5] * 4), bytes([0xA5] * 4)))  # opaque
        p01, p23 = self.group(pf, 5, 2)
        self.assertEqual(p01, bytes([2, 0x11, 0xF0, 0x22]))                           # masked
        self.assertEqual(p23, bytes([0x33] * 4))

    def test_fine_scroll_and_backdrop_pointer(self):
        pf = rl.render(self.level, 0, 7, self.bd + 4 * 100)
        self.assertEqual(self.group(pf, 0, 1)[0], bytes([0xA7] * 4))   # tile line 7 at the top
        self.assertEqual(self.group(pf, 9, 1)[0], bytes([0xA0] * 4))   # the next row's line 0
        self.assertEqual(self.group(pf, 0, 3)[0][:2], bytes([3, 100]))  # backdrop line 100
        self.assertEqual(self.group(pf, 92, 3)[0][:2], bytes([3, 0]))   # wraps every 192 lines

    def test_strip(self):
        self.assertEqual(rl.map_rows(self.level), (0x7FE0 - 0x6000) // 40)
        s = rl.strip(self.level)
        self.assertEqual(len(s), rl.map_rows(self.level) * 16 * 160)
        for y in (0, 15, 16, 100, 191, 192, 400):       # column 0 shows the backdrop
            self.assertEqual(s[y * 160:y * 160 + 2], bytes([0, y % 192]), y)


if __name__ == "__main__":
    unittest.main()
