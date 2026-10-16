"""rp/src/x2set.c, the converted set on the card, over a host folder (FatFs
calls on host files: ff_stdio.c; built with x2set_driver.c under ASan and
UBSan): the set made from two synthetic disks laid out like the original's,
not made again when complete, made again when its manifest is of another
version, and the messages' cases (no disks, one disk, another dump). No
game data."""

import os
import shutil
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
from test_x2conv import build_disks, pasti, write  # noqa: E402

NAMES = ["BOOT.X2", "GAME.X2", "TITLE.X2", "LEVEL1.X2", "LEVEL2.X2", "LEVEL3.X2", "LEVEL4.X2", "LEVEL5.X2"]


def expected_outputs(p1, p2):
    f1, f2 = x2disk.flat(stx.read(p1)), x2disk.flat(stx.read(p2))
    boot = x2disk.boot_image(f1)
    game = x2disk.game_image(f1)
    title = bytearray(32000)
    band = 0x4994E - x2disk.BOOT_BASE
    for line in range(54):
        title[(40 + line) * 160 + 4:(40 + line) * 160 + 164] = boot[band + line * 160:band + (line + 1) * 160]
    title = bytes(title) + boot[0x40A5C - x2disk.BOOT_BASE:0x40A5C - x2disk.BOOT_BASE + 32]
    levels = [d for _, _, d in x2disk.levels(x2disk.virtual_sectors(game, f2))]
    return [boot, game, title] + levels


class X2SetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        src = os.path.join(REPO, "rp", "src")
        cls.exe = os.path.join(cls.tmp.name, "x2set_driver")
        cmd = [os.environ.get("CC", "cc"), "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
               "-D_DEFAULT_SOURCE", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-fno-sanitize-recover=undefined", "-I", os.path.join(HERE, "shim"), "-I", os.path.join(src, "include"),
               "-o", cls.exe, os.path.join(HERE, "x2set_driver.c"), os.path.join(HERE, "ff_stdio.c")]
        cmd += [os.path.join(src, f) for f in ("x2set.c", "stx.c", "x2game.c", "lzhuf.c", "crc32.c")]
        subprocess.run(cmd, check=True)
        d1, d2, _ = build_disks(31)
        cls.d1 = pasti(d1, {0, 1, 2, 34, 35})
        cls.d2 = pasti(d2, {0, 3})

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def folder(self, name, disks=("d1", "d2")):
        path = os.path.join(self.tmp.name, name)
        os.makedirs(path)
        if "d1" in disks:
            write(os.path.join(path, "XENON2 disk one.stx"), self.d1)
        if "d2" in disks:
            write(os.path.join(path, "x2_2.STX"), self.d2)
        write(os.path.join(path, "readme.txt"), b"not a disk image")
        return path

    def expected_file(self, path):
        outs = expected_outputs(os.path.join(path, "XENON2 disk one.stx"), os.path.join(path, "x2_2.STX"))
        exp = os.path.join(self.tmp.name, os.path.basename(path) + ".expected")
        write(exp, "".join(f"{len(o)} {zlib.crc32(o):08x}\n" for o in outs).encode())
        return outs, exp

    def run_driver(self, path, expected=None):
        args = [self.exe, path] + ([expected] if expected else [])
        r = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        status, calls = r.stdout.split()
        return status, int(calls)

    def test_makes_the_set_then_finds_it_complete(self):
        path = self.folder("full")
        outs, exp = self.expected_file(path)
        status, calls = self.run_driver(path, exp)
        self.assertEqual(status, "READY")
        self.assertGreater(calls, 100)
        for name, data in zip(NAMES, outs):
            with open(os.path.join(path, "SET", name), "rb") as f:
                self.assertEqual(f.read(), data, name)
        with open(os.path.join(path, "SET", "SET.X2M")) as f:
            lines = f.read().splitlines()
        self.assertEqual(lines[0], "X2SET 1")
        self.assertEqual(lines[1:], [f"{n} {len(o)} {zlib.crc32(o):08x}" for n, o in zip(NAMES, outs)])
        self.assertEqual(self.run_driver(path, exp), ("READY", 0))

    def test_another_version_is_made_again(self):
        path = self.folder("version")
        _, exp = self.expected_file(path)
        self.assertEqual(self.run_driver(path, exp)[0], "READY")
        manifest = os.path.join(path, "SET", "SET.X2M")
        with open(manifest) as f:
            text = f.read()
        with open(manifest, "w") as f:
            f.write(text.replace("X2SET 1", "X2SET 0", 1))
        status, calls = self.run_driver(path, exp)
        self.assertEqual(status, "READY")
        self.assertGreater(calls, 0)

    def test_a_missing_file_is_made_again(self):
        path = self.folder("missing")
        _, exp = self.expected_file(path)
        self.assertEqual(self.run_driver(path, exp)[0], "READY")
        os.remove(os.path.join(path, "SET", "LEVEL3.X2"))
        self.assertGreater(self.run_driver(path, exp)[1], 0)

    def test_a_double_sided_disk_1_is_the_kixx_release(self):
        # One double-sided disk named like disk 1: refused by name, and
        # skipped when the Image Works disks are there too.
        d1, _, _ = build_disks(32)
        recs = []
        for t in range(80):
            for side in (0, 1):
                data = d1[t * 5120:(t + 1) * 5120] if side == 0 else bytes(5120)
                recs.append(track_record(t, side, 0x00, plain=data))
        kixx = image_file(*recs)
        path = os.path.join(self.tmp.name, "kixx")
        os.makedirs(path)
        write(os.path.join(path, "xenon2_kixx.stx"), kixx)
        self.assertEqual(self.run_driver(path)[0], "KIXX")
        both = self.folder("kixx-and-imageworks")
        write(os.path.join(both, "aaa_kixx.stx"), kixx)
        _, exp = self.expected_file(both)
        self.assertEqual(self.run_driver(both, exp)[0], "READY")

    def test_the_messages_cases(self):
        self.assertEqual(self.run_driver(self.folder("none", ()))[0], "NO_DISKS")
        self.assertEqual(self.run_driver(self.folder("one", ("d1",)))[0], "NO_DISK2")
        self.assertEqual(self.run_driver(self.folder("two", ("d2",)))[0], "NO_DISK1")
        self.assertEqual(self.run_driver(os.path.join(self.tmp.name, "absent"))[0], "NO_CARD")
        # Synthetic disks do not convert to the known release.
        self.assertEqual(self.run_driver(self.folder("other"))[0], "WRONG_DUMP")


if __name__ == "__main__":
    unittest.main()
