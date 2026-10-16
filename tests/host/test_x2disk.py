"""tools/re/lzhuf.py and tools/re/x2disk.py on synthetic data: an LZHUF
encoder written here (LZHUF's adaptive Huffman coding, LHarc -lh1-'s starting
ring) round-trips through the decoder; the game image's chained-XOR
decryption undoes its inverse; two level streams laid out across the resident
block and disk 2 come back in order with their start positions. No game data."""

import os
import random
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "re"))
import lzhuf  # noqa: E402
import x2disk  # noqa: E402

N, F, T, R = lzhuf.N, lzhuf.F, lzhuf.T, lzhuf.R


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, nbits, value):
        for k in range(nbits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> k) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def done(self):
        if self.n:
            self.out.append(self.acc << (8 - self.n))
            self.acc = self.n = 0
        return bytes(self.out)


# Encoder side of the position code: the first index of each upper value in
# D_CODE gives its prefix, D_LEN its length.
P_CODE, P_LEN = {}, {}
for i, (code, length) in enumerate(zip(lzhuf.D_CODE, lzhuf.D_LEN)):
    if code not in P_CODE:
        P_CODE[code], P_LEN[code] = i, length


def encode(data: bytes, window: int = 512) -> bytes:
    """Greedy LZSS over the last `window` bytes (the format allows 4,036)."""
    h = lzhuf.Huffman()
    w = BitWriter()

    def put_char(c):
        bits = []
        k = h.prnt[c + T]
        while True:
            bits.append(k & 1)
            k = h.prnt[k]
            if k == R:
                break
        for b in reversed(bits):
            w.put(1, b)
        h.update(c)

    def put_position(p):
        u = p >> 6
        w.put(P_LEN[u], P_CODE[u] >> (8 - P_LEN[u]))
        w.put(6, p & 0x3F)

    i = 0
    while i < len(data):
        best_len, best_dist = 0, 0
        for dist in range(1, min(i, window, N - F) + 1):
            n = 0
            while n < F and i + n < len(data) and data[i + n] == data[i + n - dist]:
                n += 1
            if n > best_len:
                best_len, best_dist = n, dist
        if best_len > lzhuf.THRESHOLD:
            put_char(255 - lzhuf.THRESHOLD + best_len)
            put_position(best_dist - 1)
            i += best_len
        else:
            put_char(data[i])
            i += 1
    return w.done()


def sample(seed, n):
    rnd = random.Random(seed)
    words = [bytes(rnd.randrange(256) for _ in range(rnd.randrange(3, 40))) for _ in range(12)]
    out = bytearray()
    while len(out) < n:
        out += rnd.choice(words) if rnd.random() < 0.7 else bytes([rnd.randrange(256)])
    return bytes(out[:n])


def stream(data: bytes, pad: int) -> bytes:
    return bytes(pad) + b"\x2a" + struct.pack("<I", len(data)) + encode(data)


class LzhufTest(unittest.TestCase):
    def test_ring_and_tables(self):
        ring = lzhuf.lh1_ring()
        self.assertEqual(len(ring), 4096)
        self.assertEqual(ring[:13], bytes(13))
        self.assertEqual(ring[-128:], b" " * 128)
        self.assertEqual(sorted(set(lzhuf.D_CODE)), list(range(64)))

    def test_round_trip(self):
        for seed, n in ((1, 3000), (2, 9000)):
            data = sample(seed, n)
            packed = encode(data)
            out, end = lzhuf.decode(packed, 0, len(data))
            self.assertEqual(out, data)
            self.assertLessEqual(end, len(packed))
            self.assertLess(len(packed), len(data))

    def test_frequency_rescale(self):
        # Random bytes code one symbol each: 34,000 of them take the root's
        # count past MAX_FREQ (32,768), so the tree is rebuilt mid-stream.
        rnd = random.Random(3)
        data = bytes(rnd.randrange(256) for _ in range(34000))
        self.assertEqual(lzhuf.decode(encode(data, window=8), 0, len(data))[0], data)


class X2DiskTest(unittest.TestCase):
    def test_decrypt_undoes_encrypt(self):
        plain = sample(4, 5000)
        self.assertEqual(x2disk.decrypt(x2disk.encrypt(plain)), plain)
        self.assertNotEqual(x2disk.encrypt(plain)[:100], plain[:100])

    def test_levels_across_resident_block_and_disk2(self):
        a, b = sample(5, 7000), sample(6, 5000)
        body = stream(a, 3) + stream(b, 17)
        res_bytes = x2disk.RESIDENT_SECTORS * 512
        game = bytearray(x2disk.RESIDENT_BASE - x2disk.GAME_BASE + 512) + body[:res_bytes]
        disk2 = b"BOOT".ljust(512, b"\0") + body[res_bytes:] + bytes(512)
        v = x2disk.virtual_sectors(bytes(game), disk2)
        self.assertEqual(v[512:512 + 64], body[:64])
        got = x2disk.levels(v, count=2)
        self.assertEqual([g[2] for g in got], [a, b])
        self.assertEqual(got[0][:2], (1, 0))


if __name__ == "__main__":
    unittest.main()
