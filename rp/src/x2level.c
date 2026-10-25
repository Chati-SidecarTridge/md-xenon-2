/**
 * File: x2level.c
 * Description: A level converted for drawing at 50 fps (x2level.h).
 *
 * The original's level, as it sits at $4F000: its first longs are the
 * addresses of its blocks (+$00 the backdrop, +$04 the map, +$08 the tile
 * block, +$1C the palette; the others are code entries and values). The map
 * is 20 big-endian words a row and ends where the next block starts. The
 * backdrop is 20 columns of 192 lines of planes 0-1, 4 bytes a line. The
 * tile block is a run of records from its 16-byte unit 1 to its end, each
 * opaque (16 lines of planes 0-3, a word each: 128 bytes) or masked (16
 * lines of a mask for planes 0-1, planes 0-1, planes 2-3: 192 bytes); a map
 * word is a record's offset / 16, negative for a masked one.
 *
 * A record's kind is in the words that name it, but the level's code names
 * tiles the map does not (a scenery object's cells take other tiles as it
 * comes into view). So the block is walked from its start and each record
 * classified by its content: a masked tile never sets a pixel where its
 * mask keeps the backdrop. On the five levels of the Image Works release
 * this tells every record the map names correctly, and the walk ends at the
 * block's end.
 *
 * The enemies' sprites are found from their animation lists: the level's
 * wave handler (the bra.w at +$10) is `add.w d0,d0` twice, `movea.l
 * (pc,d0.w),a2`, `jmp (a2)`, then a table of 11 entries, kinds 1-10 each a
 * `lea abs.l,a2` (level 3's kind 3 after setting the big enemies' hit
 * points, `move.w $426,-(sp); move.w abs.l,$426`) to the kind's type
 * record (24 bytes a part, the animation
 * list at +16; a non-zero word after a part starts the next); +$28 is the
 * enemies' shot sprite. The level's own code names more, at addresses
 * known per level (extras): level 1's wall crawler ($4F8DC) shows the
 * lists at $4F3D4, $4F412 and withdraws with $4F3F8, $4F436; the wall
 * objects' shots are sprites through a table of pointers at $67BD2; its end
 * boss's eye is one of four sprites ($50866), its turrets one sprite
 * ($5405A) or one of eight by direction ($5058A), their shot the list at
 * $4F4B0; its worms' parts one of eight sprites by direction ($50374 the
 * first part, $50394 the others), a destroyed part's pieces the lists at
 * $4F474 and $4F450.
 */

#include "x2level.h"

#include <stdlib.h>
#include <string.h>

#define HEAD_SIZE 64u   /* the level's header: its first 16 longs */
#define HEAD_LONGS 15u  /* scanned for block addresses */
#define OPAQUE_SIZE 128u
#define MASKED_SIZE 192u
#define UNIT 16u        /* the tile block's granularity */
#define TYPE_PART 24u
#define MAX_PARTS 8
#define LEA_A2 0x45F9u      /* lea abs.l,a2 */
#define MOVE_W_PUSH 0x3F38u /* move.w abs.w,-(sp) */
#define AROUND_SIZE 12u     /* it and move.w abs.l,abs.w before the lea */
#define KINDS 10
#define CHUNK 256u

#define EXTRA_ANIMS 48
#define EXTRA_TABLES 8
#define EXTRA_SINGLES 12
#define EXTRA_HELD 32

typedef struct {
  uint32_t anims[EXTRA_ANIMS];
  uint32_t shot_table; /* pointers to pointers to sprites */
  uint32_t shots;
  uint32_t tables[EXTRA_TABLES][2]; /* tables of sprite pointers: address, count */
  uint32_t singles[EXTRA_SINGLES];
  uint32_t held[EXTRA_HELD]; /* sprites of lists ending held (duration 0) */
} extras_t;

