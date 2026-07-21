#!/usr/bin/env python3
# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""Capture Thrust's high-score screen and dump its custom font from RAM.

A development tool, not part of the build or the test suite. Boots the game
(discs/games/, not in the repository), reaches the high-score table by ESCAPE
from play, saves the frame, and reads the imploded redefinable-character area
&C00-&CFF -- which turns out NOT to hold the on-screen font, evidence that the
text is blitted from the game's own glyph table rather than printed as MOS
characters. The font itself was transcribed from the captured screen into
fonts/thrust.glyphs; see the header there.
"""

from pathlib import Path
import sys, time
from beebium.client import Beebium
from beebium.client.keyboard import SHIFT_KEY
REPO=Path("/Users/rjs/Code/beebium"); ROMS=REPO/"roms"
DISC=REPO/"discs"/"games"/"Disc024-Thrust.ssd"
OUT=Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)

def sb(bbc):
    bbc.keyboard.matrix_down(*SHIFT_KEY); time.sleep(0.3)
    bbc.keyboard.break_down(); time.sleep(0.3); bbc.keyboard.break_up(); time.sleep(0.5)
    bbc.keyboard.matrix_up(*SHIFT_KEY)
def tap(bbc,r,c,h=0.12):
    bbc.keyboard.matrix_down(r,c); time.sleep(h); bbc.keyboard.matrix_up(r,c)
def frame(bbc):
    return bbc.video.capture_frame(timeout=10.0)

with Beebium.launch(mos_filepath=str(ROMS/"acorn-mos_1_20.rom"),
    basic_filepath=str(ROMS/"bbc-basic_2.rom"),
    server_filepath=str(REPO/"build"/"src"/"server"/"beebium-model-b"),
    extra_args=["--fdc","acorn-1770","--sideways",f"14:rom:{ROMS/'acorn-dfs_2_26.rom'}"]) as bbc:
    bbc.debugger.ensure_running(); time.sleep(1.5)
    bbc.disc.drive(0).insert(str(DISC))
    sb(bbc); time.sleep(8.0)
    for _ in range(3): tap(bbc,6,2); time.sleep(1.2)   # page MODE 7 instructions
    time.sleep(9.0)                                     # MODE 2 loading + MODE 7 keys
    # Tap SPACE until we reach the 288-wide game frame.
    got=False
    for attempt in range(12):
        tap(bbc,6,2); time.sleep(2.0)
        f=frame(bbc)
        print(f"attempt {attempt}: {f.width}x{f.height} f={f.field_order}", flush=True)
        if f.width == 288:
            got=True; break
    if not got:
        print("never reached game", flush=True); sys.exit(1)
    tap(bbc,7,0); time.sleep(4.0)                       # ESCAPE -> high score table
    f=frame(bbc)
    f.save_png(str(OUT/"hiscore.png"))
    print(f"high score: {f.width}x{f.height} f={f.field_order}", flush=True)

    peek = bbc.memory.address.peek
    def art(rows): return ["".join('#' if b&(0x80>>i) else '.' for i in range(8)) for b in rows]
    font = bytes(peek[0x0C00:0x0D00])   # codes 224-255 (imploded default)
    (OUT/"font_c00.bin").write_bytes(font)
    print("=== codes 224-255 at &C00 ===", flush=True)
    for c in range(224,256):
        rows=list(font[(c-224)*8:(c-224)*8+8])
        if any(rows):
            print(f"code {c} (&{c:02X})  letter+160 would be {chr(c-160) if 32<=c-160<127 else '?'!r}", flush=True)
            for r in art(rows): print("   ",r, flush=True)
    # Context: explode level (OSBYTE &14 / workspace) and OSHWM.
    print("=== workspace &0340-&0370 ===", flush=True)
    print(" ".join(f"{b:02X}" for b in peek[0x0340:0x0370]), flush=True)
