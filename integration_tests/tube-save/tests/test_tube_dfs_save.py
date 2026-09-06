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

"""Integration tests for BASIC LOAD/SAVE over DFS with and without the Tube.

Reproduces a byte-doubling bug: when BASIC SAVEs a program to DFS while
the 65C02 Tube second processor is active, every byte in the output file
is doubled (e.g. 0x0D 0x00 becomes 0x0D 0x0D 0x00 0x00).

The SAVE path uses the Tube R3/R4 bulk transfer protocol to move file
data from parasite memory to the host filing system. The LOAD direction
(host to parasite) works correctly -- games like Chuckie Egg 2023 and
Tube Elite boot fine.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from oaknut.dfs import DFS, ACORN_DFS_40T_SINGLE_SIDED

from beebium.client import Beebium
from beebium.client.screen import screen_contains, read_mode7_screen, dump_screen

from tube_save_helpers import TUBE_CYCLES_PER_KEY, run_until_or_timeout, SSD_SIZE


_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)


def _read_saved_file(ssd_filepath: Path) -> bytes | None:
    """Read the saved file from the output SSD image."""
    data = ssd_filepath.read_bytes()
    buf = bytearray(data)
    dfs = DFS.from_buffer(memoryview(buf), ACORN_DFS_40T_SINGLE_SIDED)
    test_file = dfs.path("$.TEST")
    if test_file.exists():
        return test_file.read_bytes()
    return None


def _diagnose_mismatch(original: bytes, saved: bytes) -> str:
    """Produce a diagnostic message for a byte mismatch."""
    # Check for byte-doubling pattern
    doubled = bytes(b for byte in original for b in (byte, byte))
    if len(saved) == len(original):
        # Same length but different content -- check if first half is doubled
        half = len(saved) // 2
        if saved[:half] == doubled[:half]:
            return (
                f"Byte-doubling detected: each byte was written twice, "
                f"truncated to original length ({len(saved)} bytes). "
                f"First mismatch context follows."
            )
    elif len(saved) == 2 * len(original) and saved == doubled:
        return (
            f"Byte-doubling detected: output is exactly 2x original size "
            f"({len(saved)} vs {len(original)} bytes), with each byte doubled."
        )

    # Find first divergence
    for i, (a, b) in enumerate(zip(original, saved)):
        if a != b:
            ctx_start = max(0, i - 8)
            ctx_end = min(len(saved), i + 8)
            return (
                f"Mismatch at offset {i}: expected 0x{a:02X}, got 0x{b:02X}\n"
                f"  Original: {original[ctx_start:ctx_end].hex(' ')}\n"
                f"  Saved:    {saved[ctx_start:ctx_end].hex(' ')}"
            )

    if len(saved) != len(original):
        return f"Length mismatch: expected {len(original)}, got {len(saved)}"

    return "Unknown mismatch"


def _has_prompt_after(bbc: Beebium, command: str) -> bool:
    """Check that a '>' prompt appears on a line AFTER the given command."""
    rows = read_mode7_screen(bbc)
    found_command = False
    for row in rows:
        stripped = row.strip()
        if command in stripped:
            found_command = True
        elif found_command and stripped == ">":
            return True
    return False


def _load_and_save(bbc: Beebium, cycles_per_key: int, emulated_seconds: float) -> bool:
    """Type LOAD and SAVE commands, waiting for completion."""
    load_cmd = 'LOAD ":0.TEST"'
    bbc.keyboard.type(load_cmd + '\r', cycles_per_key=cycles_per_key)
    ok = bbc.run_until_or_timeout(
        lambda: _has_prompt_after(bbc, load_cmd),
        emulated_seconds=emulated_seconds,
    )
    if not ok:
        return False

    save_cmd = 'SAVE ":1.TEST"'
    bbc.keyboard.type(save_cmd + '\r', cycles_per_key=cycles_per_key)
    ok = bbc.run_until_or_timeout(
        lambda: _has_prompt_after(bbc, save_cmd),
        emulated_seconds=emulated_seconds,
    )
    return ok


def _load_and_save_tube(bbc: Beebium) -> bool:
    """Type LOAD and SAVE commands with Tube, waiting for completion."""
    load_cmd = 'LOAD ":0.TEST"'
    bbc.keyboard.type(load_cmd + '\r', cycles_per_key=TUBE_CYCLES_PER_KEY)
    ok = run_until_or_timeout(
        bbc,
        lambda: _has_prompt_after(bbc, load_cmd),
        emulated_seconds=30.0,
    )
    if not ok:
        return False

    save_cmd = 'SAVE ":1.TEST"'
    bbc.keyboard.type(save_cmd + '\r', cycles_per_key=TUBE_CYCLES_PER_KEY)
    ok = run_until_or_timeout(
        bbc,
        lambda: _has_prompt_after(bbc, save_cmd),
        emulated_seconds=30.0,
    )
    return ok


def _eject_and_read(bbc: Beebium, blank_ssd_filepath: Path) -> bytes:
    """Eject drive 1 to flush writes, then read the saved file."""
    # Request immediate eject (flushes dirty tracks synchronously)
    bbc.disc.drive(1).eject(immediate=True)
    bbc.disc.drive(1).wait_for_eject(timeout=10.0)
    # Read back from the flushed SSD file
    saved = _read_saved_file(blank_ssd_filepath)
    assert saved is not None, "Saved file $.TEST not found on output SSD"
    return saved


class TestDfsSaveWithoutTube:
    """Control tests: LOAD/SAVE without Tube should produce exact copies."""

    def test_save_without_tube(
        self,
        bbc_no_tube: Beebium,
        test_program_bytes: bytes,
        blank_ssd_filepath: Path,
    ) -> None:
        """SAVE without Tube produces a byte-exact copy."""
        ok = _load_and_save(bbc_no_tube, cycles_per_key=100_000, emulated_seconds=15.0)
        screen = dump_screen(bbc_no_tube)
        assert ok, f"LOAD/SAVE did not complete:\n{screen}"
        print(f"Screen after LOAD/SAVE:\n{screen}")

        saved = _eject_and_read(bbc_no_tube, blank_ssd_filepath)
        if saved != test_program_bytes:
            pytest.fail(
                f"Without Tube: {_diagnose_mismatch(test_program_bytes, saved)}"
            )


@_skip_windows_ci
class TestDfsSaveWithTube:
    """Bug reproduction: LOAD/SAVE with Tube active."""

    def test_save_with_tube(
        self,
        bbc_with_tube: Beebium,
        test_program_bytes: bytes,
        blank_ssd_filepath: Path,
    ) -> None:
        """SAVE with Tube should produce a byte-exact copy.

        This test reproduces the byte-doubling bug where each byte
        in the saved file appears twice.
        """
        ok = _load_and_save_tube(bbc_with_tube)
        assert ok, f"LOAD/SAVE did not complete:\n{dump_screen(bbc_with_tube)}"

        saved = _eject_and_read(bbc_with_tube, blank_ssd_filepath)
        if saved != test_program_bytes:
            pytest.fail(
                f"With Tube: {_diagnose_mismatch(test_program_bytes, saved)}"
            )

    def test_load_with_tube(
        self,
        bbc_with_tube: Beebium,
        test_program_bytes: bytes,
    ) -> None:
        """LOAD with Tube correctly transfers data to parasite memory.

        Verifies the host-to-parasite direction works, isolating
        any bug to the SAVE (parasite-to-host) direction.
        """
        load_cmd = 'LOAD ":0.TEST"'
        bbc_with_tube.keyboard.type(load_cmd + '\r', cycles_per_key=TUBE_CYCLES_PER_KEY)
        ok = run_until_or_timeout(
            bbc_with_tube,
            lambda: _has_prompt_after(bbc_with_tube, load_cmd),
            emulated_seconds=30.0,
        )
        assert ok, f"LOAD did not complete:\n{dump_screen(bbc_with_tube)}"

        # Read parasite memory: PAGE is &0800 on the 65C02 coprocessor
        page = 0x0800
        program_length = len(test_program_bytes)
        parasite = bbc_with_tube.connect_parasite()
        parasite_data = parasite.memory.address.peek.read(page, program_length)

        if parasite_data != test_program_bytes:
            pytest.fail(
                f"LOAD direction mismatch: "
                f"{_diagnose_mismatch(test_program_bytes, parasite_data)}"
            )