static const extras_t extras[6] = {
    [1] = {{0x4F3D4, 0x4F412, 0x4F3F8, 0x4F436, 0x4F4B0, 0x4F450, 0x4F474},
           0x67BD2,
           5,
           {{0x50866, 4}, {0x5058A, 8}, {0x50374, 8}, {0x50394, 8}},
           {0x5405A},
           {0}},
    [2] = {{0x4FF14, 0x4FED2, 0x4F21C, 0x4F24C, 0x4F234, 0x4F252, 0x4F420,
            0x4F444, 0x4F468, 0x4F48C, 0x4F4B0, 0x4F4D4, 0x4F4F8, 0x4F51C,
            0x4F264, 0x4F2A0, 0x4F2DC, 0x4F318, 0x4F33C, 0x4F3C0, 0x4F3A8,
            0x4F390, 0x4F378, 0x4F360, 0x4F408, 0x4F3F0, 0x4F3D8, 0x5108C,
            0x510AA},
           0x6BA5E,
           5,
           {{0x509FC, 8}, {0x50A1C, 8}, {0x50A3C, 8}},
           {0x57078},
           /* the crawlers' firing ($4FF44, $4FF02): each ends held */
           {0x57224, 0x57188, 0x570D8, 0x56E7C, 0x56F2C, 0x56FC8}},
    /* level 3: the edge enemy's, the turrets', the lizards' tails, the
     * walkers', the big walls' bursts, the mid boss's parts, blink and
     * shot, the end boss's head and shot; the snake's, the claw's and the
     * end boss's parts' sprites by direction */
    [3] = {{0x4F34E, 0x4F372, 0x4F396, 0x4F3BA, 0x4F9D0, 0x4F9F4, 0x4FA18,
            0x4FA3C, 0x4FA60, 0x4FA84, 0x4FAA8, 0x4FACC, 0x502B4, 0x502DE,
            0x509CE, 0x509F2, 0x50A1C, 0x50F08, 0x50EE4, 0x50EC0, 0x50E9C,
            0x50E78, 0x50E54, 0x50E30, 0x50E0C, 0x4FC1C, 0x4FC04, 0x4FC22,
            0x4FC3A, 0x4FC6A, 0x4FC70, 0x4FC40, 0x4FD14, 0x4F6C2, 0x4F710,
            0x4F740, 0x4F73A, 0x4F716, 0x4F6EC, 0x4F6E6, 0x4F69E},
           0x6D2F8,
           5,
           {{0x4F278, 8}, {0x4F298, 8}, {0x4F2B8, 8}, {0x4FFAC, 16},
            {0x4F78E, 8}, {0x4F7D2, 8}, {0x4F822, 8}},
           {0x554B2, 0x55402, 0x55562, 0x555B8, 0x588D0, 0x59290, 0x59322,
            0x5B668, 0x5DDC4, 0x580B8, 0x58378},
           /* a walker stopping, left ($50A40); the end boss's head,
            * one way ($4F764) */
           {0x515EE, 0x5172A, 0x518CA, 0x51A6A, 0x5BD64}},
    [4] = {{0x4F38A, 0x4F3BA, 0x4F3EA, 0x4F41A, 0x50232, 0x502AA, 0x5074E,
            0x50C70, 0x50C94,
            /* by direction: the creatures', the mid boss's jaw and pods',
             * the end boss's claws' shots */
            0x4F7F4, 0x4F824, 0x4F854, 0x4F884, 0x4F8B4, 0x4F8E4, 0x4F794,
            0x4F7C4, 0x50904, 0x50928, 0x5094C, 0x50970, 0x50994, 0x5030A,
            0x50322, 0x5027A, 0x50292, 0x502C2, 0x502DA, 0x502F2, 0x50050,
            0x50068, 0x50080, 0x50098, 0x500B0},
           0x76938,
           5,
           {{0x50772, 4}, {0x507D6, 8}},
           {0x59FCE, 0x59AF2, 0x59A42, 0x5A0E4, 0x5A08E},
           /* the walkers' drops ($4F58E, $4F5D6), the mid boss's parts
            * ($5021A ... $5022C) and the mover ($50EF4, $50ED0) */
           {0x59362, 0x59412, 0x594C2, 0x59572, 0x59622, 0x596D2, 0x59782,
            0x59832, 0x598E2, 0x59992, 0x58C82, 0x58D32, 0x58DE2, 0x58E92,
            0x58F42, 0x58FF2, 0x590A2, 0x59152, 0x59202, 0x592B2, 0x5330A,
            0x5C5C6, 0x5DA46, 0x587C6, 0x55E6A, 0x5570A, 0x560AA, 0x5633A,
            0x5594A, 0x55BDA}},
    [5] = {{0x4F5BC, 0x4F5D4, 0x4F5EC, 0x4F604, 0x4F61C, 0x4F634, 0x4F58C,
            0x4F5A4, 0x4F716, 0x4F73A, 0x4F75E, 0x4F782, 0x4F91E, 0x4F936,
            0x4F94E, 0x4F966, 0x4F97E, 0x4F996, 0x4F9AE, 0x4F9C6, 0x516F8,
            0x51710, 0x51178, 0x4FD64, 0x4FD6A, 0x4FD70, 0x4FD9A, 0x4FDD6,
            0x5017E, 0x50196, 0x501AE, 0x501C6, 0x501DE, 0x501F6, 0x5020E,
            0x50226},
           0x719BC,
           4,
           {{0x5148A, 8}, {0x50850, 8}, {0x5070A, 6}, {0x507BA, 4},
            {0x507CA, 4}, {0x50476, 12}, {0x50142, 15}, {0x5012E, 5}},
           {0x563BE, 0x5816A, 0x594D2, 0x59514},
           {0}},
};

