# Reverse-engineering tools

Tools for studying the original Xenon 2 Megablast (Atari ST, Image Works,
1989) from the user's own copy of its two disks. Nothing of the game is in
this folder: the tools read the disk images and the emulator's memory, and
everything they produce (disk images, RAM dumps, listings, screenshots)
belongs outside the repository.

## Setup

- Hatari (2.6.1 used here), Python 3 (standard library only), and Docker
  with `logronoide/atarist-toolkit-docker-x86_64:1.4.0` for the 68000
  disassembler.
- The original disks: the Image Works release from
  <https://www.atarimania.com/st/dumps/xenon_ii_-_megablast_imageworks.zip>
  (two Pasti `.stx` images). Keep them, and everything made from them,
  outside the repository (for example `~/xenon2/`).
- `cp re.cfg.example re.cfg` and fill in the paths: Hatari, a TOS 1.04 image
  (ST) and a TOS 1.62 image (STE), the two disk images, and `RUNS`, where
  run folders go. `re.cfg` is gitignored.

## Tools

| Tool | What it does |
| --- | --- |
| `stx.py` | Reads Pasti images: `info` (tracks, sector IDs, CRC errors, fuzzy bits), `sector`, `flat` (all sectors in track and ID order, as a raw image) |
| `x2disk.py` | Extracts the game image and the five levels from the two disk images, as the original's loader does: decryption of the game, the level stream's virtual sectors, LZHUF unpacking |
| `lzhuf.py` | LZHUF decoding as in LHarc's `-lh1-` method |
| `hatari_run.py` | Runs the original in Hatari, headless and scripted: one folder per run (`command.txt`, `hatari.out`, `hatari.log`, `shots/`, the run's own copies of the disks, `result.txt`); expectation files wait on Hatari's output (an FDC trace line, say), press keys and fire, swap disks, run debugger commands and stage debugger command files |
| `fdc_map.py` | Turns a run's FDC trace (`--trace fdc`) into a load map: which disk, tracks and sectors went to which address, and when |
| `disasm.sh` | Disassembles a raw 68000 binary at a given address with the toolkit's objdump |
| `st_screen.py` | ST low-resolution screen memory to palette indices and PNG; reads PNGs |
| `golden.py` | Renders a dumped screen and its palette as a PNG, and checks it pixel for pixel against Hatari's own screenshot |

Tests on synthetic data: `tests/host/test_stx.py`, `tests/host/test_x2disk.py`
(`make -C tests/host test`).

## Scripts (`scripts/`)

Expectation files (`*.exp`) for `hatari_run.py --expect`, debugger command
files (`*.ini`, staged into the run folder) and debugger scripts (`*.dbg`,
for `--parse`). Run them with `--trace fdc`: most wait for floppy events
rather than for time, because Hatari runs fast-forwarded and host seconds
mean little to the ST.

| Script | What it shows |
| --- | --- |
| `boot.exp`, `attract-fdc.exp` | the boot, the disk swap, the attract sequence |
| `stage2.dbg` | a breakpoint where the boot sector hands over, with a RAM dump |
| `level1.exp`, `start-level1.exp`, `ready-fires.exp` | starting a one-player game |
| `to-poll.exp` | RAM when disk 1 is loaded and the game waits for disk 2 |
| `frame-rate.exp`, `play.exp` | the frame rate, in a quiet stretch and in play |
| `screen.exp`, `screen-back.exp` | twenty consecutive frames and their palettes |
| `golden-title.exp`, `golden-level1.exp` | golden frames |
| `find-lives.exp`, `find-shield.exp`, `who-builds.exp`, `level-stream.dbg`, `watch-stick.dbg`, `ready-probe.exp`, `shop-search.exp` | the probes that found the variables and the loaders |

Example: the frame rate in play, then a golden frame:

```bash
tools/re/hatari_run.py --trace fdc --expect tools/re/scripts/play.exp --name play
tools/re/hatari_run.py --trace fdc --expect tools/re/scripts/golden-title.exp --name title
tools/re/golden.py <run folder> title --check <run folder>/shots/title.png
```

## Hatari's debugger: what works

- `b <condition> :trace :file <file>` logs without stopping and runs the
  file's commands; `:once` removes the breakpoint after it fires; `:N` fires
  on every Nth hit.
- A quoted expression is evaluated when the command runs: `VBL > "VBL"`
  waits for a later frame; `"($406).l"` reads a pointer; the video base is
  `"($ff8201).b*65536+($ff8203).b*256"` (`<<` and `|` do not evaluate as
  expected there).
- Hatari ignores a breakpoint whose condition repeats one already set: add
  a condition to tell them apart.
- `parse <file>` runs a command file; `:file` is a breakpoint option only.
- `savebin <file> <address> <length>` also reads the hardware registers
  (`$FF8240`: the palette).
- `w` writes RAM, but behind the I/O handlers. The control socket's key
  events reach the keyboard, not the joystick; the right mouse button is
  joystick 1's fire while the game has the mouse off.
