#!/usr/bin/env python3
"""Summarise Hatari's FDC trace (a run with --trace fdc) into a load map:
runs of consecutive sector reads, with the disk inserted at the time, the
track, side, sectors, DMA address, bytes and the VBL they started at.

  fdc_map.py RUN_FOLDER [--disks NAME,NAME...]

A run of reads is consecutive when each read lands at the previous one's
address + 512 (or on the next track's first sector). Standard library only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

READ = re.compile(r"^fdc type II read sector sector=0x([0-9a-f]+) .*?tr=0x([0-9a-f]+) .*?side=(\d) "
                  r"drive=(\d) .*?addr=0x([0-9a-f]+) VBL=(\d+) .*?pc=([0-9a-f]+)")
OTHER = re.compile(r"^fdc type (I|III|IV) (\w+)(.*?)VBL=(\d+)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run", type=Path)
    ap.add_argument("--disks", default="disk1,disk2", help="names for the disks, in the order inserted")
    ap.add_argument("--commands", action="store_true", help="also list seeks and other commands")
    args = ap.parse_args()
    names = args.disks.split(",")
    disk = -1
    runs = []
    cur = None
    for line in (args.run / "hatari.out").read_text(errors="replace").splitlines():
        if line.startswith("fdc insert drive=0"):
            disk += 1
            cur = None
            runs.append(dict(event=f"-- {names[disk] if disk < len(names) else f'disk#{disk}'} inserted"))
            continue
        m = READ.match(line)
        if m:
            sec, tr, side, drive, addr, vbl, pc = (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)),
                                                   int(m.group(4)), int(m.group(5), 16), int(m.group(6)), m.group(7))
            if cur and "event" not in cur and addr == cur["addr"] + cur["bytes"] and side == cur["side"]:
                cur["bytes"] += 512
                cur["last"] = (tr, sec)
                cur["count"] += 1
            else:
                cur = dict(disk=disk, first=(tr, sec), last=(tr, sec), side=side, addr=addr, bytes=512,
                           vbl=vbl, pc=pc, count=1)
                runs.append(cur)
            continue
        if args.commands:
            m = OTHER.match(line)
            if m:
                runs.append(dict(event=f"   VBL {m.group(4):>6} type {m.group(1)} {m.group(2)}{m.group(3)[:60]}"))
    for r in runs:
        if "event" in r:
            print(r["event"])
            continue
        name = names[r["disk"]] if 0 <= r["disk"] < len(names) else "?"
        print(f"{name}  t{r['first'][0]:02d}s{r['first'][1]:02d}-t{r['last'][0]:02d}s{r['last'][1]:02d} side {r['side']}"
              f"  {r['count']:4d} sectors -> ${r['addr']:06x}-${r['addr'] + r['bytes'] - 1:06x}"
              f"  ({r['bytes']:6d} B)  VBL {r['vbl']}  pc ${r['pc']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
