"""tools/re/play_sound.py on synthetic data: the title tune's sections read
as the boot program reads them (a count and an address each, 0 to loop),
its PCM and its rendering; the YM2149 model's tone frequency and an
envelope that rises and holds; a trace log read across a frame. No game
data."""

import os
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "re"))
import play_sound as ps  # noqa: E402

B = ps.BOOT_BASE


def synthetic_boot():
    boot = bytearray(0x28000)
    struct.pack_into(">IIIII", boot, ps.TUNE_SECTIONS - B, 3, 0x60000, 2, 0x60010, 0)
    boot[0x60000 - B:0x60003 - B] = bytes([10, 11, 12])
    boot[0x60010 - B:0x60012 - B] = bytes([20, 21])
    for s in range(256):  # sample s: channel A at s & 15, B and C silent
        struct.pack_into(">8B", boot, ps.TUNE_TABLE - B + 8 * s, 8, s & 15, 9, 0, 10, 0, 0, 0)
    return bytes(boot)


class TuneTest(unittest.TestCase):
    def test_sections_loop(self):
        boot = synthetic_boot()
        self.assertEqual(ps.tune_sections(boot), [(3, 0x60000), (2, 0x60010)])
        n = 12
        data = ps.tune_samples(boot, n / ps.TUNE_RATE)
        self.assertEqual(list(data), [10, 11, 12, 20, 21] * 2 + [10, 11])

    def test_render_levels(self):
        boot = synthetic_boot()
        out = ps.render_tune(boot, 0.01)
        self.assertEqual(len(out), int(0.01 * ps.RATE))


class YMTest(unittest.TestCase):
    def test_tone_frequency(self):
        ym = ps.YM()
        period = 125  # 2 MHz / (16 x 125) = 1 kHz
        ym.write(0, period & 255)
        ym.write(1, period >> 8)
        ym.write(7, 0b111110)  # tone A on, the rest off
        ym.write(8, 15)
        samples = [ym.sample() for _ in range(ps.RATE)]
        mean = sum(samples) / len(samples)
        crossings = sum(1 for a, b in zip(samples, samples[1:]) if (a - mean) * (b - mean) < 0)
        self.assertAlmostEqual(crossings / 2, 1000, delta=5)

    def test_envelope_rises_and_holds(self):
        ym = ps.YM()
        ym.write(7, 0b111111)  # everything off: the channel holds its level
        ym.write(8, 16)  # channel A follows the envelope
        ym.write(11, 4)
        ym.write(13, 13)  # attack, then hold at the top
        levels = [ym.sample() for _ in range(2000)]
        self.assertLess(levels[0], levels[100])
        self.assertAlmostEqual(levels[-1], ps.LEVELS[31] / 3)


class LogTest(unittest.TestCase):
    def test_frames(self):
        lines = ["ym write data reg=0x8 val=0xf video_cyc=100 0@0 pc=2d3b4",
                 "something else",
                 "ym write data reg=0x7 val=0x3f video_cyc=50 0@0 pc=2d3b4",
                 "ym write data reg=0x8 val=0x0 video_cyc=150000 0@0 pc=2d3b4"]
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "log.txt"
            p.write_text("\n".join(lines) + "\n")
            out = ps.render_log(p, 0.0, 0.05)
        self.assertEqual(len(out), int(0.05 * ps.RATE))
        # level up from the first frame's write, down again in the second
        self.assertGreater(max(out[:800]), 0)


if __name__ == "__main__":
    unittest.main()
