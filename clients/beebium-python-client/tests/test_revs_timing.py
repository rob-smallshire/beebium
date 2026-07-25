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

"""Diagnostic tests for Revs split-screen timing.

Revs uses chained VIA T1 timer interrupts for palette switching in its
split-screen display. The total timer chain must match exactly one CRTC
frame period. If the CRTC frame length is wrong (e.g., missing interlace
dummy raster), the palette change points drift, producing a rolling blue
band across the display.

Requirements:
    - Beebium server executable (auto-detected or via BEEBIUM_SERVER)
    - MOS 1.20 ROM and BASIC 2 ROM (via BEEBIUM_ROM_DIR)
    - DFS 1770 ROM (auto-detected in ROM directory)
    - Revs disc image at discs/games/Disc015-Revs.ssd
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import read_mode7_screen, screen_contains

REVS_DISC_FILENAME = "Disc015-Revs.ssd"

# Sequence of (wait_text, key_to_press) tuples for navigating from boot
# to gameplay. Text is matched against Mode 7 screen memory.
REVS_BOOT_SEQUENCE = [
    ("Keys:", " "),
    ("Select keyboard", " "),
    ("A Formula 3 car", " "),
    ("The pedals", " "),
    ("In your Ralt", " "),
    ("speedometer", " "),
    ("Loading the game", " "),
    ("See how revs are lost", " "),
    ("Having elected to enter", " "),
    ("Having decided on how long", " "),
    ("Racing involves more", " "),
    ("Along the top", " "),
    ("The precise", " "),
    # Silverstone splash auto-advances, just wait for the next screen
    ("PRACTICE", "1"),
    ("PRESS SPACE BAR TO CONTINUE", " "),
    ("SELECT WING SETTINGS", "\r"),
    ("front", "\r"),
    ("PRESS SPACE BAR TO CONTINUE", " "),
]


def _find_revs_disc() -> Path | None:
    """Find the Revs disc image."""
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [
        repo_root / "discs" / "games" / REVS_DISC_FILENAME,
        repo_root / "tests" / "assets" / "discs" / REVS_DISC_FILENAME,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


@pytest.fixture(scope="module")
def revs_disc_filepath() -> Path:
    """Path to the Revs disc image."""
    path = _find_revs_disc()
    if path is None:
        pytest.skip(f"Revs disc image not found: {REVS_DISC_FILENAME}")
    return path


@pytest.fixture
def bbc_revs(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    revs_disc_filepath: Path,
) -> Beebium:
    """A BBC Micro that auto-boots the Revs disc on power-on.

    Configures:
    - Acorn 1770 disc controller
    - 1770 DFS ROM in sideways slot 14
    - Auto-boot keyboard link
    - Revs disc in drive 0
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
                "--auto-boot",
                "--floppy",
                f"0:{revs_disc_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.debugger.stop()
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _navigate_to_gameplay(bbc: Beebium) -> bool:
    """Navigate through Revs instructional screens to gameplay.

    Returns True if all screens were found and navigated successfully.
    """
    def dump_failure(step_num: int, waiting_for: str) -> None:
        rows = read_mode7_screen(bbc)
        print(f"\nStep {step_num}: Failed waiting for: {waiting_for}")
        print("Current screen:")
        for i, row in enumerate(rows):
            print(f"  Row {i:2d}: [{row}]")

    # If a screen never appears, the previous keypress was most likely dropped
    # -- the press is server-paced over the keyboard matrix, but the fast-forward
    # stops the machine between chunks, so under load a single press can miss the
    # MOS scan and not register. Re-press the previous key and wait again. A
    # screen that never appears even after re-pressing is a genuine navigation
    # failure and still fails here, so this discriminates a real fault from a
    # dropped keypress rather than masking it. (Text is not assumed to vanish on
    # the next screen -- some Revs headings persist -- so advance is judged by
    # the next screen appearing, not the current one leaving.)
    prev_key: str | None = None
    for step_num, (wait_text, key) in enumerate(REVS_BOOT_SEQUENCE, 1):
        found = False
        for _ in range(3):
            found = bbc.run_until_or_timeout(
                lambda text=wait_text: screen_contains(bbc, text),
                emulated_seconds=60.0,
                chunk_seconds=0.5,
            )
            if found or prev_key is None:
                break
            bbc.keyboard.type(prev_key)  # re-drive the dropped press
        if not found:
            dump_failure(step_num, repr(wait_text))
            return False
        bbc.keyboard.type(key)
        prev_key = key
    # Give the game time to enter its custom screen mode
    bbc.run_for_emulated_seconds(2.0)
    return True


