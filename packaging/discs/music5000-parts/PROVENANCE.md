# Music 5000 discs: provenance

The two disc images here are the originals, committed byte for byte as
downloaded. `../build-music5000-discs.sh` builds the bundled masters from them
(`discs/bundled/hybrid-music-5000-system-4.tar.xz` and
`discs/bundled/amplinex-001-002.tar.xz`), which ship inside the
`model-b-plus-128k-music-5000` preset.

They are redistributed here by the decision of Beebium's owner, Robert
Smallshire (2026-10-26).

## The source

Both come from Colin Fraser's Hybrid Music System page,
http://www.colinfraser.com/m5000/m5000.htm, fetched 2026-10-26. That page
says of its disc images:

> Please note these disk images are provided for educational use only.
> If you are the original copyright owner of any of the files on these disks,
> and you do not wish them to be distributed, let me know and I'll take the
> files down.

The software on them is (c) 1986 Hybrid Technology Ltd (AMPLE, Studio 5000)
and the Amplinex contributors (Amplinex, published by Amplinex from 1987).

## M5000-4.ssd: the Music 5000 System Disk

| | |
|-|-|
| Download | http://www.colinfraser.com/m5000/disks/M5000-4.zip |
| Zip SHA-256 | `5fd745eaf0bdf273957eca22477797f4a5a8ded0fc342fdd4ab1ff0a2c21bf5e` |
| Image | `M5000-4.ssd`, the zip's only member (dated 2007-01-07), 106496 bytes |
| Image SHA-256 | `d60ccb5a211d73c12c60617a3ac0aabf178f4d193e417a191c96af435959979d` |
| Format | Acorn DFS, single-sided 80 track, stored truncated after its last used sector; no title; 28 files; boot option 0 |

Colin Fraser's description: "Music 5000 System Disk. Includes M2000 and
M4000 modules, and patched ROM image. If you SHIFT+BREAK this disk on a
Master, it checks for the AMPLE ROM in sideways slot 4, and if not found,
loads both the AMPLE ROM image, and AMPLE Nucleus Help File ROM. A further
SHIFT+BREAK then loads the AMPLE Studio system. If you use real ROMs, you may
want to replace the !BOOT file with 'runner' which is the normal AMPLE !BOOT
script. If you don't have a Music 4000 or 2000, you can edit 'runner' so
modules M2 and M4 don't INSTALL, saving memory."

Its catalogue:

| Directory | Files |
|-----------|-------|
| `$` | `!BOOT` (the sideways RAM loader), `runner` (AMPLE Studio's start-up script), `ANHF` (the AMPLE Nucleus Help File ROM image), pieces `pavane`, `concert`, `ppach`, `rmix1`, `rmix2`, `kins1`, `kins2`, and `jukebox` |
| `R` | `AMPLE` (the AMPLE Nucleus ROM image) |
| `M` | Studio 5000 modules: `STAFF`, `REC`, `PAD`, `MIX`, `MENU`, `M5`, `M4`, `M2`, `KFX`, `INT`, `INS1`, `FX1A`, `EW`, `ATOS` |
| `C` | `PREPARE`, `CLOSE` |

### What the build changes

`hybrid-music-5000-system-4.ssd` is `M5000-4.ssd` with two changes, made with
oaknut-disc 13.5.0 (pinned in the script) so they are reproducible from the
original:

1. `$.!BOOT` is replaced by a copy of `$.runner` without its `"M4" INSTALL`
   line. This follows Colin Fraser's own advice above. With the ROMs fitted
   the loader is not needed. The M4 module serves the Music 4000 keyboard: it
   polls the user port and hangs without the keyboard, which is what Chris
   Jordan described on Stardot. Beebium fits no Music 4000 (a future
   user-port extension for it could change that). The M2 (Music 2000)
   module loads harmlessly and is kept. The new `!BOOT` has `runner`'s load
   and exec addresses (`&00000000`, `&0003FFFF`) and is 196 bytes.

   ```
   oaknut-disc cat  <image>:$.runner > runner
   (runner without the line "M4" INSTALL<CR>) > boot
   oaknut-disc rm   <image>:$.!BOOT
   oaknut-disc put  <image>:$.!BOOT boot --load 0x00000000 --exec 0x0003FFFF --meta-format none
   ```

2. The boot option is set to 3 (`*EXEC`), so Shift-Break, or a power-on
   with auto-boot, runs it. The original is 0, so Shift-Break does nothing.

   ```
   oaknut-disc opt <image> EXEC
   ```

Every other file is byte-identical to the original. The image differs from the
original only in catalogue sectors 0 and 1 and the new `!BOOT`'s data sector,
and in two effects of oaknut writing the image:

- the empty disc title is stored as spaces rather than NULs;
- the image is written at its full 80-track size (204800 bytes), padded with
  zeros.

### The ROM images

Copied from the original disc with `oaknut-disc get <image>:<file> <rom>
--meta-format none`:

| Bundled as | From | Size | ROM type byte | Title |
|------------|------|------|---------------|-------|
| `hybrid-ample_1_00.rom` | `R.AMPLE` (load and exec `&8000`) | 16384 | `&C2` (service and language) | `AMPLE`; reports "AMPLE Nucleus V1.00 (C) 1986 Hybrid Technology" |
| `hybrid-anhf.rom` | `$.ANHF`, 16383 bytes, plus one `&FF` | 16384 | `&82` (service) | `ANHF` (the AMPLE Nucleus Help File) |

`hybrid-ample_1_00.rom` is the file byte for byte. `$.ANHF` was saved as
`&3FFF` bytes, one short of the 16 KiB EPROM, so the build appends one `&FF`
(an erased EPROM byte) to make `hybrid-anhf.rom` a full 16 KiB image; a
short image draws a warning when it is fitted.

Colin Fraser describes the AMPLE image as a "patched ROM image"; what the
patch is, is not documented there.

The preset fits them as sideways ROM in slots 4 and 5, where the original
loader would have put them on a Master, so the B+ 128K's own sideways RAM
banks stay free for AMPLE.

## anex0102.dsd: Amplinex 001 and 002

| | |
|-|-|
| Download | http://www.colinfraser.com/m5000/disks/anex0102.zip |
| Zip SHA-256 | `48df3e09c8519b24f0af7df486ec5afe092a83b0070f4428cd84c46e7ab7ea6d` |
| Image | `anex0102.dsd`, the zip's only member (dated 1999-08-14), 326144 bytes |
| Image SHA-256 | `3433a63ebd66d367c3cbe2f494b6a415ed1784dbe1749f3c5a891bd5e8790356` |
| Format | Acorn DFS, double-sided 80 track (interleaved), boot option 0 on both sides |

Amplinex (AMPLE Information Exchange) was a disc magazine for Music 5000 users.
It is bundled unchanged as `amplinex-001-002.dsd`. In drive 1, side 0 is drive
1 and side 1 is drive 3.

Side 0, "AMPLINEX 001":

| Directory | Files |
|-----------|-------|
| `$` | `VIOLA4`, `TOBLIN`, `SURVEY`, `RADIO`, `KBGATE`, `INDEX`, `CONCERT`, `CLARIN`, `BYSTAND`, `BELL4`, `AMPLINX`, `ALIEN` |
| `A` | `UTILS2`, `UTILS1`, `SV`, `QUEST1`, `NEWS1`-`NEWS4`, `INTRO1`-`INTRO5`, `HINTS1`, `FEAT1`-`FEAT3`, `ADS1` |

Side 1, "AMPLINEX 002" (as `*CAT 3` shows it):

| Directory | Files |
|-----------|-------|
| `$` | `ACTDEMO`, `AMPLINX`, `ASD`, `ASDDEMO`, `CANTATA`, `DRIP`, `ELITE`, `FYER`, `NITRAWL`, `OBOE`, `PAVANE`, `QWERTY`, `SKY`, `STORM`, `TERRACB`, `WHALE`, `YES` |
| `A` | `FEAT1`-`FEAT3`, `HINTS1`, `INTRO1`-`INTRO5`, `NEWS1`-`NEWS3`, `QUEST1`, `UTILS1` (and possibly more below the first screen of the listing) |

On side 0 the pieces, AMPLE programs saved with exec address `&30100`, are
`ALIEN`, `AMPLINX`, `BYSTAND`, `CONCERT` and `TOBLIN`. Files such as `VIOLA4`,
`BELL4` and `CLARIN` are instrument definitions, and `INDEX` is a
supplementary index to the Music 5000 User Guide.
