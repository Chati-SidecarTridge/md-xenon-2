# Changelog

## v1.0.0 (2026-10-25)

**Xenon 2 Megablast at 50 frames a second.** The Bitmap Brothers' 1989 shoot'em up, the whole
game, on an Atari ST, STE, Mega ST or Mega STE with a SidecarTridge Multi-device. The original
ran at 16.7 frames a second; this one runs at 50.

- **The whole game.** It is rewritten in C from the original's code and checked frame by frame
  against the original in Hatari:
  - all five levels, with their mid-level and end-of-level bosses
  - Crispin's Swop Shop, with every weapon and extra it sells
  - David Whittaker's music and effects
  - two players taking turns
  - the high scores, now kept on the cartridge
- **Two ways to play at 50.**
  - **The sprites at the original's pace:** the world scrolls at 50 while the ship, the shots
    and the enemies move at the original's speed, drawn smoothly in between.
  - **×3:** everything three times as fast.
  - At 17 frames a second it is the original's pace.
- **An options screen.** Press O on the title or the attract. Every option is on a key and kept
  on the cartridge:
  - the frame rate and the sprites' speed
  - the damage a hit takes, the lives and the shots ×3
  - the level to start at
  - the music
  - an STE's sound through the DMA chip or the YM

  The app starts at 50 frames a second with the sprites at the original's pace, a hit taking 1,
  5 lives and the shots ×3. F9 puts these defaults back.
- **Esc goes back to GEM, X to the Multi-device's menu.** Both work from the title, the attract
  and the options. In play, Esc goes back to the attract, as in the original.
- **Your own disks.** Copy the two Pasti `.stx` images of the Image Works release into the
  card's `XENON2` folder. The first start converts them and the five levels once, in about a
  minute, with its progress on the screen. After that each level starts in about two seconds.
  When the disks are missing, or are another release, the screen says so and shows a QR code
  to the instructions.

Thanks to The Bitmap Brothers, The Assembly Line, David Whittaker and Image Works for the game;
to [Hatari](https://www.hatari-emu.org), which every routine was checked against; to
[Atarimania](https://www.atarimania.com) for preserving the disks; and to Nayuki's QR Code
generator.
