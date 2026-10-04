# SAA5050 conformance study: MODE7DEM against real-BBC captures

Status: the five defects are fixed (issue #175); MODE7DEM now matches all
47 captures exactly. Sections 5 and 6 record the state before the fixes,
measured at f7302fd7 and again at 31b71c70 with identical per-cell
results; section 7 gives the state after them. Date: 2026-10-05.

This study runs the MODE7DEM teletext test pages in Beebium and compares
every frame, pixel for pixel, with captures taken from a real BBC Micro. It
then sets out the SAA5050 attribute rules as the real chip behaves, says
which of them `Saa5050.hpp` gets wrong, and proposes a test suite of our own
that pins every rule down without depending on third-party material.

Headline: before #175, 35 of the 47 reference captures matched exactly,
and all 12 that did not were explained by five defects, the worst of which
(colour codes acting Set-At on held graphics) is common in real Prestel
art. After #175 all 47 match.

## 1. Sources

- The stardot thread "BeebEm mistake rendering teletext hold graphics?",
  https://stardot.org.uk/forums/viewtopic.php?t=32725 (both pages, 40 posts,
  March to September 2026).
- The disc `mode7dem.ssd` attached to the thread by avengahM (post p480068).
  The copy in `mode7dem.zip` is byte-identical.
- `mode7dem.zip`, attached by regregex (Greg Cook, post p480436): 47 PNG
  captures, `26032600.png` to `26032646.png`, taken from a real BBC with an
  RGBtoHDMI board.
- b2, branch `wip/tom` of github.com/tom-seddon/b2, which is not yet on its
  master: ec7a10c9 "Fix teletext not holding flashing graphics correctly",
  d198ffce (unscaled teletext output for tests), d5273281 and 0e6b2176 "Add
  mode7dem captures from BBC and corresponding tests". 0e6b2176 says which
  page, flash phase and reveal state each capture shows, and lists the 139
  pixels of capture noise b2 ignores. The local `/Users/rjs/Code/b2` was
  fetched but not modified.
- https://mdfs.net/Info/Comp/Teletext/errors.htm (J.G. Harston): three
  annotated images of common emulator errors on the CEEFAX Engineering Test
  Page, which is page 13 here. The page itself carries no prose.
- The 1976 Teletext specification, cited in the thread:
  https://www.blunham.com/Radar/Teletext/PDFs/TeletextSpec1976.pdf (Set-At
  and Set-After). I did not have the SAA5050 datasheet to hand. Where a rule
  below cites "datasheet" it is as quoted in the thread or in b2's comments,
  not read first-hand.

## 2. The program

`$.!BOOT` is `CH."MODE7"`. `$.MODE7` is BBC BASIC; it was detokenised with
`oaknut-basic`. `C.MODE7` holds 28800 bytes: 30 screens of 960 bytes (24
rows of 40). Both files were extracted with `oaknut-disc`.

```
    1 *FX 4,1
    2 OSCLI"KEY1"+CHR$145
    3 OSCLI"KEY2"+CHR$146
    4 OSCLI"KEY3"+CHR$133
    5 A%=135
   ...
  120 REPEAT
  130   CLS : FOR N%=1 TO 960:VDU (128 OR BGET#file%):NEXT
  140   IF EOF#file% THEN PTR#file%=0
  150   PRINT TAB(0,24)" Press F1 to reveal, F2 to conceal "TAB(0,0);
  160   REPEAT
  170     C% = (&FF00 AND USR&FFF4) DIV 256
  180     X%=POS:Y%=VPOS:PRINT TAB(37,24);RIGHT$("0"+STR$~C%,2);TAB(X%,Y%);
  190     K%=INKEY(1000)
  200     IF K%=145 PROCreveal ELSE IF K%=146 PROCconceal
  210     IF K%=136 VDU8 ELSEIF K%=137 VDU9 ELSEIF K%=138 VDU10 ELSEIF K%=139 VDU11
  220   UNTIL K%=133 OR K%=-1
  230 UNTIL FALSE
  250 DEF PROCreveal: ... every &98 on screen becomes &9B ...
  290 DEF PROCconceal: ... every &9B on screen becomes &98 ...
```

