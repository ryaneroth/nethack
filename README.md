# NetHack for KIM-1

A NetHack-style roguelike for the KIM-1, written in C for cc65. It is a new,
compact game that follows NetHack's rules and flavour rather than a build of
the upstream source tree (see [Upstream NetHack](#upstream-nethack) for why).

- A 64×21 dungeon of rooms and corridors with doors, lit and dark rooms, and a
  remembered map. Ten levels; the Amulet of Yendor lies on level 10, and you
  win by carrying it back up the stairs from level 1.
- 24 NetHack monsters (newts, jackals, grid bugs that can't move diagonally,
  floating eyes whose gaze paralyses you if you hit them, soldier ants,
  trolls…) with NetHack's to-hit, AC, speed and experience rules, plus the
  ogre king guarding the Amulet.
- A pet kitten that follows you, fights for you, and comes with you down the
  stairs when it is next to you.
- 31 kinds of object. Potions, scrolls and wands start unidentified and are
  shuffled each game (a "ruby potion", a "scroll labeled ZELGO MER"); some
  weapons and armor are cursed and cannot be taken off.
- Hunger and fainting, corpses (some poisonous), hidden traps and searching,
  Elbereth, `#pray`, and a tombstone when you die.

## Memory layout

The target is a KIM-1 with 32 KB of RAM at `$2000-$9FFF` and a 24 KB flash
ROM at `$A000`. One link produces two images that carry the same build stamp:

| Image | Address | Contents |
| --- | --- | --- |
| `nethack.bin` | `$A000` (ROM) | Startup, cc65 runtime, SD/FAT32 driver, the main game loop, display, movement, combat and monsters |
| `nethack-ram.bin` | `$2000` (RAM) | Object and monster tables, level generation, item commands, new-game and end-of-game code |

Game state follows the RAM image, and the cc65 software stack uses
`$9800-$9FFF`. The linker map (`nethack.map`) shows the current sizes; at the
time of writing the ROM image is about 23 KB and the RAM image about 12.5 KB,
leaving roughly 14 KB of RAM free.

At start-up the ROM checks whether the matching RAM image is already in
memory. If not, it loads `NETHACK.BIN` from the SD card. A stale
`NETHACK.BIN` from a different build is rejected, so copy both images after
every build.

## SD card

Format the card FAT32 and copy these files to its root directory:

| File | Source | Purpose |
| --- | --- | --- |
| `NETHACK.BIN` | `nethack-ram.bin` | The RAM half of the program (required unless loaded by paper tape) |
| `NHHELP.TXT` | `data/NHHELP.TXT` | Full help text shown by `?` (optional) |

The game also writes to the card:

- `LEVEL01.DAT` … `LEVEL10.DAT`: each level you leave is saved, so it is still
  there when you come back, as NetHack does with its level files. Files from an
  earlier game are ignored.
- `NHSAVE.DAT`: written by `S` (save and stop). It is restored at the next
  start and then deleted.

Without an SD card the game can still run if the RAM image was loaded by paper
tape, but it cannot save, and levels are regenerated when you revisit them.

The SD interface is the [`sdcard6502`](https://github.com/ryaneroth/sdcard6502)
hardware used by the [Adventure port](https://github.com/ryaneroth/Adventure);
the driver is vendored in `vendor/sdcard6502`.

The game needs `sdcard6502` at or after the fixes merged in its pull
request #3, which fix reading the final cluster of a file, contiguous-cluster detection, and
reuse of deleted directory entries. The game needs the last fix: it deletes
and re-creates level files, and without reuse the root directory eventually
fills up.

## Build

```sh
make          # nethack.bin (ROM) and nethack-ram.bin (RAM)
make ptp      # also nethack.ptp and nethack-ram.ptp (MOS paper tape)
```

Requires cc65; `make ptp` also needs `srec_cat`.

## Running on the KIM-1

1. Flash `nethack.bin` to the ROM at `$A000`.
2. Copy `nethack-ram.bin` to the SD card as `NETHACK.BIN` (and optionally
   `data/NHHELP.TXT`), or load `nethack-ram.ptp` by paper tape.
3. Start it from the monitor with `G A000` and play through the serial
   terminal.

The game asks for your name and whether your terminal supports ANSI screen
clearing. With ANSI on, it clears and redraws the screen each turn; with it
off, each turn's map scrolls up the terminal. `O` toggles this during play.

## Running in the emulator

Create a test card image containing the RAM image and help file, then run
from the workspace root:

```sh
cp nethack/nethack-ram.bin /tmp/NETHACK.BIN
python3 nethack/tests/make_fat32_image.py /tmp/nethack-card.img \
  --add /tmp/NETHACK.BIN --add nethack/data/NHHELP.TXT
python3 -m kim1emu.cli \
  --load-bin nethack/nethack.bin --load-addr 0xA000 --start-addr 0xA000 \
  --map-bin nethack/nethack-ram.bin@0x2000 \
  --sd-image /tmp/nethack-card.img --save-sd-image /tmp/nethack-card.img \
  --sd-hw sdcard6502 --serial --io-bridge high
```

`--map-bin` preloads the RAM image, which skips the slow load from the card;
leave it out to exercise the SD loader. `--save-sd-image` keeps saved games
and level files between runs.

## Commands

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| `h j k l y u b n` | move / attack | `H J K L Y U B N` | run |
| `,` | pick up | `d` | drop |
| `i` | inventory | `:` | look here |
| `w` | wield (`-` for none) | `W` / `T` | wear / take off armor |
| `q` | quaff a potion | `r` | read a scroll |
| `e` | eat | `z` | zap a wand |
| `s` | search for traps | `.` | rest |
| `<` `>` | climb stairs | `E` | engrave (try `Elbereth`) |
| `#pray` | pray | `S` | save and stop |
| `Q` | quit | `?` | help |
| `O` | toggle ANSI clearing | `Ctrl-R` | redraw |

A number before `s` or `.` repeats it (for example `20s`). Running and
repeated commands stop when something interesting happens.

## Upstream NetHack

An earlier attempt built upstream NetHack 5.0 itself with cc65; it has been
removed. Every upstream source file could be made to compile, but the result
cannot fit this machine. Compiling all of it (131 game files, Lua, the TTY
window port and the platform files) with `-O --static-locals` gave:

| Segment | Size |
| --- | --- |
| Code | about 3.0 MB |
| Read-only data | about 373 KB |
| Initialised + zeroed state | about 670 KB |

`decl.c`'s globals alone are about 117 KB. Code could in principle be paged
in from the SD card, but the game state, which changes every turn, is many
times the 64 KB address space, so that work was set aside in favour of this
game.