/* Where the tiles end inside their block when the level's code keeps data
 * after them, so the raw copy keeps it: every level's shot table (its last
 * 32 bytes: the enemies' shots' sprites, read through it), level 5's
 * snakes' paths before it; 0: the block is all tiles. */
static const uint32_t tiles_end[6] = {[1] = 0x67BD2, [2] = 0x6BA5E,
                                      [3] = 0x6D2F8, [4] = 0x76938,
                                      [5] = 0x7157A};

typedef struct {
  uint32_t map, map_rows, backdrop, tiles, tiles_size, palette;
} layout_t;

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

/* The block that starts after `addr`: the lowest header address above it,
 * or the level's end. */
static uint32_t block_end(const uint8_t *head, uint32_t size, uint32_t addr) {
  uint32_t end = X2LEVEL_BASE + size;
  for (uint32_t i = 0; i < HEAD_LONGS; i++) {
    uint32_t a = be32(head + 4 * i);
    if (a > addr && a < end) end = a;
  }
  return end;
}

/* The planar tiles the raw copy leaves out: their block, or up to the
 * level's `tiles_end` when its code keeps data inside the block. */
static uint32_t gap_size(uint32_t level, const layout_t *l) {
  uint32_t end = level < 6 ? tiles_end[level] : 0;
  uint32_t tiles = X2LEVEL_BASE + l->tiles;
  if (end > tiles && end < tiles + l->tiles_size) return end - tiles;
  return l->tiles_size;
}

static int layout(const uint8_t *head, uint32_t size, layout_t *l) {
  uint32_t top = X2LEVEL_BASE + size;
  uint32_t backdrop = be32(head + 0x00), map = be32(head + 0x04);
  uint32_t tiles = be32(head + 0x08), palette = be32(head + 0x1C);
  if (backdrop < X2LEVEL_BASE || backdrop + X2LEVEL_BACKDROP_BYTES > top ||
      map < X2LEVEL_BASE || map >= top || tiles < X2LEVEL_BASE ||
      tiles >= top || palette < X2LEVEL_BASE || palette + 32 > top) {
    return X2_ERR_FORMAT;
  }
  l->map = map - X2LEVEL_BASE;
  l->map_rows = (block_end(head, size, map) - map) / (X2LEVEL_COLS * 2);
  l->backdrop = backdrop - X2LEVEL_BASE;
  l->tiles = tiles - X2LEVEL_BASE;
  l->tiles_size = (block_end(head, size, tiles) - tiles) / UNIT * UNIT;
  l->palette = palette - X2LEVEL_BASE;
  return l->map_rows && l->tiles_size > UNIT ? X2_OK : X2_ERR_FORMAT;
}

static int read_be(const x2_source_t *src, uint32_t size, uint32_t addr,
                   uint8_t *buf, uint32_t len) {
  if (addr < X2LEVEL_BASE || addr + len > X2LEVEL_BASE + size) {
    return X2_ERR_FORMAT;
  }
  return src->read(src->ctx, addr - X2LEVEL_BASE, buf, len) == 0
             ? X2_OK
             : X2_ERR_READ;
}

