"""The converter's C core (rp/src/stx.c, x2game.c, lzhuf.c, crc32.c, built
as tools/x2conv under the address and undefined-behaviour sanitizers) against
the Python tools (tools/re/x2disk.py) on two synthetic disks laid out like the
original's: a boot program encrypted in its two layers on disk 1 tracks 1-32,
a game image encrypted on tracks 34-79 holding the level stream's first 18
sectors, and five LZHUF levels across that block and disk 2. Tracks with and
without sector descriptors, sectors out of order, a sector with a CRC error.
No game data."""

import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "re"))
sys.path.insert(0, HERE)
import stx  # noqa: E402
import x2disk  # noqa: E402
from test_stx import image_file, track_record  # noqa: E402
from test_x2disk import encode, sample  # noqa: E402

TRACKS = 80
SECTOR = 512
SPT = 10


def pasti(flat: bytes, descriptor_tracks: set) -> bytes:
    """A Pasti image of a flat single-sided disk: some tracks with sector
    descriptors (IDs skewed by two per track, sector 6 of track 0 marked
    with a CRC error), the others standard."""
    recs = []
    for t in range(TRACKS):
        data = flat[t * SPT * SECTOR:(t + 1) * SPT * SECTOR]
        if t in descriptor_tracks:
            first = (SPT - 2 * t) % SPT
            order = [(first + k) % SPT + 1 for k in range(SPT)]
            sectors = [(t, 0, n, 2, 0x08 if (t == 0 and n == 6) else 0,
                        data[(n - 1) * SECTOR:n * SECTOR]) for n in order]
            recs.append(track_record(t, 0, 0x41, sectors, image=b"\x4e" * 32))
        else:
            recs.append(track_record(t, 0, 0x00, plain=data))
    return image_file(*recs)


def level_stream(levels, pads):
    out = bytearray()
    for data, pad in zip(levels, pads):
        out += bytes(pad) + b"\x2a" + struct.pack("<I", len(data)) + encode(data, window=256)
    return bytes(out)


def build_disks(seed):
    rnd = random.Random(seed)
    boot_plain = bytearray(sample(seed, x2disk.TRACK_BYTES * 32))
    game_plain = bytearray(sample(seed + 1, x2disk.TRACK_BYTES * 46))
    levels = [sample(seed + 10 + n, rnd.randrange(2500, 6000)) for n in range(5)]
    stream = level_stream(levels, [rnd.randrange(0, 40) for _ in levels])
    res = x2disk.RESIDENT_BASE - x2disk.GAME_BASE
    head = stream[:x2disk.RESIDENT_SECTORS * SECTOR].ljust(x2disk.RESIDENT_SECTORS * SECTOR, b"\0")
    game_plain[res + SECTOR:res + SECTOR + len(head)] = head
    rest = stream[x2disk.RESIDENT_SECTORS * SECTOR:]

    d1 = bytearray(TRACKS * SPT * SECTOR)
    d1[:SECTOR] = (b"\x60\x1cX2    ").ljust(SECTOR, b"\0")
    d1[x2disk.TRACK_BYTES:33 * x2disk.TRACK_BYTES] = x2disk.boot_encrypt(bytes(boot_plain))
    d1[34 * x2disk.TRACK_BYTES:80 * x2disk.TRACK_BYTES] = x2disk.encrypt(bytes(game_plain))
    d2 = bytearray(TRACKS * SPT * SECTOR)
    d2[:SECTOR] = (b"\x60\x1cX2B   ").ljust(SECTOR, b"\0")
    d2[SECTOR:SECTOR + len(rest)] = rest
    return bytes(d1), bytes(d2), levels


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def build_x2conv(outdir):
    exe = os.path.join(outdir, "x2conv")
    src = os.path.join(REPO, "rp", "src")
    cmd = [os.environ.get("CC", "cc"), "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=undefined",
           "-I", os.path.join(src, "include"), "-o", exe, os.path.join(REPO, "tools", "x2conv", "x2conv.c")]
    cmd += [os.path.join(src, f) for f in ("stx.c", "x2game.c", "lzhuf.c", "crc32.c", "x2level.c", "x2global.c", "x2sprite.c", "x2tune.c")]
    subprocess.run(cmd, check=True)
    return exe


class X2ConvTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_x2conv(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_both(self, seed):
        d1, d2, levels = build_disks(seed)
        work = os.path.join(self.tmp.name, f"s{seed}")
        os.makedirs(work)
        p1, p2 = os.path.join(work, "d1.stx"), os.path.join(work, "d2.stx")
        write(p1, pasti(d1, {0, 1, 2, 5, 33, 34, 35, 60}))
        write(p2, pasti(d2, {0, 3, 4, 7}))
        out = os.path.join(work, "out")
        r = subprocess.run([self.exe, p1, p2, out], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        # The Python side, from the same Pasti images.
        f1 = x2disk.flat(stx.read(p1))
        f2 = x2disk.flat(stx.read(p2))
        boot = x2disk.boot_image(f1)
        game = x2disk.game_image(f1)
        py_levels = x2disk.levels(x2disk.virtual_sectors(game, f2))
        def read(name):
            with open(os.path.join(out, name), "rb") as f:
                return f.read()
        return boot, game, py_levels, levels, read

    def test_same_bytes_as_the_python_tools(self):
        boot, game, py_levels, levels, read = self.run_both(21)
        self.assertEqual(read("boot.bin"), boot)
        self.assertEqual(read("game.bin"), game)
        for n, (_, _, data) in enumerate(py_levels, 1):
            self.assertEqual(read(f"level{n}.bin"), data)
            self.assertEqual(data, levels[n - 1])

    def test_title_is_the_band_and_the_palette(self):
        boot, _, _, _, read = self.run_both(22)
        title = read("title.bin")
        self.assertEqual(len(title), 32032)
        band = 0x4994E - x2disk.BOOT_BASE
        for line in range(54):
            self.assertEqual(title[(40 + line) * 160 + 4:(40 + line) * 160 + 164],
                             boot[band + line * 160:band + (line + 1) * 160])
        self.assertEqual(title[32000:], boot[0x40A5C - x2disk.BOOT_BASE:0x40A5C - x2disk.BOOT_BASE + 32])
        self.assertFalse(any(title[:40 * 160 + 4]))

    def test_reports_crc32(self):
        d1, d2, _ = build_disks(23)
        work = os.path.join(self.tmp.name, "crc")
        os.makedirs(work)
        p1, p2 = os.path.join(work, "d1.stx"), os.path.join(work, "d2.stx")
        write(p1, pasti(d1, {0}))
        write(p2, pasti(d2, {0}))
        r = subprocess.run([self.exe, p1, p2, os.path.join(work, "out")], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(os.path.join(work, "out", "game.bin"), "rb") as f:
            game = f.read()
        self.assertIn(f"crc32 {zlib.crc32(game):08x}", r.stdout)

    def test_refuses_other_disks(self):
        d1, d2, _ = build_disks(24)
        work = os.path.join(self.tmp.name, "other")
        os.makedirs(work)
        p1, p2 = os.path.join(work, "d1.stx"), os.path.join(work, "d2.stx")
        write(p1, pasti(d1, set()))
        write(p2, pasti(b"\0" * len(d2), set()))
        r = subprocess.run([self.exe, p1, p2, os.path.join(work, "out")], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Image Works", r.stderr)


if __name__ == "__main__":
    unittest.main()