- Each screen is written through VDU with bit 7 set, so screen memory holds
  the file bytes ORed with &80. Row 24 carries the prompt and, at columns 37
  and 38, the hex of the byte under the cursor: the cursor is parked at
  (0,0), and OSBYTE 135 is called via `USR&FFF4` with `A%=135`.
- f3 (KEY3 = CHR$133) pages on. INKEY also times out after 10 s, so the
  program pages through by itself. That is the `K%=-1` term; this copy of the
  program differs there from the listing quoted in b2.
- The BBC has no hardware reveal. f1 rewrites every CONCEAL (&98) on screen
  as ESC (&9B), which the SAA5050 ignores, and f2 does the reverse.
  BBC BASIC for SDL uses &9B on a page to switch on its Full Level One
  emulation (post p480389), which is why some pages carry a stray &9B.
- *FX4,1 makes the cursor keys return codes (they move the cursor, so a
  tester can read any cell's code at row 24).

The pages come from BBCSDL's MODE7DEM (zlib licence, R.T. Russell). They
reproduce Micronet, CEEFAX and Prestel frames.

## 3. What each page exercises

"Page n" is screen n-1 of `C.MODE7`; the thread's page numbers are these.
Codes are the low 7 bits.

| Page | Content | What it exercises |
|---|---|---|
| 1 | Micronet title | Double height (DH) with NEW BG on the code row; header row colours |
| 2 | Character definitions | The non-ASCII glyphs: `_` hash, `#` pound, `` ` `` long dash, `\|` double bar, `^[]` arrows, `~` divide, `\{}` 1/2 1/4 3/4, &7F block |
| 3 | Foreground text colours | Alpha colour codes &01-&07 |
| 4 | Foreground graphics colours | Graphics colours &11-&17; "blast-through" capitals (bit 5 clear) shown as text in graphics mode |
| 5 | NEW / BLACK BACKGROUND | Both Set-At: the code's own cell takes the new background |
| 6 | FLASH and STEADY | Flash is Set-After; flashing foreground over a steady background |
| 7 | SEPARATED / CONTIGUOUS | Mosaic separation; switching mid-row |
| 8 | CONCEAL | Conceal until the next colour code; reveal via f1 |
| 9 | Double/normal height prose | Double-height text on two rows |
| 10 | Double height on a mixed row | Normal-height text on a DH row is blank on the lower row; background of both rows; NORMAL HEIGHT |
| 11 | HOLD and RELEASE | Held mosaics across colour and background codes; held character cleared by alpha codes and by a change of height. Row 10 is the thread's opening case (section 6.6) |
| 12 | FLOF black foreground, Cyrillic | Level-2 codes &80 and &90 (no-ops on the SAA5050); ESC (&9B, ignored); conceal via f2 |
| 13 | CEEFAX Engineering Test Page | Everything at once: hold with separation, conceal, flash/steady, box (no-ops), DH with mid-row NORMAL, SO/SI (no-ops). The mdfs error images are of this page |
| 14 | Prestel art | Hold; flash and steady inside a held run |
| 15 | Prestel "Construction Industry" | Hold, flash, steady, NEW BG, double height |
| 16 | Prestel art | Hold with separated/contiguous switching, BLACK/NEW BG |
| 17, 18 | Prestel art (Norwich Union) | Dense mosaics, NEW BG |
| 19 | Prestel art | Hold and release with BLACK/NEW BG |
| 20 | Prestel "EXPRESS" train | Flash, steady, hold, release, separated, double height with codes in either order; the thread's "page 20" |
| 21 | Prestel art | STEADY in plain mosaics |
| 22 | Prestel art | Flash, hold, NEW BG, NORMAL HEIGHT at end of row |
| 23 | Prestel "DRACULA" | Flash |
| 24 | Prestel art | STEADY, alpha/graphics mix |
| 25 | Prestel "EASTEL Bunny Jigsaw" | Plain mosaics |
| 26, 27 | Prestel art | Flash and steady |
| 28 | Prestel "LANDSCAPE" | Hold, release, BLACK/NEW BG |
| 29 | Prestel "MARILYN MONROE" | Separated mosaics on NEW BG |
| 30 | Prestel "That's all, Folks" | Double height, flash, hold; the thread's "page 30" (the train smoke) |

## 4. Method

### 4.1 The oracle

The 47 captures and what each one shows come from b2's table in 0e6b2176.
For each capture, b2 records the screen, whether flashing cells are in their
visible phase, and whether the page was revealed (&98 shown as &9B) or
concealed (&9B shown as &98). Capture 46 is page 11 with avengahM's
workaround: the byte at row 10, column 2 (NEW BG) is replaced by `A`.

| Capture | Page | Flash | Reveal | | Capture | Page | Flash | Reveal |
|---|---|---|---|---|---|---|---|---|
| 0-4 | 1-5 | on | no | | 25-28 | 16-19 | on | no |
| 5, 6 | 6 | on, off | no | | 29, 30 | 20 | on, off | no |
| 7 | 7 | on | no | | 31 | 21 | on | no |
| 8, 9 | 8 | on, off | no | | 32, 33 | 22 | on, off | no |
| 10, 11 | 8 | on, off | yes | | 34, 35 | 23 | on, off | no |
| 12-14 | 9-11 | on | no | | 36, 37 | 24, 25 | on | no |
| 15, 16 | 12 | on | yes, no | | 38, 39 | 26 | on, off | no |
| 17, 18 | 13 | on, off | no | | 40, 41 | 27 | on, off | no |
| 19, 20 | 13 | on, off | yes | | 42, 43 | 28, 29 | on | no |
| 21, 22 | 14 | on, off | no | | 44, 45 | 30 | on, off | no |
| 23, 24 | 15 | on, off | no | | 46 | 11 (`A` at r10 c2) | on | no |

### 4.2 Driving Beebium

- **Server.** `beebium-model-b`, built from f7302fd7 in a private worktree,
  so the concurrent `Saa5050.hpp` work in the main checkout was not in it.
  Launched through the Python client with MOS 1.20 and BASIC 2.
- **Screen bytes.** After the BASIC prompt, `VDU23,1,0;0;0;0;` turns the
  cursor off. CRTC R12/R13 are checked to be &28/&00, i.e. the screen starts
  at &7C00. For each capture the emulator is stopped and 1000 bytes are
  written to &7C00, the way b2 does it: the page bytes ORed with &80, with
  &98 and &9B swapped as the table says, then row 24 as the program prints
  it.
- **Equivalence with the real program.** As a check, the real program was
  booted from a copy of the disc (`model-b-disc` preset) and left to page
  through on its INKEY timeout. At each of the 30 pages, and again at page 1
  after the wrap, screen memory equalled the bytes poked above, all 1000 of
  them. f1 and f2 on page 8 produced the revealed and concealed bytes.
  Rendering depends only on screen memory, the flash phase and the cursor, so
  poking is equivalent to running the program.
- **Frames.** Each capture's run skips two frames after the write. It then
  collects every frame for 3 emulated seconds (76 frames, at least two
  64-field flash cycles): `capture_frame(after_cycle=...)`, waiting on
  emulated cycles, not wall-clock time.
- **Flash phase.** A page's frames fall into one distinct image, or two for
  a flashing page. No frame mixed phases between its fields: both 16 and 64
  are even. The flash-visible phase covers 48 of 64 fields, so the majority
  image is "flash on" and the minority image "flash off". The class counts
  (52-60 against 16-24) are consistent with that.

### 4.3 Geometry and alignment

- **The capture.** 768x576, pure 8-colour RGB, both fields woven. One
  SAA5050 dot is one pixel and one TV line is one pixel, so a cell is 12x20
  and the 40x25 page is 480x500.
- **Our frame.** 640x500: 16 px per cell, both fields woven. Each 6-dot half
  cell becomes 8 px. Dots 0, 2, 3 and 5 appear pure. Dots 1 and 4 appear only
  in the gamma blends `blend(d0,d1)`, `blend(d2,d1)`, `blend(d3,d4)` and
  `blend(d5,d4)`. With pure palette colours each blended channel is one of
  four values: 0 (off,off), 153 (on,off), 204 (off,on) or 255 (on,on). The
  12-dot row therefore comes back exactly, and the redundant samples are
  checked for consistency (no inconsistency was found on any frame).
- **Alignment.** Found by exhaustive search on every capture, maximising
  exact-pixel agreement over offsets x 140..172 and y 28..48. Every capture
  aligned at x=156, y=38, which puts our cell (0,0) at capture (156,38).
  Agreement at the best offset was above 99.99% on capture 0. No scaling,
  resampling or colour tolerance is involved.

### 4.4 Tolerance

Exact RGB equality per pixel, excluding only b2's 139 pixels of capture
noise. These are RGBtoHDMI sampling errors on strokes of N, K and m. Greg
Cook confirms they are not in the analogue signal (p480695). They include
the top-left notch of the double-height "m"s, which his BBC B lacks and Tom
Seddon's B+ 128 shows (p480685).

No capture shows a cursor (section 9.1); ours is turned off. Nothing else
is excluded. The comparison is pixel-exact; it is not done by eye.

## 5. Results

35 of 47 captures match exactly; 12 differ. Pixel counts exclude the cells
listed in section 4.4. A cell is 240 pixels.

| Capture | Page | Phase | Differing px | Cells | Cause (section 6) |
|---|---|---|---|---|---|
| 0, 2-13 | 1, 3-10 | all | 0 | 0 | match |
| 1 | 2 | on | 44 | 1 | 6.5 glyph &7D |
| 14 | 11 | on | 2592 | 12 | 6.1 |
| 15, 16 | 12 | on | 0 | 0 | match |
| 17, 18 | 13 | on, off | 1932 | 20 | 6.1, 6.4 (r5 c32, c36), 6.5 (r21 c37) |
| 19, 20 | 13 revealed | on, off | 1932 | 20 | 6.1, 6.5 |
| 21, 22 | 14 | on, off | 0 | 0 | match |
| 23, 24 | 15 | on, off | 4416, 4176 | 19, 18 | 6.1 |
| 25 | 16 | on | 1056 | 6 | 6.1 |
| 26, 27 | 17, 18 | on | 0 | 0 | match |
| 28 | 19 | on | 2232 | 10 | 6.1 |
| 29 | 20 | on | 3120 | 13 | 6.1 |
| 30 | 20 | off | 2916 | 13 | 6.1, 6.2 (r3 c27, r4 c25, r5 c26) |
| 31-41 | 21-27 | all | 0 | 0 | match |
| 42 | 28 | on | 852 | 4 | 6.1 |
| 43 | 29 | on | 0 | 0 | match |
| 44 | 30 | on | 0 | 0 | match |
| 45 | 30 | off | 240 | 1 | 6.3 (r10 c11) |
| 46 | 11, `A` at r10 c2 | on | 4032 | 18 | 6.1 |

By page: 21 of 30 pages match in every capture. Pages 2, 11, 13, 15, 16, 19,
20, 28 and 30 do not. Every differing cell was examined; none is unexplained.

None of the mismatches looks like the double-height row-counting bug being
fixed concurrently: every double-height row on every page, including page
10's mixed row and page 20's codes in either order, matches.

Against the thread's known trouble pages:

- **Page 14:** matches.
- **Page 20:** fails, through the colour rule (6.1) and the flashing-hold rule (6.2).
- **Page 30:** fails in its off phase (6.3), in exactly the train-smoke cell the thread discusses.

## 6. Confirmed defects, worst first

Each defect gives a minimal byte sequence: a row of screen memory starting at
column 0. The expected output follows the real captures and b2's corrected
code.

### 6.1 Foreground colour codes act Set-At on held graphics; they are Set-After

`97 FF 9E 93`: graphics white, block, HOLD, graphics yellow. The real chip
shows the held block in cell 3 in the old colour, white. Beebium shows it
in yellow. The same holds for alpha colour codes &81-&87, e.g. page 11 row
15, cell 20 (alpha magenta under hold) shows the held block in green.

Cause: `Saa5050::byte()` runs `process_control_code()`, which updates `m_fg`,
before it writes `output->fg`. b2 latches the foreground into the output
before processing the code; the background is still taken after, which keeps
NEW/BLACK BACKGROUND Set-At.

Seen in captures 14, 17-20, 23-25, 28-30, 42 and 46: 182 of the 195 differing
cells and 95% of the differing pixels. It is the worst defect because held mosaics with
colour changes are the standard Prestel/CEEFAX technique for gap-free
colour transitions. Most of the art pages use it.

### 6.2 The held-graphics memory captures the flash-blanked glyph

`97 88 FF 9E 89 FF` (Tom Seddon's reduction, p480574). Real output: 2 spaces,
2 flashing blocks, 2 steady blocks. In the flash-off phase Beebium shows cell
4 (STEADY, holding) blank instead of a steady block.

Cause: `byte()` stores `m_last_graphics_data = data` after `data` has been
zeroed for `!m_text_visible`. The memory must hold the glyph's bitmap,
independent of flash; b2 ec7a10c9 fixed exactly this.

Seen in capture 30 (page 20, flash off), 3 cells: r3 c27, r4 c25 and r5 c26,
the white wisp above the E of EXPRESS described in p480577.

### 6.3 Held graphics in control-code cells ignore flash

`97 FF 9E 88 9C`, flash-off phase. The real chip shows cell 3 (FLASH,
Set-After) as a steady held block and cell 4 (BLACK BG) blank: the held
block is flashing by then. Beebium shows a block in cell 4.

Cause: in the control-code branch, `data` (the held glyph) is never masked by
`m_text_visible`. b2 applies the flash mask after the code is processed, with
the mask taken from the state before processing, except that STEADY forces it
visible. That makes FLASH Set-After and STEADY Set-At for held graphics too.

Seen in capture 45 (page 30, flash off), r10 c11. 6.2 hides this defect in
most places: a held glyph stored during the off phase is blank anyway. Fixing
6.2 alone would therefore expose more of 6.3. The two should be fixed
together.

### 6.4 CONCEAL is Set-At, but Beebium shows the held graphic in its cell

`97 FF 9E 98`: the real chip shows cell 3 blank. Beebium shows the held block
there, because `m_conceal` is set only after the cell's `data` is computed.
The cancelling colour code is Set-After (its cell is still concealed), and
Beebium gets that right. The held memory survives conceal on the real chip
(page 13 r5 c34 shows the held block again after conceal ends), and Beebium
matches that too.

Seen in captures 17 and 18 (page 13 concealed): r5 c32 and r5 c36. In the
revealed captures (19, 20) those cells hold ESC and match.

### 6.5 Glyph &7D (3/4) has its "3" one dot too far right

Font rows 1 to 5 (of 0-9) of 0x7D in `TeletextFont.hpp` are shifted right by
one dot:

| Row | Real chip and b2 | Beebium |
|---|---|---|
| 1 | `.XX...` | `..XX..` |
| 2 | `...X..` | `....X.` |
| 3 | `.XX...` | `..XX..` |
| 4 | `...X..` | `....X.` |
| 5 | `.XX..X` | `..XX.X` |

Rows 6-9 agree. Seen in capture 1 (page 2, r17 c3) and captures 17-20
(page 13, r21 c37): 44 pixels each.

### 6.6 Not a defect: SAA5050 hold memory

This is the thread's opening question. Unlike a teletext TV or the
specification, the SAA5050 forgets the held mosaic at any control code
displayed while hold is off. On page 11 row 10, `97 FF 9D 9E` therefore
shows a blank at the HOLD. Replacing the NEW BG with `A`, as in capture 46,
keeps the block, because an alphanumeric character in graphics mode does not
clear the memory. Beebium matches both (row 10 of captures 14 and 46 has no
differing cells).

## 7. The SAA5050 attribute rules

"Set-At": the code's own cell is displayed with the new state. "Set-After":
the change takes effect from the next cell. For most codes the distinction is
visible only under hold, since a control code's cell otherwise shows a
space.

Source key: **C** = the reference captures (cells named); **S** = 1976
specification; **T** = thread post; **b2** = b2 `wip/tom` teletext.cpp,
which passes all 47 captures; **mdfs** = mdfs.net error images.

| # | Rule | Real SAA5050 (BBC) | Source | Saa5050.hpp |
|---|---|---|---|---|
| R1 | Row start state | Alpha white, black background, contiguous, steady, normal height, not concealed, hold off, held memory empty | S, b2, C (every page) | Matches |
| R2 | Alpha colour &81-&87 | Foreground Set-After, also for a held glyph in the code's cell. Selects alpha, cancels conceal (Set-After) and clears the held memory (Set-After) | C p11 r15, p13; T p480826; b2 | Matches since #175 (was Set-At, 6.1) |
| R3 | Graphics colour &91-&97 | Foreground Set-After as R2. Selects graphics, cancels conceal (Set-After) | C p11, p13, p15, p16, p19, p20, p28; b2 | Matches since #175 (was Set-At, 6.1) |
| R4 | FLASH &88 | Set-After, for characters and held glyphs | S; C p6, p30 r10 c10; b2 | Matches since #175 (held glyphs did not flash, 6.3) |
| R5 | STEADY &89 | Set-At, for characters and held glyphs | T p480577, p480780; C p20 r3 c27 | Matches since #175 (held glyphs wrong via 6.2) |
| R6 | Flash timing | 64-field cycle, hidden for fields 0-15 (3:1 visible:hidden), counted at vsync | b2 constants only; **not verifiable from stills** | Same constants; unverified |
| R7 | Flash memory | The held memory stores the glyph bitmap regardless of flash phase | b2 ec7a10c9; T p480574; C p20 off | Matches since #175 (6.2) |
| R8 | CONCEAL &98 | Set-At; held glyphs are blanked in its cell too. Lasts until the next colour code, whose cell is still concealed | C p8, p13 r5 c32..c37; b2 | Matches since #175 (was not Set-At for a held glyph, 6.4) |
| R9 | Hold memory under conceal | The memory survives conceal and shows again after it | C p13 r5 c34 | Matches here (memory is not stored while concealed; untested where a mosaic is drawn under conceal and then held) |
| R10 | Reveal | Not a chip function on the BBC; software rewrites &98 to &9B (ESC, ignored) | Program listing | n/a (ESC is a no-op: matches) |
| R11 | BLACK BG &9C, NEW BG &9D | Set-At (the code's cell takes the new background). NEW BG copies the current foreground | S; C p5 | Matches |
| R12 | HOLD &9E | Set-At: its cell shows the held glyph | S; C p11 | Matches |
| R13 | RELEASE &9F | Set-After: its cell still shows the held glyph | S; C p11 r10 c35 | Matches |
| R14 | What the memory holds | The last mosaic displayed (bit 5 set, graphics mode) as rendered, with its own separation | mdfs errSep; C p13 r5, p16 | Matches |
| R15 | What clears the memory | Any control code displayed while hold is off; an alpha colour code (after its cell); a change of height (in its cell); end of row. An alphanumeric character in graphics mode does not | T p480068, p480389; C p11 r10 and capture 46 | Matches (6.6) |
| R16 | CONTIGUOUS &99 / SEPARATED &9A | Affect following mosaics; a held glyph keeps the separation it was drawn with | S; mdfs errSep; C p7, p13, p16 | Matches |
| R17 | DOUBLE HEIGHT &8D | The code's cell shows a blank if it changes the height (clears the memory), the held glyph otherwise. Text after it is double height | T p480826, p480828 (hoglet capture65); C p11 r19-20 | Matches |
| R18 | NORMAL HEIGHT &8C | As R17 for its own cell. Set-At versus Set-After is not observable apart from the height change (p480821) | S; T p480780-p480822; C p10, p11 | Matches |
| R19 | Double-height rows | A row with DH shows the top halves. The next row is the lower half, read from **its own RAM**, not copied from the top row: its codes and colours apply. Normal-height text in the lower row is blank. The lower row's background follows its own codes | T p480084, p480091, p480232, p492606; C p1, p9, p10, p15, p20 r19-22 | Matches |
| R20 | Level-2 codes | &80, &90, &8A/&8B (box), &8E/&8F (SO/SI), &9B (ESC), &90 (DLE) are no-ops displayed as control-code spaces | C p12, p13; b2 | Matches |
| R21 | Glyphs and rounding | 5x9 font with character rounding (the b2 formula). Real BBCs differ in the double-height `m` notch (B versus B+) | C all pages; T p480685 | Matches since #175 (&7D was wrong, 6.5) |

## 8. Proposed test plan

### 8.1 What exists

Coverage as committed at f7302fd7.

- **`tests/test_saa5050.cpp`**: construction, reset, line management, glyphs
  for space and 'A', emit_pixels.
- **`tests/test_saa5050_hold_graphics.cpp`**: hold reproduces the held glyph
  exactly; hold persists across codes; RELEASE is Set-After. Covers R12,
  R13 and part of R14.
- **`tests/test_mode7.cpp`**, chip level: colour codes set the foreground,
  but only the state *after* the code, so they cannot see Set-At versus
  Set-After. Also separated mode, background codes, the flash state
  machine, start-of-line reset, cursor XOR and the gamma blend table.
- **`tests/test_mode7.cpp`**, machine level: golden-master PPMs for colours,
  sixels, the printable set, double height and hold, compared with a
  per-channel tolerance of 16 and passing with up to 0.1% of the frame
  different. Fixing 6.1 changed 440 px (0.14%) of the hold master, so
  that change would have been caught; but the tolerance had hidden a
  stale hold master (its RELEASE cell still truncated as before the #57
  fix) and drift in the B+ cursor's blink phase. Since #175 the masters
  are compared exactly outside the cursor's cell.
- **`tests/test_teletext_grid.cpp`**: the semantic capture grid (colours,
  conceal, double height, flash flags), not pixels.

None of these exercises R2/R3 under hold, R5, R7, R8 under hold, R15, R17
under hold, R19's split rows or glyph &7D.

### 8.2 New vectors (authored by us)

These are implemented in `tests/test_saa5050_attribute_rules.cpp` (#175),
with further checks that the screen-text grid records what each cell shows.

Each vector is one or two rows of screen bytes. Expected output is stated per
cell as (glyph, foreground, background), in both flash phases where flash is
involved: "blk" = &7F block, "sep" = separated &7F, "-" = blank (background
only).

The vectors drive the chip directly (`byte()` per cell, a raster per glyph
row, `vsync()` to choose the flash phase). Pixel rows are compared via
`emit_pixels` with the 12-dot decode used in this study, which is exact. That
keeps them independent of CRTC timing and of the concurrent row-counting
work.

| Id | Bytes | Expected cells | Rules |
|---|---|---|---|
| V1 | `97 FF 9E 93 FF` | -, blk W, blk W, **blk W**, blk Y | R3, R12 (6.1) |
| V2 | `97 FF 9E 83 41` | -, blk W, blk W, **blk W**, A Y | R2 (6.1) |
| V3 | `97 FF 9E 85 9C` | -, blk W, blk W, blk W, **-** (memory cleared after the alpha code) | R2, R15 |
| V4 | `97 88 FF 9E 89 FF` | on: -, -, blk, blk, blk, blk. off: -, -, -, -, **blk**, blk | R4, R5, R7 (6.2) |
| V5 | `97 FF 9E 88 9C` | on: -, blk, blk, blk, blk. off: -, blk, blk, **blk**, **-** | R4, held flash (6.3) |
| V6 | `97 FF 9E 98 9C 97 9C` | -, blk, blk, **-**, -, - (colour cell still concealed), blk W (memory survived) | R8, R9 (6.4) |
| V7 | `97 FF 9D 9E 9C` | -, blk, - bg W, - bg W, - (memory cleared by NEW BG while not holding) | R11, R15 |
| V8 | `97 FF 41 9E 9C` | -, blk, A W, blk, blk (alphanumeric does not clear) | R15 |
| V9 | `97 FF 9E 8D` / `97 FF 9E 8C` | cell 3 **-** (height change) / cell 3 blk (no change) | R17, R18 |
| V10 | `97 9A FF 9E 99 9C` | -, -, sep, sep, sep, sep (the held glyph keeps its separation) | R14, R16 |
| V11 | `81 9D 41 9C 41` | -, - bg R, A R/R, - bg K, A R/K | R11 |
| V12 | `8D 41` then `82 8D 42` (two rows) | Row 1: A top half white. Row 2: B lower half green (from RAM, not copied) | R19 |
| V13 | `8D 41 8C 43` then `8D 41 8C 43` | Row 1: A top, C normal. Row 2: A lower, C **blank** | R19 |
| V14 | `97 FF 9E 9F 9C` | -, blk, blk, blk, - | R13 (exists; keep) |
| V15 | 64 vsyncs over `88 41` | Hidden for fields 0-15 of the cycle, visible for 16-63 | R6 |
| V16 | `7D` at glyph rows 0-9 | The bitmap in 6.5, left column | R21 (6.5) |
| V17 | `8A 8B 8E 8F 90 9B 80` under hold | Each shows the held glyph; no state change | R20 |
| V18 | Row-start reset after `91 9A 9E 88 9D 98 8D` | Next row starts in R1's state | R1 |

Each vector should also be written twice more:

- As a C++ machine-level test: the bytes poked at &7C00, then the cells
  read back through `TeletextGrid`, and pixels compared exactly at the 12-dot
  level, with no percentage tolerance.
- As a Python integration test through `capture_frame`. This study's harness
  shows that path is exact.

The existing golden-master tolerance should be removed, or replaced by exact
comparison. It can hide a whole cell.

### 8.3 MODE7DEM as an optional oracle

The 47 captures could back an opt-in test: run when an environment variable
names a local directory holding `mode7dem.ssd` and the 47 PNGs, skip
otherwise.

It should do exactly what section 4 did: poke the bytes, collect a flash
cycle's frames, decode to 12 dots, align at (156,38) and compare with the
139 b2 noise pixels excluded. It must never be committed;
see section 10. It is the ultimate whole-chip regression, but the V-vectors
above are what should gate CI.

## 9. Observations outside the SAA5050

### 9.1 Cursor position: retracted

An earlier version of this section reported the hardware cursor one cell
right and two lines lower relative to the text than ours. That was wrong.
No capture shows a cursor at all: what was measured, at capture y=58..59,
x=168..179, is the top of row 1's yellow NEW BACKGROUND band, seen through
a crop two lines off the true alignment. Our cursor-off frames match every
capture exactly, those pixels included. The captures say nothing about the
cursor's position.

### 9.2 Golden-master tolerance

See section 8.1: the tolerance hid stale masters rather than this
defect. The masters are now compared exactly outside the cursor's cell.

## 10. Licence position

| Material | Origin | Position for Beebium |
|---|---|---|
| Page data (`C.MODE7`) | BBCSDL's MODE7DEM, zlib licence (R.T. Russell, b2 LICENCE). The pages reproduce 1980s Micronet, CEEFAX and Prestel frames, whose own copyright is unclear | Do not commit. The zlib terms would allow it, but the frames inside are third-party |
| Driver program (`MODE7`) | avengahM, posted to the forum without a licence | Do not commit |
| Captures | Greg Cook (regregex). He gave b2 permission to include them, saying they are "no more encumbered than their sources" and disclaiming pixel-perfect accuracy (p480695). b2 credits him in its LICENCE (0e6b2176) | That permission was given to b2, not to us. Use only as an optional, local, non-committed oracle (8.3), unless he is asked directly |
| b2's ignore list and grab table | b2, GPL-3.0 | Facts about the captures; used here as data in the study, not copied into our tree |

Our own vectors (8.2) are authored from the rules, not copied from the pages.
They carry no third-party content.

## 11. Reproducing

The scripts lived in a scratch directory and are not committed. They did
this:

1. `oaknut-disc cat` and `oaknut-basic detokenise` to extract and read the
   disc.
2. A Python-client script that boots `beebium-model-b` with MOS 1.20 and
   BASIC 2 and turns the cursor off. For each of the 47 captures it pokes
   the 1000 screen bytes described in 4.2, collects every frame for 3
   emulated seconds and saves each distinct frame.
3. A NumPy script doing the decode, alignment search and comparison of 4.3
   and 4.4, reporting per capture the differing pixel and cell counts and,
   per cell, the colour census of the capture against ours.
4. A Python-client script that boots the real program from a copy of the
   disc and checks, page by page, that screen memory equals the poked bytes
   (4.2).