/* The lists level_sprites gathers: on the heap, as the converter runs
 * deep under the card's reads on core 0's 4 KB stack. */
typedef struct {
  uint32_t anims[KINDS * MAX_PARTS + EXTRA_ANIMS];
  uint32_t singles[1 + EXTRA_TABLES * 16 + 5 + EXTRA_SINGLES + EXTRA_HELD];
} gathered_t;

static int gather(const x2_source_t *src, uint32_t size, uint32_t level,
                  const uint8_t *head, gathered_t *g, uint32_t *addrs, int *count);

/* The enemies' sprites, sorted, into `addrs` (x2sprite_collect). */
static int level_sprites(const x2_source_t *src, uint32_t size, uint32_t level,
                         const uint8_t *head, uint32_t *addrs, int *count) {
  gathered_t *g = malloc(sizeof(gathered_t));
  if (!g) return X2_ERR_RANGE;
  int e = gather(src, size, level, head, g, addrs, count);
  free(g);
  return e;
}

static int gather(const x2_source_t *src, uint32_t size, uint32_t level,
                  const uint8_t *head, gathered_t *g, uint32_t *addrs, int *count) {
  uint32_t *anims = g->anims, *singles = g->singles;
  int n_anims = 0, n_singles = 0, e;
  uint8_t b[8];
  uint32_t handler = X2LEVEL_BASE + 0x12u + (uint32_t)(int16_t)be16(head + 0x12);
  for (int k = 1; k <= KINDS; k++) {
    if (read_be(src, size, handler + 10u + 4u * (uint32_t)k, b, 4) != X2_OK) {
      break; /* no wave handler: no enemy types */
    }
    uint32_t stub = be32(b);
    if (read_be(src, size, stub, b, 6) != X2_OK) break;
    if (be16(b) == MOVE_W_PUSH) { /* the big ones' hit points set around it */
      stub += AROUND_SIZE;
      if (read_be(src, size, stub, b, 6) != X2_OK) break;
    }
    if (be16(b) != LEA_A2) break;
    uint32_t part = be32(b + 2);
    for (int n = 0; n < MAX_PARTS; n++, part += TYPE_PART) {
      if ((e = read_be(src, size, part + 16u, b, 4)) != X2_OK) return e;
      anims[n_anims++] = be32(b);
      if ((e = read_be(src, size, part + TYPE_PART, b, 2)) != X2_OK) return e;
      if (be16(b) == 0) break;
    }
  }
  if (read_be(src, size, be32(head + 0x28), b, 8) == X2_OK) {
    singles[n_singles++] = be32(head + 0x28);
  }
  if (level < 6) {
    const extras_t *x = &extras[level];
    for (int i = 0; i < EXTRA_ANIMS && x->anims[i]; i++) anims[n_anims++] = x->anims[i];
    for (int t = 0; t < EXTRA_TABLES && x->tables[t][0]; t++) {
      for (uint32_t i = 0; i < x->tables[t][1]; i++) {
        if (read_be(src, size, x->tables[t][0] + 4u * i, b, 4) == X2_OK) {
          singles[n_singles++] = be32(b);
        }
      }
    }
    for (int i = 0; i < EXTRA_SINGLES && x->singles[i]; i++) singles[n_singles++] = x->singles[i];
    for (int i = 0; i < EXTRA_HELD && x->held[i]; i++) singles[n_singles++] = x->held[i];
    for (uint32_t i = 0; i < x->shots; i++) {
      if ((e = read_be(src, size, x->shot_table + 4u * i, b, 4)) != X2_OK) return e;
      if (read_be(src, size, be32(b), b, 4) != X2_OK) continue; /* the game's */
      singles[n_singles++] = be32(b);
    }
  }
  return x2sprite_collect(src, X2LEVEL_BASE, anims, n_anims, singles,
                          n_singles, addrs, X2LEVEL_MAX_SPRITES, count);
}

