# The converted set

The cartridge reads the user's own copy of the original game, the two disk
images of the Image Works release (Pasti `.stx`), from the app's folder on the
SD card (`/XENON2` by default). At every start it checks the set the
converter made from them in `/XENON2/SET`; when the set is missing, made by
another version of the converter, or a file is missing or of another size, it
makes the whole set again (a few seconds), with its progress on the ST.

| File | Bytes | What |
| --- | --- | --- |
| `BOOT.X2` | 163,840 | the boot-time program (disk 1 tracks 1-32) as it sits in the ST's memory once it has decrypted itself |
| `GAME.X2` | 235,520 | the game (disk 1 tracks 34-79) as it sits in the ST's memory once decrypted |
| `TITLE.X2` | 32,032 | an ST low-resolution screen (32,000 bytes: black, the logo band the boot program shows at full scale) and its 16 palette words (big-endian, `0RRR0GGG0BBB`) |
| `LEVEL1.X2` ... `LEVEL5.X2` | 124,060 ... 183,880 | each level's data, unpacked as the game unpacks it before the level starts |
| `SET.X2M` | text | the manifest, written last |

The manifest's first line is `X2SET <version>` (`X2SET_VERSION` in
`rp/src/include/x2set.h`); then one line per file, in the order above:
`<name> <size> <crc32 in hex>`. A set is complete when the version matches and
every file it lists has its size. Each file's CRC-32 is checked against the
known release when it is made (`x2_known` in `rp/src/x2game.c`); disks that
convert to anything else are refused with a message, and no manifest is
written.

The converter is `rp/src/x2game.c` (with `stx.c`, `lzhuf.c`, `crc32.c`) on
the cartridge and in `tools/x2conv` on a computer: the same code, the same
bytes. `tools/re/x2disk.py` is the reference it is tested against.

## A level file

A level sits in the ST's memory at `$4F000`; every address inside it is an
absolute ST address, so its offset in the file is the address minus
`$4F000`. All values are big-endian.

| Offset | What |
| --- | --- |
| `+$00` | long: the backdrop's address |
| `+$04` | long: the map's address |
| `+$08` | long: the tile block's address |
| `+$10` | `bra.w`: the wave handler, whose jump table names the enemy types |
| `+$14` | long: the scenery table's address |
| `+$1C` | long: the palette's address (16 words) |
| `+$28` | long: the enemies' shot sprite |
| `+$30` / `+$34` | longs: the path table's address, and the start of the paths it points at |
| `+$38` | long: the wave table's address |
| `+$0C` ... `+$2C` | other `bra.w` entries: the level's own code, which the game calls at level start and every frame |

- **The map**: 20 words a row, about 300 rows, the level's start at the
  end of the map (it scrolls from the last row towards the first). A word
  of 0 shows the backdrop; a positive word is an opaque tile at
  `tiles + word * 16`; a negative one a masked tile at
  `tiles + (word & $7FFF) * 16`.
- **A tile**: 16 x 16 pixels. Opaque: 128 bytes, 8 a line (the four
  planes, a word each). Masked: 192 bytes, 12 a line: the mask, planes
  0-1, planes 2-3 (two words each). Where the mask is set, the backdrop's
  planes 0-1 show through under the tile's own bits.
- **The backdrop**: 320 x 192 pixels of two planes (colours 0-3), stored as
  20 columns of 192 lines, 4 bytes a line; it repeats vertically. Under a
  backdrop cell planes 2-3 are 0.

- **The wave table and the scenery table**: records of 14 bytes (seven
  words), ended by a negative word. Each starts with the world line that
  triggers it: the game fires a record on the frame the playfield's top
  line reaches it. A wave: line, kind (0 a capsule, 1-10 an enemy type),
  how many, the path (from 1), the spacing (below 100 a delay in path steps
  between members; from 100, value - 100 pixels across), the firing (0
  none; 1-99 a random direction at that rate; from 100, aimed at the ship),
  the path steps a frame (for a capsule, the token it holds). A scenery
  record: line, kind (0 a restart point, 1-3 a wall object drawn in the
  map, 4 a wall crawler), a word, the crawler's travel (16-line steps), x,
  the world line of its top-left cell, the side (0 left, 1 right).
- **An enemy type**: 24 bytes a part (the wave handler loads the first
  part's address for kinds 1-10; a non-zero word after a part starts the
  next): the object type, the score it is worth, its update, draw and hit
  routines (the game's), its animation (pairs of a sprite and the frames
  it shows), a word, two flags (two hit points instead of one; the parts
  destroyed together).
- **A path**: word opcodes. 10 x y: the start position; 2 direction rate
  acceleration steps: an arc; 4 n: wait n steps; 8 offset: go to the path's
  start + offset; 6 and eight offsets: one at random; 0: the end. A step
  moves the enemy by the game's 256-direction sine table, 1/64 pixel a
  unit, then turns the direction by the rate (1/256 a step), the rate by
  the acceleration. `tools/re/waves.py` decodes the tables and steps a path
  as the original does.

The file holds the map as the level starts: while it plays, the level's
own code rewrites some of its words (a scenery object's cells take other
tiles of the block), so a renderer of a running game reads the map from
the game's memory.

The playfield is the top 192 lines of the screen. The map moves a line per
original frame; the backdrop half as fast. `tools/re/render_level.py` draws
a playfield from a level file exactly as the original does.
