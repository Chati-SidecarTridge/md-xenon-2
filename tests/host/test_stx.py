"""tools/re/stx.py, the Pasti (.stx) reader, on synthetic images built here:
a standard track without sector descriptors, a track with descriptors, a raw
track image with a sync offset, a fuzzy-bit mask, sectors out of order, a
sector without data and one of another size. No game data."""

import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "re"))
import stx  # noqa: E402


def track_record(number, side, flags, sectors=(), fuzzy=b"", image=None, sync=None, plain=None):
    """One Pasti track record. sectors: (id_track, id_side, id_number,
    size_code, status, data or None); data is laid out after the image."""
    body = b""
    descs = b""
    data_area = b""
    head = b""
    if image is not None:
        head = (struct.pack("<H", sync) if sync is not None else b"") + struct.pack("<H", len(image)) + image
    if plain is not None:
        body = plain
        count = len(plain) // 512
    else:
        count = len(sectors)
        offset = len(head)
        for (t, s, n, size, status, data) in sectors:
            descs += struct.pack("<IHHBBBBHBB", offset if data else 0, 0, 0, t, s, n, size, 0, status, 0)
            if data:
                data_area += data
                offset += len(data)
        body = descs + fuzzy + head + data_area
    rec = struct.pack("<IIHHHBB", 16 + len(body), len(fuzzy), count, flags, 6250, number | (side << 7), 0)
    return rec + body


def image_file(*tracks):
    return b"RSY\0" + struct.pack("<HHHBBI", 3, 1, 0, len(tracks), 2, 0) + b"".join(tracks)


def fill(byte, n=512):
    return bytes([byte]) * n


class StxTest(unittest.TestCase):
    def test_standard_track(self):
        plain = fill(1) + fill(2) + fill(3)
        img = stx.parse(image_file(track_record(0, 0, 0x00, plain=plain)))
        t = img.track(0, 0)
        self.assertEqual([s.number for s in t.sectors], [1, 2, 3])
        self.assertEqual(t.sectors[2].data, fill(3))

    def test_descriptors_image_fuzzy_and_odd_sectors(self):
        sectors = [
            (5, 0, 3, 2, 0x00, fill(0x33)),           # out of order
            (5, 0, 1, 2, 0x80, fill(0x11)),           # fuzzy: mask follows
            (5, 0, 2, 2, 0x10, None),                 # record not found
            (9, 1, 4, 1, 0x08, fill(0x44, 256)),      # other ID, 256 B, CRC error
        ]
        rec = track_record(5, 0, 0xC1, sectors, fuzzy=fill(0xF0), image=b"\x4e" * 40, sync=12)
        img = stx.parse(image_file(track_record(4, 0, 0x00, plain=fill(9)), rec))
        t = img.track(5, 0)
        self.assertEqual(t.image, b"\x4e" * 40)
        by_id = {s.number: s for s in t.sectors}
        self.assertEqual(by_id[3].data, fill(0x33))
        self.assertEqual(by_id[1].data, fill(0x11))
        self.assertEqual(by_id[1].fuzzy, fill(0xF0))
        self.assertIsNone(by_id[2].data)
        self.assertEqual((by_id[4].track, by_id[4].side, by_id[4].size), (9, 1, 256))
        self.assertEqual(by_id[4].data, fill(0x44, 256))

    def test_flatten_orders_by_track_side_and_id(self):
        a = track_record(1, 0, 0x01, [(1, 0, 2, 2, 0, fill(0xB)), (1, 0, 1, 2, 0, fill(0xA))])
        b = track_record(0, 0, 0x00, plain=fill(0xC))
        data, lines = stx.flatten(stx.parse(image_file(a, b)))
        self.assertEqual(data, fill(0xC) + fill(0xA) + fill(0xB))
        self.assertEqual(len(lines), 3)

    def test_rejects_other_files(self):
        with self.assertRaises(stx.StxError):
            stx.parse(b"not a pasti image")
        bad = image_file(track_record(0, 0, 0x00, plain=fill(1)))[:-100]
        with self.assertRaises(stx.StxError):
            stx.parse(bad)

    def test_cli_flat_writes_image_and_map(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "x.stx")
            with open(src, "wb") as f:
                f.write(image_file(track_record(0, 0, 0x00, plain=fill(7) + fill(8))))
            out = os.path.join(d, "x.st")
            sys.argv = ["stx.py", "flat", src, "-o", out]
            self.assertEqual(stx.main(), 0)
            with open(out, "rb") as f:
                self.assertEqual(f.read(), fill(7) + fill(8))
            self.assertTrue(os.path.isfile(out + ".map"))


if __name__ == "__main__":
    unittest.main()