uint32_t x2level_image_size(const x2_source_t *src, uint32_t size,
                            uint32_t level, uint8_t *work) {
  uint8_t head[HEAD_SIZE];
  layout_t l;
  int count = 0;
  uint32_t *addrs = (uint32_t *)(work + X2LEVEL_BACKDROP_BYTES);
  if (size < HEAD_SIZE || src->read(src->ctx, 0, head, HEAD_SIZE) != 0 ||
      layout(head, size, &l) != X2_OK ||
      level_sprites(src, size, level, head, addrs, &count) != X2_OK) {
    return 0;
  }
  uint32_t end = X2LEVEL_HEADER_SIZE + l.map_rows * X2LEVEL_COLS * 2 +
                 X2LEVEL_BACKDROP_SIZE + 2 * l.tiles_size +
                 ((size - gap_size(level, &l) + 3u) & ~3u) +
                 (uint32_t)count * sizeof(x2_sprite_t);
  for (int i = 0; i < count; i++) {
    x2_sprite_t s;
    if (x2sprite_shape(src, X2LEVEL_BASE, addrs[i], &s) != X2_OK) return 0;
    end += (uint32_t)s.width * s.height;
  }
  return end;
}

/* A masked record never sets a pixel of planes 0-3 where its mask keeps
 * the backdrop. */
static int looks_masked(const uint8_t *rec) {
  for (int line = 0; line < X2LEVEL_CELL; line++) {
    const uint8_t *p = rec + line * 12;
    uint16_t m0 = be16(p), m1 = be16(p + 2), d0 = be16(p + 4);
    uint16_t d1 = be16(p + 6), p2 = be16(p + 8), p3 = be16(p + 10);
    if ((m0 & d0) || (m1 & d1) || (m0 & p2) || (m0 & p3)) return 0;
  }
  return 1;
}

static void opaque_tile(const uint8_t *rec, uint8_t *out) {
  for (int line = 0; line < X2LEVEL_CELL; line++) {
    const uint8_t *p = rec + line * 8;
    uint16_t p0 = be16(p), p1 = be16(p + 2), p2 = be16(p + 4),
             p3 = be16(p + 6);
    for (int x = 0; x < X2LEVEL_CELL; x++) {
      int b = 15 - x;
      *out++ = (uint8_t)(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) |
                         (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3));
    }
  }
}

static void masked_tile(const uint8_t *rec, uint8_t *out) {
  for (int line = 0; line < X2LEVEL_CELL; line++) {
    const uint8_t *p = rec + line * 12;
    uint16_t m0 = be16(p), m1 = be16(p + 2), d0 = be16(p + 4);
    uint16_t d1 = be16(p + 6), p2 = be16(p + 8), p3 = be16(p + 10);
    for (int x = 0; x < X2LEVEL_CELL; x++) {
      int b = 15 - x;
      uint8_t colour = (uint8_t)(((d0 >> b) & 1) | (((d1 >> b) & 1) << 1) |
                                 (((p2 >> b) & 1) << 2) |
                                 (((p3 >> b) & 1) << 3));
      uint8_t keep = (uint8_t)((((m0 >> b) & 1) | (((m1 >> b) & 1) << 1))
                               << X2LEVEL_KEEP_SHIFT);
      *out++ = colour | keep;
    }
  }
}

static int put(const x2_sink_t *out, uint32_t *at, const void *buf,
               uint32_t len) {
  if (out->write(out->ctx, *at, buf, len) != 0) return X2_ERR_WRITE;
  *at += len;
  return X2_OK;
}

static const uint8_t zeros[128];

static int put_zeros(const x2_sink_t *out, uint32_t *at, uint32_t len) {
  while (len) {
    uint32_t n = len < sizeof(zeros) ? len : sizeof(zeros);
    if (put(out, at, zeros, n) != X2_OK) return X2_ERR_WRITE;
    len -= n;
  }
  return X2_OK;
}

