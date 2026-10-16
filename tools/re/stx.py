#!/usr/bin/env python3
"""Read Pasti (.stx) Atari ST floppy images: tracks, sectors, their data.

  stx.py info  IMAGE                 the tracks: sectors, their IDs and status
  stx.py sector IMAGE TRACK SIDE N   one sector's data, as hex (or -o FILE)
  stx.py flat  IMAGE -o FILE         every sector with data, in track, side and
                                     sector-ID order, as one file (a .st-like
                                     image when the disk is standard), with a
                                     map of where each sector went (FILE.map)

A Pasti image is a 16-byte file header, then one record per track. A track
record is a 16-byte descriptor; when the track has sector descriptors (flag
bit 0), 16 bytes per sector, the fuzzy-bit masks, then the track data, which
starts with the raw track image when there is one (flag bit 6; with bit 7 a
16-bit sync offset first) and holds each sector's data at the offset its
descriptor gives. Without sector descriptors the track is standard: its
sectors are 512 bytes each, numbered from 1, back to back. Integers are
little-endian.

Fuzzy (weak) bits, read timings and the raw track image are reported, not
reproduced: the game's data is in ordinary sectors. Standard library only.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

FILE_MAGIC = b"RSY\0"
TRK_SECT = 0x01      # the track has sector descriptors
TRK_IMAGE = 0x40     # the track data starts with the raw track image
TRK_SYNC = 0x80      # ... preceded by the offset of its first sync mark
FDC_RNF = 0x10       # WD1772 status: record not found (no data)
FDC_CRC = 0x08       # WD1772 status: CRC error
FDC_FUZZY = 0x80     # Pasti: the sector has a fuzzy-bit mask


class StxError(ValueError):
    pass


@dataclass
class Sector:
    track: int          # the ID field's values, which a protection may change
    side: int
    number: int
    size: int           # bytes, 128 << the ID's size code
    fdc_status: int
    bit_position: int
    read_time: int
    data: bytes | None  # None when the sector has no data (record not found)
    fuzzy: bytes | None = None


@dataclass
class Track:
    track: int          # the physical track and side the record is for
    side: int
    flags: int
    track_length: int
    sectors: list[Sector] = field(default_factory=list)
    image: bytes | None = None


@dataclass
class Image:
    version: int
    tool: int
    revision: int
    tracks: list[Track]

    def track(self, track: int, side: int) -> Track | None:
        for t in self.tracks:
            if t.track == track and t.side == side:
                return t
        return None


def parse(blob: bytes) -> Image:
    if len(blob) < 16 or blob[:4] != FILE_MAGIC:
        raise StxError("not a Pasti image (no RSY header)")
    version, tool, _res, track_count, revision = struct.unpack_from("<HHHBB", blob, 4)
    if version != 3:
        raise StxError(f"Pasti version {version}: only version 3 is known")
    pos = 16
    tracks = []
    for _ in range(track_count):
        if pos + 16 > len(blob):
            raise StxError(f"track record at {pos:#x} past the end of the file")
        rec_size, fuzzy_count, sector_count, flags, track_length, number, _type = \
            struct.unpack_from("<IIHHHBB", blob, pos)
        if rec_size < 16 or pos + rec_size > len(blob):
            raise StxError(f"track record at {pos:#x}: size {rec_size} out of range")
        rec = blob[pos:pos + rec_size]
        t = Track(track=number & 0x7F, side=number >> 7, flags=flags, track_length=track_length)
        if not flags & TRK_SECT:
            body = rec[16:]
            for n in range(sector_count):
                data = body[n * 512:(n + 1) * 512]
                if len(data) != 512:
                    raise StxError(f"track {t.track}/{t.side}: standard sector {n + 1} cut short")
                t.sectors.append(Sector(t.track, t.side, n + 1, 512, 0, 0, 0, data))
        else:
            descs = []
            p = 16
            for _ in range(sector_count):
                descs.append(struct.unpack_from("<IHHBBBBHBB", rec, p))
                p += 16
            fuzzy = rec[p:p + fuzzy_count]
            p += fuzzy_count
            data_start = p
            if flags & TRK_IMAGE:
                q = p
                if flags & TRK_SYNC:
                    q += 2
                (image_size,) = struct.unpack_from("<H", rec, q)
                t.image = rec[q + 2:q + 2 + image_size]
            fz = 0
            for (offset, bitpos, rtime, idt, ids, idn, idsize, _crc, status, _sf) in descs:
                size = 128 << (idsize & 3)
                data = None
                if not status & FDC_RNF:
                    a = data_start + offset
                    data = rec[a:a + size]
                    if len(data) != size:
                        raise StxError(f"track {t.track}/{t.side}: sector {idn} data out of the record")
                mask = None
                if status & FDC_FUZZY:
                    mask = fuzzy[fz:fz + size]
                    fz += size
                t.sectors.append(Sector(idt, ids, idn, size, status, bitpos, rtime, data, mask))
        tracks.append(t)
        pos += rec_size
    return Image(version, tool, revision, tracks)


def read(path: str | Path) -> Image:
    return parse(Path(path).read_bytes())


def flatten(img: Image) -> tuple[bytes, list[str]]:
    """Every sector with data in physical track, side, then sector-ID order."""
    out = bytearray()
    lines = []
    for t in sorted(img.tracks, key=lambda t: (t.track, t.side)):
        for s in sorted(t.sectors, key=lambda s: s.number):
            if s.data is None:
                lines.append(f"t{t.track:02d} s{t.side} id{s.number:3d}: no data")
                continue
            lines.append(f"t{t.track:02d} s{t.side} id{s.number:3d}: {len(out):#08x} +{s.size}")
            out += s.data
    return bytes(out), lines


def describe(img: Image) -> str:
    rows = [f"Pasti v{img.version} tool {img.tool:#x} revision {img.revision}, {len(img.tracks)} tracks"]
    for t in img.tracks:
        ids = [s.number for s in t.sectors]
        odd = []
        for s in t.sectors:
            if s.track != t.track or s.side != t.side:
                odd.append(f"id {s.number} says t{s.track}/s{s.side}")
            if s.size != 512:
                odd.append(f"id {s.number} {s.size} B")
            if s.fdc_status & ~FDC_FUZZY:
                odd.append(f"id {s.number} status {s.fdc_status:#04x}")
            if s.fuzzy is not None:
                odd.append(f"id {s.number} fuzzy")
        std = ids == list(range(1, len(ids) + 1))
        rows.append(f"t{t.track:02d} s{t.side} flags {t.flags:#04x} {len(ids):2d} sectors"
                    + ("" if std else f" ids {ids}")
                    + (" image" if t.image is not None else "")
                    + (f"  [{'; '.join(odd)}]" if odd else ""))
    return "\n".join(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("info"); a.add_argument("image")
    a = sub.add_parser("sector"); a.add_argument("image")
    a.add_argument("track", type=int); a.add_argument("side", type=int); a.add_argument("number", type=int)
    a.add_argument("-o", "--out")
    a = sub.add_parser("flat"); a.add_argument("image"); a.add_argument("-o", "--out", required=True)
    args = ap.parse_args()
    try:
        img = read(args.image)
    except (OSError, StxError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    if args.cmd == "info":
        print(describe(img))
    elif args.cmd == "sector":
        t = img.track(args.track, args.side)
        s = next((s for s in (t.sectors if t else []) if s.number == args.number), None)
        if s is None or s.data is None:
            print("error: no such sector, or no data", file=sys.stderr)
            return 1
        if args.out:
            Path(args.out).write_bytes(s.data)
        else:
            for i in range(0, len(s.data), 16):
                row = s.data[i:i + 16]
                print(f"{i:04x}  {row.hex(' ')}  {''.join(chr(c) if 32 <= c < 127 else '.' for c in row)}")
    else:
        data, lines = flatten(img)
        Path(args.out).write_bytes(data)
        Path(args.out + ".map").write_text("\n".join(lines) + "\n")
        print(f"{len(data)} bytes, {len(lines)} sectors -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
