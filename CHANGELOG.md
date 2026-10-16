# Changelog

## v1.0.0 (unreleased)

Xenon 2 Megablast for the SidecarTridge Multi-device, built on
md-framebuffer-template v1.1.0.

- The template's demos, games and examples are removed; the cartridge boots
  to the app's own screen and reads its SD card folder `/XENON2`.
- At every start the cartridge checks the converted set in `/XENON2/SET` and, when it is
  missing or out of date, makes it from the two disk images of the Image Works release in
  `/XENON2`, with its progress on the ST; other disks are refused with a message. Then it shows
  the original's logo in its colours. `docs/set-format.md` describes the set; `tools/x2conv`
  makes the same set on a computer.
- Space or fire on the title starts level 1's world: drawn every frame at 50 fps, pixel for pixel
  as the original draws it, and scrolled at the original's pace with its status bar below. The
  first start converts the level into the cartridge's flash (a few seconds, with its progress on
  the ST); later starts, power cycles included, find it there.
- Level 1 shows the Megablaster at its start and the status bar with its starting score, lives and
  shield, drawn from the original's own graphics, which the first start converts into the
  cartridge's flash with the level.
- `tools/re/`: tools to study the original from the user's own disks: a Pasti
  reader, a scripted Hatari harness, the extraction of the game image and its
  five levels, golden frames checked against Hatari's screenshots.