int x2level_convert(const x2_source_t *src, uint32_t size, uint32_t level,
                    uint32_t source_crc, const x2_sink_t *out, uint8_t *work,
                    x2_progress_fn progress, void *progress_ctx) {
  uint8_t head[HEAD_SIZE];
  layout_t l;
  if (size < HEAD_SIZE || src->read(src->ctx, 0, head, HEAD_SIZE) != 0) {
    return X2_ERR_READ;
  }
  if (layout(head, size, &l) != X2_OK) return X2_ERR_FORMAT;

  uint32_t *addrs = (uint32_t *)(work + X2LEVEL_BACKDROP_BYTES);
  int count = 0;
  if (level_sprites(src, size, level, head, addrs, &count) != X2_OK) {
    return X2_ERR_FORMAT;
  }
  x2level_header_t h;
  memset(&h, 0xFF, sizeof(h)); /* unwritten flash; the magic comes last */
  h.version = X2LEVEL_VERSION;
  h.level = level;
  h.source_crc = source_crc;
  h.size = x2level_image_size(src, size, level, work);
  if (h.size == 0) return X2_ERR_FORMAT;
  h.map_offset = X2LEVEL_HEADER_SIZE;
  h.map_rows = l.map_rows;
  h.backdrop_offset = h.map_offset + l.map_rows * X2LEVEL_COLS * 2;
  h.tiles_offset = h.backdrop_offset + X2LEVEL_BACKDROP_SIZE;
  h.tiles_size = 2 * l.tiles_size;
  h.raw_offset = h.tiles_offset + h.tiles_size;
  h.raw_size = size;
  h.raw_gap = l.tiles;
  h.raw_gap_size = gap_size(level, &l);
  uint32_t kept = size - h.raw_gap_size;
  h.sprites_offset = h.raw_offset + ((kept + 3u) & ~3u);
  h.sprite_count = (uint32_t)count;
  h.opaque_tiles = 0;
  h.masked_tiles = 0;
  uint8_t pal[32];
  if (src->read(src->ctx, l.palette, pal, sizeof(pal)) != 0) {
    return X2_ERR_READ;
  }
  for (int i = 0; i < 16; i++) h.palette[i] = be16(pal + 2 * i);

  /* Count the records first: the header goes out before them. */
  uint32_t units = l.tiles_size / UNIT;
  uint8_t rec[MASKED_SIZE];
  for (uint32_t pos = 1; pos + OPAQUE_SIZE / UNIT <= units;) {
    int masked = 0;
    if (pos + MASKED_SIZE / UNIT <= units) {
      if (src->read(src->ctx, l.tiles + pos * UNIT, rec, MASKED_SIZE) != 0) {
        return X2_ERR_READ;
      }
      masked = looks_masked(rec);
    }
    if (masked) {
      h.masked_tiles++;
      pos += MASKED_SIZE / UNIT;
    } else {
      h.opaque_tiles++;
      pos += OPAQUE_SIZE / UNIT;
    }
  }

  uint32_t at = 0;
  if (put(out, &at, &h, sizeof(h)) != X2_OK) return X2_ERR_WRITE;

  /* The map, a row at a time, in native byte order. */
  for (uint32_t r = 0; r < l.map_rows; r++) {
    uint8_t raw[X2LEVEL_COLS * 2];
    uint16_t row[X2LEVEL_COLS];
    if (src->read(src->ctx, l.map + r * sizeof(raw), raw, sizeof(raw)) != 0) {
      return X2_ERR_READ;
    }
    for (int c = 0; c < X2LEVEL_COLS; c++) row[c] = be16(raw + 2 * c);
    if (put(out, &at, row, sizeof(row)) != X2_OK) return X2_ERR_WRITE;
  }

  /* The backdrop, from columns of planes 0-1 to lines of packed pixels. */
  if (src->read(src->ctx, l.backdrop, work, X2LEVEL_BACKDROP_BYTES) != 0) {
    return X2_ERR_READ;
  }
  for (uint32_t line = 0; line < X2LEVEL_LINES; line++) {
    uint8_t px[X2LEVEL_BACKDROP_LINE] = {0};
    for (int c = 0; c < X2LEVEL_COLS; c++) {
      const uint8_t *p = work + c * (X2LEVEL_LINES * 4) + line * 4;
      uint16_t p0 = be16(p), p1 = be16(p + 2);
      for (int x = 0; x < X2LEVEL_CELL; x++) {
        int b = 15 - x, i = c * X2LEVEL_CELL + x;
        px[i / 4] |= (uint8_t)((((p0 >> b) & 1) | (((p1 >> b) & 1) << 1))
                               << (2 * (i % 4)));
      }
    }
    if (put(out, &at, px, sizeof(px)) != X2_OK) return X2_ERR_WRITE;
  }

  /* The tiles, each record at twice its offset: unit 0 holds none. */
  if (put_zeros(out, &at, 2 * UNIT) != X2_OK) return X2_ERR_WRITE;
  uint32_t pos = 1, done = 0, total = h.opaque_tiles + h.masked_tiles;
  while (pos + OPAQUE_SIZE / UNIT <= units) {
    uint8_t tile[X2LEVEL_TILE_SIZE];
    uint32_t len = pos + MASKED_SIZE / UNIT <= units ? MASKED_SIZE
                                                     : OPAQUE_SIZE;
    if (src->read(src->ctx, l.tiles + pos * UNIT, rec, len) != 0) {
      return X2_ERR_READ;
    }
    int masked = len == MASKED_SIZE && looks_masked(rec);
    if (masked) {
      masked_tile(rec, tile);
    } else {
      opaque_tile(rec, tile);
    }
    if (put(out, &at, tile, sizeof(tile)) != X2_OK) return X2_ERR_WRITE;
    /* A masked record spans 12 units: 384 bytes here, 256 used. */
    if (masked && put_zeros(out, &at, 2 * (MASKED_SIZE - OPAQUE_SIZE)) !=
                      X2_OK) {
      return X2_ERR_WRITE;
    }
    pos += (masked ? MASKED_SIZE : OPAQUE_SIZE) / UNIT;
    if (progress) progress(progress_ctx, ++done, total);
  }
  if (put_zeros(out, &at, (units - pos) * 2 * UNIT) != X2_OK) {
    return X2_ERR_WRITE;
  }

  /* The level as it is but its planar tiles, then the sprites' table and
   * pixels. */
  for (uint32_t done = 0; done < size;) {
    if (done == h.raw_gap) {
      done += h.raw_gap_size;
      continue;
    }
    uint8_t raw[CHUNK];
    uint32_t end = done < h.raw_gap ? h.raw_gap : size;
    uint32_t n = end - done < CHUNK ? end - done : CHUNK;
    if (src->read(src->ctx, done, raw, n) != 0) return X2_ERR_READ;
    if (put(out, &at, raw, n) != X2_OK) return X2_ERR_WRITE;
    done += n;
  }
  if (put_zeros(out, &at, ((kept + 3u) & ~3u) - kept) != X2_OK) {
    return X2_ERR_WRITE;
  }
  uint32_t pixels_at = h.sprites_offset + (uint32_t)count * sizeof(x2_sprite_t);
  for (int i = 0; i < count; i++) {
    x2_sprite_t sp;
    if (x2sprite_shape(src, X2LEVEL_BASE, addrs[i], &sp) != X2_OK) {
      return X2_ERR_FORMAT;
    }
    sp.offset = pixels_at;
    pixels_at += (uint32_t)sp.width * sp.height;
    if (put(out, &at, &sp, sizeof(sp)) != X2_OK) return X2_ERR_WRITE;
  }
  for (int i = 0; i < count; i++) {
    x2_sprite_t sp;
    int e = x2sprite_shape(src, X2LEVEL_BASE, addrs[i], &sp);
    if (e == X2_OK) e = x2sprite_pixels(src, X2LEVEL_BASE, &sp, out, &at);
    if (e != X2_OK) return e;
  }
  if (at != h.size) return X2_ERR_FORMAT;

  uint32_t magic = X2LEVEL_MAGIC;
  return out->write(out->ctx, 0, &magic, sizeof(magic)) == 0 ? X2_OK
                                                              : X2_ERR_WRITE;
}

const uint8_t *x2level_raw(const x2level_header_t *h, uint32_t addr,
                           uint32_t len) {
  if (addr < X2LEVEL_BASE || addr + len > X2LEVEL_BASE + h->raw_size) {
    return NULL;
  }
  uint32_t off = addr - X2LEVEL_BASE, gap_end = h->raw_gap + h->raw_gap_size;
  if (off + len > h->raw_gap && off < gap_end) return NULL;
  if (off >= gap_end) off -= h->raw_gap_size;
  return (const uint8_t *)h + h->raw_offset + off;
}

const x2_sprite_t *x2level_sprites(const x2level_header_t *h) {
  return (const x2_sprite_t *)((const uint8_t *)h + h->sprites_offset);
}
