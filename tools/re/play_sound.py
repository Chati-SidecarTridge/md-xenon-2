#!/usr/bin/env python3
"""Render the original's sound to WAV, for listening next to Hatari's.

  play_sound.py tune BOOT.X2 -o tune.wav [--seconds S] [--pcm]
  play_sound.py log RUN/hatari.out -o out.wav [--from S] [--seconds S]

tune: the title tune as the boot program plays it. Timer A at 2.4576 MHz /
4 / 77 (7,979 Hz) reads one unsigned sample a tick, through a list of
sections (a count of samples and a start address each; a count of 0 starts
the list again), and writes the sound chip's three volumes from a 256-entry
table, 8 bytes an entry: register 8 and its value, 9 and its value, then
10 and its value (the boot program's Timer A handler; the list, the table
and the samples are read from BOOT.X2 where the boot program keeps them).
--pcm writes the samples themselves instead, as 8-bit PCM at 7,979 Hz.

log: the sound chip's writes in a Hatari run with --trace psg_write
("ym write data reg=R val=V video_cyc=C"), in time order. A frame has
160,256 cycles of the 8 MHz clock; a write whose cycle is lower than the
one before starts a new frame. --from skips seconds of the log.

The chip (YM2149 at 2 MHz): three square-wave tones (registers 0-5, half a
period every 8 x period cycles), a 17-bit noise generator (register 6), the
mixer (7: a set bit turns a tone or the noise off for a channel, the
channel then holding its level), the volumes (8-10, bit 4 for the
envelope) on the YM's 32-step logarithmic curve, the envelope (period
11-12, shape 13; a step every 8 x period cycles). The three channels are
summed; a slow high-pass removes the DC level, as the ST's output stage
does. Standard library only; nothing of the game is in this file.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import wave
from pathlib import Path

RATE = 44100
YM_CLOCK = 2_000_000
TICKS = YM_CLOCK / 8 / RATE  # tone and envelope counter steps a sample
ST_CLOCK = 8_010_613
FRAME_CYCLES = 160_256
TUNE_RATE = 2_457_600 / 4 / 77
BOOT_BASE = 0x40000
TUNE_SECTIONS = 0x41760
TUNE_TABLE = 0x41822

# The YM2149's 32 output levels (Ayumi's measured table).
LEVELS = [0.0, 0.0, 0.00465400167849, 0.00772106507973, 0.0109559777218,
          0.0139620050355, 0.0169985503929, 0.0200198367285, 0.024368657969,
          0.029694056611, 0.0350652323186, 0.0403906309606, 0.0485389486534,
          0.0583352407111, 0.0680552376593, 0.0777752346075, 0.0925154497597,
          0.111085679408, 0.129747463188, 0.148485542077, 0.17666895552,
          0.211551079576, 0.246387426566, 0.281101701381, 0.333730067903,
          0.400427252613, 0.467383840696, 0.53443198291, 0.635172045472,
          0.75800717174, 0.879926756695, 1.0]


def fixed_level(v: int) -> float:
    return LEVELS[(v & 15) * 2 + 1] if v & 15 else 0.0


class YM:
    def __init__(self) -> None:
        self.reg = [0] * 16
        self.tone_count = [0.0, 0.0, 0.0]
        self.tone_out = [0, 0, 0]
        self.noise_count = 0.0
        self.noise_lfsr = 1
        self.noise_out = 0
        self.env_count = 0.0
        self.env_step = 0
        self.env_hold = False

    def write(self, r: int, v: int) -> None:
        if r > 13:
            return
        self.reg[r] = v
        if r == 13:
            self.env_step = 0
            self.env_hold = False
            self.env_count = 0.0

    def env_level(self) -> float:
        shape = self.reg[13] & 15
        if shape < 8:  # one ramp, then silence: as shape 9 or 15
            shape = 15 if shape & 4 else 9
        attack, alternate, hold = bool(shape & 4), bool(shape & 2), bool(shape & 1)
        cycle, pos = divmod(self.env_step, 32)
        if cycle and hold:
            return LEVELS[31 if attack ^ alternate else 0]
        up = attack ^ (alternate and cycle % 2 == 1)
        return LEVELS[pos if up else 31 - pos]

    def sample(self) -> float:
        r = self.reg
        for c in range(3):
            period = (r[2 * c] | (r[2 * c + 1] & 15) << 8) or 1
            self.tone_count[c] += TICKS
            while self.tone_count[c] >= period:
                self.tone_count[c] -= period
                self.tone_out[c] ^= 1
        nperiod = (r[6] & 31) or 1
        self.noise_count += TICKS / 2
        while self.noise_count >= nperiod:
            self.noise_count -= nperiod
            bit = (self.noise_lfsr ^ (self.noise_lfsr >> 3)) & 1
            self.noise_lfsr = (self.noise_lfsr >> 1) | (bit << 16)
            self.noise_out = self.noise_lfsr & 1
        eperiod = (r[11] | r[12] << 8) or 1
        self.env_count += TICKS
        while self.env_count >= eperiod:
            self.env_count -= eperiod
            self.env_step += 1
        env = None
        out = 0.0
        mix = r[7]
        for c in range(3):
            tone_on = not (mix >> c & 1)
            noise_on = not (mix >> (c + 3) & 1)
            gate = (self.tone_out[c] or not tone_on) and (self.noise_out or not noise_on)
            if not gate:
                continue
            v = r[8 + c]
            if v & 16:
                if env is None:
                    env = self.env_level()
                out += env
            else:
                out += fixed_level(v)
        return out / 3


def high_pass(samples: list[float]) -> list[float]:
    out, prev_in, prev_out, a = [], 0.0, 0.0, 0.9995
    for x in samples:
        y = a * (prev_out + x - prev_in)
        prev_in, prev_out = x, y
        out.append(y)
    return out


def write_wav(path: Path, samples: list[float], rate: int = RATE) -> None:
    peak = max((abs(s) for s in samples), default=0) or 1.0
    scale = 0.9 * 32767 / peak
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(s * scale)) for s in samples))


def tune_sections(boot: bytes) -> list[tuple[int, int]]:
    out, a = [], TUNE_SECTIONS
    while True:
        count, start = struct.unpack_from(">II", boot, a - BOOT_BASE)
        if count == 0:
            return out
        out.append((count, start))
        a += 8


def tune_samples(boot: bytes, seconds: float) -> bytes:
    want, out = int(seconds * TUNE_RATE), bytearray()
    sections = tune_sections(boot)
    while len(out) < want:
        for count, start in sections:
            out += boot[start - BOOT_BASE:start - BOOT_BASE + count]
    return bytes(out[:want])


def render_tune(boot: bytes, seconds: float) -> list[float]:
    """Each sample held for its Timer A period, the three volumes summed."""
    table = boot[TUNE_TABLE - BOOT_BASE:TUNE_TABLE - BOOT_BASE + 256 * 8]
    levels = []
    for s in range(256):
        e = table[s * 8:s * 8 + 8]
        vols = {e[0]: e[1], e[2]: e[3]}
        vols[e[4] if e[4] in (8, 9, 10) else 10] = e[5]
        levels.append(sum(fixed_level(vols.get(r, 0)) for r in (8, 9, 10)) / 3)
    out, t = [], 0.0
    step = TUNE_RATE / RATE
    data = tune_samples(boot, seconds)
    for i in range(int(seconds * RATE)):
        out.append(levels[data[min(int(t), len(data) - 1)]])
        t += step
    return high_pass(out)


LOG_LINE = re.compile(r"^ym write data reg=0x([0-9a-f]+) val=0x([0-9a-f]+) video_cyc=(\d+)")


def render_log(path: Path, start: float, seconds: float) -> list[float]:
    ym, out = YM(), []
    frame, last_cyc, now = 0, -1, 0
    end = start + seconds
    with open(path, errors="replace") as f:
        for line in f:
            m = LOG_LINE.match(line)
            if not m:
                continue
            cyc = int(m.group(3))
            if cyc < last_cyc:
                frame += 1
            last_cyc = cyc
            t = (frame * FRAME_CYCLES + cyc) / ST_CLOCK
            if t >= end:
                break
            if t >= start:
                while now < (t - start) * RATE:
                    out.append(ym.sample())
                    now += 1
            ym.write(int(m.group(1), 16), int(m.group(2), 16))
    while now < seconds * RATE:
        out.append(ym.sample())
        now += 1
    return high_pass(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="what", required=True)
    t = sub.add_parser("tune")
    t.add_argument("boot", type=Path)
    t.add_argument("-o", "--out", type=Path, required=True)
    t.add_argument("--seconds", type=float, default=27.0)
    t.add_argument("--pcm", action="store_true", help="the samples as 8-bit PCM")
    lg = sub.add_parser("log")
    lg.add_argument("log", type=Path)
    lg.add_argument("-o", "--out", type=Path, required=True)
    lg.add_argument("--from", dest="start", type=float, default=0.0)
    lg.add_argument("--seconds", type=float, default=20.0)
    args = ap.parse_args()
    if args.what == "tune":
        boot = args.boot.read_bytes()
        if args.pcm:
            data = tune_samples(boot, args.seconds)
            with wave.open(str(args.out), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(1)
                w.setframerate(round(TUNE_RATE))
                w.writeframes(data)
        else:
            write_wav(args.out, render_tune(boot, args.seconds))
    else:
        write_wav(args.out, render_log(args.log, args.start, args.seconds))
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
