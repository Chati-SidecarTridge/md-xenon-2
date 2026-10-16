# Xenon 2 Megablast for the SidecarTridge Multi-device

Xenon 2 Megablast, the 1989 vertical shooter, re-made for the
[SidecarTridge Multi-device](https://sidecartridge.com) on an Atari ST, STE,
Mega ST or Mega STE, to run at **50 frames a second**. The original paced
itself to 16.7. The Raspberry Pi Pico (RP2040) in the cartridge runs the
game, rewritten in C; the ST shows its frames, plays its sound and reads the
joystick.

**Status: in development.** Not playable yet: the cartridge boots to its own
screen. The original has been studied (how it loads, its frame rate, its
screen); the converter and the title screen come next.

## What you will need

- Your own copy of the original game: the **Image Works release, two disks**.
  None of the game is in this repository or in the firmware. A preserved
  copy of the disks (two Pasti `.stx` images) is at
  <https://www.atarimania.com/st/dumps/xenon_ii_-_megablast_imageworks.zip>.
- A SidecarTridge Multi-device with a microSD card, and an Atari ST, STE,
  Mega ST or Mega STE with a colour monitor and a joystick.

The two disk images go into the card's `XENON2` folder; the cartridge will
convert what it needs from them once, at its first start. (The Kixx
re-release is a different disk and is not supported.)

## Building it

```bash
# ./build.sh <board> <build_type> <app_uuid>
#   board:      pico_w
#   build_type: debug | release
#   app_uuid:   44444444-4444-4444-8444-444444444444 for local builds
./build.sh pico_w release 44444444-4444-4444-8444-444444444444
```

The ARM GNU Toolchain 14.2 (`PICO_TOOLCHAIN_PATH`) and
[atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker)
are needed; `CLAUDE.md` has the details. `make -C tests/host test` runs the
host tests.

## How it is made

The app is built on
[md-framebuffer-template](https://github.com/sidecartridge/md-framebuffer-template)
v1.1.0, whose framework does the ST's side: the frame the RP draws, one byte
per pixel, is converted to the ST's planes and copied to the screen every
frame, and the keyboard, the joystick and the sound go through the
cartridge. The template's README is the guide to that framework's API.

`tools/re/` holds the tools used to study the original from its disks: a
Pasti reader, a scripted Hatari harness, the extraction of the game and its
levels, golden frames (see [its README](tools/re/README.md)). What they
produce stays outside the repository.

## License

The code in this repository is GPL v3.0 (see [LICENSE](LICENSE)). Xenon 2
Megablast is The Bitmap Brothers' game, published by Image Works in 1989; it
belongs to its rights holders and is not part of this repository.
