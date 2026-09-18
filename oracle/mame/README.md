# MAME oracle harness

A small headless harness for running a BBC Micro under [MAME](https://www.mamedev.org)
and reading back CPU state and memory, as a second differential-testing oracle beside the
jsbeeb one in `../`. Like that one it is a bug-hunting instrument, not part of the build or
CI: picked up when a defect needs a third opinion, put down again afterwards.

It earned its keep on the "Flip!" reports (issues #85/#86): MAME's default `bbcb` runs the
Intel 8271, and the game runs there, while every WD1770 / DFS 2.2x configuration (Beebium,
jsbeeb's 1770 model, and MAME's own `-fdc acorn1770`) fails it identically -- which is what
established the game as 8271-only rather than a Beebium defect.

## MAME and ROMs

- **MAME is an external program** (GPL-3.0), installed separately; it is not vendored in
  this repository, and this harness contains no MAME code -- it only drives MAME through
  its Lua engine. Developed and validated against **MAME v0.289** (Homebrew,
  `brew install mame`). The Lua API names below were checked against that version's
  `src/frontend/mame/luaengine*.cpp`; **they drift between releases**, so re-check on upgrade.
- **ROMs are copyrighted and are NOT distributed with Beebium.** They must live OUTSIDE the
  repository. The scripts default the rompath to
  `$BEEBIUM_MAME_ROMPATH` (or `$XDG_DATA_HOME/beebium/mame-roms`,
  i.e. `~/.local/share/beebium/mame-roms`) and refuse to write inside the repo. A
  `.gitignore` guard also blocks stray ROM/zip files under `oracle/`.
- Build the rompath from images you already have with **`setup_roms.sh`** (below); it never
  downloads anything. Then verify with `mame -rompath <dir> -verifyroms bbcb` (the optional
  Watford-DFS BIOS variants may be reported missing; the default BIOS still boots).

The minimal default `bbcb` set is five files (from `mame -listroms bbcb`): `os12.rom` and
`basic2.rom` (which Beebium's own `roms/acorn-mos_1_20.rom` and `roms/bbc-basic_2.rom`
satisfy -- identical SHA-1), the speech VSM `cm62024.bin`, the SAA5050 charset `saa5050`,
and the 8271 DFS `dnfs120.rom` (DFS 1.20).

## setup_roms.sh

```
./setup_roms.sh [source ...]        # sources: directories to search, or romset .zip files
```

Searches the given sources plus `BEEBIUM_ROM_SOURCES` (colon-separated) plus sensible
defaults (the repo's `roms/`, `~/Code/b-em`, `~/Code/beebem-mac`), matches each required
file by SHA-1, and copies it into the rompath under MAME's layout (`bbcb/`, `saa5050/`,
`bbc_acorn8271/`). It never downloads and never writes inside the repo, and reports what it
found and what is still missing (exit 3 if a required file is absent).

## Files

- `harness.lua` -- a MAME `-autoboot_script`. Parameters come from environment variables
  so the one script serves every run:
  `HK_KEYS` (keystrokes, `\n` = RETURN), `HK_DELAY` (seconds before typing),
  `HK_RUN` (seconds after typing before the dump), `HK_DUMPS` (`addr:len,...` in
  hex:decimal), `HK_OUT` (dump file), `HK_SNAP` (set = also snapshot),
  `HK_CPUTAG` (default `:maincpu`), `HK_SPACE` (default `program`).
- `mame_run.sh` -- a wrapper that sets those, runs MAME headless, and exits. It pre-flights
  MAME and the romset and exits **3** ("oracle unavailable") if either is missing, so a
  caller can tell "not set up" from "ran and disagreed".
- `setup_roms.sh` -- assembles the rompath from images you already have (see above).

## Validated luaengine calls (MAME v0.289)

- `emu.wait(seconds)` -- yields the autoboot coroutine (luaengine.cpp).
- `manager.machine.natkeyboard:post(text)` -- types via the natural keyboard.
- `manager.machine.devices[tag].spaces["program"]:read_u8(addr)` -- side-effect-free read.
- `manager.machine.devices[tag].state[symbol].value` -- CPU registers (PC/A/X/Y/SP/P).
- `manager.machine.video:snapshot()` -- writes a PNG into `-snapshot_directory`.
- `manager.machine:exit()` -- schedules a clean exit.

## Usage

```
./mame_run.sh -m <machine> -d <disc> [-k <keys>] [-D <sec>] [-R <sec>] \
              -a "5000:16,00FD:2" [-o out.txt] [-s] [-- <extra mame args>]
```

- `-m` machine, `-d` floppy image (`:flop1`), `-k` keystrokes (`\n` = RETURN, typed after
  `-D` seconds), `-R` seconds to run before dumping, `-a` comma list of `addr:len`
  (address hex, length decimal), `-o` output file, `-s` also snapshot, `--` pass-through
  to MAME. `ROMPATH` and `OUTDIR` come from the environment.

Boot check (reads "BBC Computer 32K" from the Mode 7 screen):

```
./mame_run.sh -m bbcb -D 2 -R 1 -a "7C28:16"
```

Run a disc, dump the crash-relevant bytes, and snapshot:

```
./mame_run.sh -m bbcb -d flip.ssd \
  -k $'*DISC\nPAGE=&1900\nCHAIN"FLIP!"\n' -D 3 -R 16 -s \
  -a "0355:1,5000:16,5050:16,00FD:2"
```

## BBC machine / FDC options

- `bbcb` -- default is the **Intel 8271** (`bbc_acorn8271`), default BIOS **DNFS 1.20
  (Acorn DFS 1.20)**. Other 8271 BIOSes: DFS 0.90 / 0.98, DNFS 1.00, Watford.
- Add `-- -fdc acorn1770` for the **WD1770** (`bbc_acorn1770`, default BIOS **Acorn DFS
  2.23**) -- the like-for-like with Beebium's WD1770 + DFS 2.26.
- `mame bbcb -listslots` and `-listbios` show the full set (Cumana, Opus, Solidisk,
  Watford controllers; multiple DFS/ADFS BIOSes).

Read a config's DFS version by extracting the ROM (e.g. `dnfs120.rom`) and checking its
title, or from `mame -listxml bbcb` biosset descriptions.