class TestRevsTimingDiagnostics:
    """Diagnostic tests for Revs CRTC and VIA timer synchronization."""

    def test_boot_revs(self, bbc_revs: Beebium) -> None:
        """Boot Revs to gameplay and report CRTC state."""
        bbc = bbc_revs

        success = _navigate_to_gameplay(bbc)
        assert success, "Failed to navigate Revs to gameplay"

        bbc.debugger.ensure_stopped()
        state = bbc.crtc.state
        print("\n=== Revs CRTC State ===")
        print(f"R0 (htotal):      {state.registers[0]:3d} (0x{state.registers[0]:02X})")
        print(f"R1 (hdisplayed):  {state.registers[1]:3d} (0x{state.registers[1]:02X})")
        print(f"R2 (hsync_pos):   {state.registers[2]:3d} (0x{state.registers[2]:02X})")
        print(f"R3 (sync_width):  {state.registers[3]:3d} (0x{state.registers[3]:02X})")
        print(f"R4 (vtotal):      {state.registers[4]:3d} (0x{state.registers[4]:02X})")
        print(f"R5 (vtotal_adj):  {state.registers[5]:3d} (0x{state.registers[5]:02X})")
        print(f"R6 (vdisplayed):  {state.registers[6]:3d} (0x{state.registers[6]:02X})")
        print(f"R7 (vsync_pos):   {state.registers[7]:3d} (0x{state.registers[7]:02X})")
        print(f"R8 (interlace):   {state.registers[8]:3d} (0x{state.registers[8]:02X})")
        print(f"R9 (max_scanline):{state.registers[9]:3d} (0x{state.registers[9]:02X})")
        print(f"R12 (start_hi):   {state.registers[12]:3d} (0x{state.registers[12]:02X})")
        print(f"R13 (start_lo):   {state.registers[13]:3d} (0x{state.registers[13]:02X})")
        print(f"\nis_interlaced (R8 & 3 == 3): {state.is_interlaced}")
        print(f"is_interlace_sync (R8 & 1):  {(state.interlace_mode & 1) != 0}")

        # Calculate expected frame length
        total_rows = state.vtotal + 1
        scanlines_per_row = state.max_scanline + 1
        chars_per_line = state.htotal + 1
        vadj = state.vtotal_adj
        total_scanlines = total_rows * scanlines_per_row + vadj
        frame_chars = total_scanlines * chars_per_line
        print(
            f"\nExpected frame: {total_rows} rows x {scanlines_per_row} scanlines "
            f"+ {vadj} vadj = {total_scanlines} scanlines"
        )
        print(f"Frame period: {total_scanlines} x {chars_per_line} = {frame_chars} character clocks")

    def test_measure_frame_periods(self, bbc_revs: Beebium) -> None:
        """Measure consecutive frame periods to detect drift."""
        bbc = bbc_revs

        success = _navigate_to_gameplay(bbc)
        assert success, "Failed to navigate Revs to gameplay"

        bbc.debugger.ensure_stopped()

        # Measure frame periods by stepping cycles and detecting VSYNC transitions.
        STEP_SIZE = 32
        NUM_FRAMES = 20

        # Advance to a known state: wait for VSYNC to go inactive
        for _ in range(20000):
            bbc.debugger.step_cycles(STEP_SIZE)
            if not bbc.crtc.state.in_vsync:
                break

        # Wait for VSYNC rising edge
        for _ in range(20000):
            bbc.debugger.step_cycles(STEP_SIZE)
            if bbc.crtc.state.in_vsync:
                break

        # Wait for VSYNC falling edge
        for _ in range(20000):
            bbc.debugger.step_cycles(STEP_SIZE)
            if not bbc.crtc.state.in_vsync:
                break

        frame_start = bbc.debugger.cycle_count
        frame_periods = []

        for _frame_num in range(NUM_FRAMES):
            # Wait for next VSYNC rising edge
            for _ in range(20000):
                bbc.debugger.step_cycles(STEP_SIZE)
                if bbc.crtc.state.in_vsync:
                    break

            # Wait for VSYNC falling edge
            for _ in range(20000):
                bbc.debugger.step_cycles(STEP_SIZE)
                if not bbc.crtc.state.in_vsync:
                    break

            frame_end = bbc.debugger.cycle_count
            period = frame_end - frame_start
            frame_periods.append(period)
            frame_start = frame_end

        print("\n=== Frame Period Measurements (2MHz CPU cycles) ===")
        for i, period in enumerate(frame_periods):
            char_clocks = period / 2
            scanlines = char_clocks / (bbc.crtc.state.htotal + 1)
            print(f"Frame {i:2d}: {period:6d} cycles = {char_clocks:.1f} char clocks = {scanlines:.2f} scanlines")

        unique_periods = set(frame_periods)
        print(f"\nUnique periods: {sorted(unique_periods)}")
        print(f"Period range: {max(frame_periods) - min(frame_periods)} cycles")

        if len(unique_periods) == 1:
            print("All frames identical -- non-interlace or broken interlace")
        elif len(unique_periods) == 2:
            periods = sorted(unique_periods)
            diff = periods[1] - periods[0]
            avg = sum(frame_periods) / len(frame_periods)
            print(f"Two alternating periods (diff={diff} cycles) -- interlace")
            print(f"Average period: {avg:.1f} cycles = {avg / 2:.1f} char clocks")
        else:
            print(f"WARNING: {len(unique_periods)} different periods -- unstable timing")
