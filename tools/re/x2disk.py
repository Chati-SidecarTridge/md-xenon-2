#!/usr/bin/env python3
"""Extract the game image and the five levels from the user's own Xenon 2
disks (the Image Works release, two Pasti images), the way the original's
loader does it:

  x2disk.py DISK1.stx DISK2.stx -o OUTDIR

writes OUTDIR/game.bin (the program as it sits at $400 once decrypted) and
OUTDIR/level1.bin ... level5.bin (each level's data as unpacked to its
workspace), and prints their sizes and CRC-32s.

The layout, as the original reads it:

- Disk 1, side 0, tracks 34-79 (235,520 bytes) load to $400. They are
  decrypted in place, in two passes over $400-$3FFFF: each byte is XORed with
  the decrypted byte before it, the first with GAME_SEED.
- Levels are read through a virtual sector space: sector 0 is disk 2's boot
  sector, sectors 1-18 are 512-byte blocks of the game image at
  RESIDENT_BASE + 512 * n, and sector n > 18 is disk 2's logical sector
  n - 18 (track = s // 10, sector ID = s % 10 + 1).
- Level 1 starts at virtual sector 1, byte 0; every other level starts where
  the one before it ends. A level is zero bytes, a marker byte, its unpacked
  length (32-bit little-endian), then LZHUF data (LHarc -lh1-, lzhuf.py).

Standard library only; nothing of the game is in this file.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lzhuf  # noqa: E402
import stx  # noqa: E402

TRACK_BYTES = 10 * 512
GAME_FIRST_TRACK, GAME_LAST_TRACK = 34, 79
GAME_BASE = 0x400
GAME_DECRYPT_END = 0x40000
GAME_SEED = 0x2A
GAME_PASSES = 2
RESIDENT_BASE = 0x32B5C
RESIDENT_SECTORS = 18
LEVELS = 5


def flat(image: stx.Image) -> bytes:
    """Side 0's sectors in track and ID order, as the game addresses them."""
    data, _ = stx.flatten(image)
    return data


def decrypt(block: bytes, seed: int = GAME_SEED, passes: int = GAME_PASSES) -> bytes:
    b = bytearray(block)
    for _ in range(passes):
        prev = seed
        for i in range(len(b)):
            b[i] ^= prev
            prev = b[i]
    return bytes(b)


def encrypt(block: bytes, seed: int = GAME_SEED, passes: int = GAME_PASSES) -> bytes:
    """The inverse of decrypt() (for tests)."""
    b = bytearray(block)
    for _ in range(passes):
        prev = seed
        for i in range(len(b)):
            plain = b[i]
            b[i] ^= prev
            prev = plain
    return bytes(b)


def game_image(disk1: bytes) -> bytes:
    block = disk1[GAME_FIRST_TRACK * TRACK_BYTES:(GAME_LAST_TRACK + 1) * TRACK_BYTES]
    # The loader decrypts up to GAME_DECRYPT_END; the bytes past the loaded
    # block only affect themselves (each byte depends on those before it).
    return decrypt(block)


def virtual_sectors(game: bytes, disk2: bytes) -> bytes:
    res = RESIDENT_BASE - GAME_BASE
    return (disk2[:512] + game[res + 512:res + (RESIDENT_SECTORS + 1) * 512] + disk2[512:])


def levels(virtual: bytes, count: int = LEVELS) -> list[tuple[int, int, bytes]]:
    """(virtual sector, offset, unpacked data) of each level, in order."""
    out = []
    pos = 512
    for _ in range(count):
        start = pos
        while virtual[pos] == 0:
            pos += 1
        pos += 1                                  # the marker byte
        (length,) = struct.unpack_from("<I", virtual, pos)
        data, end = lzhuf.decode(virtual, pos + 4, length)
        out.append((start // 512, start % 512, data))
        pos = end
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disk1")
    ap.add_argument("disk2")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()
    d1, d2 = flat(stx.read(args.disk1)), flat(stx.read(args.disk2))
    if d1[2:4] != b"X2" or d2[2:5] != b"X2B":
        print("error: not the two Image Works disks (boot sector names X2 / X2B)", file=sys.stderr)
        return 1
    outdir = Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)
    game = game_image(d1)
    (outdir / "game.bin").write_bytes(game)
    print(f"game.bin    {len(game):7d} B  crc32 {zlib.crc32(game):08x}")
    for n, (sec, off, data) in enumerate(levels(virtual_sectors(game, d2)), 1):
        (outdir / f"level{n}.bin").write_bytes(data)
        print(f"level{n}.bin  {len(data):7d} B  crc32 {zlib.crc32(data):08x}  (virtual sector {sec}, offset {off})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
