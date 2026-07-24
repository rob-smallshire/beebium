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

"""Auto-boot behaviour: booting a disc by Shift-Break.

DFS runs a disc's !BOOT file when Shift is held across a BREAK. The subtlety
that makes this easy to get wrong from software is timing: DFS reads the Shift
key a little way *into* the reset routine, so Shift must remain held for a short
while after Break is released, not merely during it. Keyboard.shift_break and
the Beebium.boot_disc helper encapsulate that; these tests pin the end-to-end
behaviour, which was previously only covered by byte-level link round-trips.

Elite is used because it is a committed disc that auto-boots; it needs the Tube,
so these run with --tube-65c02.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest
from tube_test_helpers import dump_diagnostics, run_until_or_timeout

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)

ELITE_DISC_FILENAME = "Disc999-EliteSNG45.ssd"
ELITE_BANNER = "6502 Second Processor ELITE"


def _find_elite_disc() -> Path | None:
    repo_root = Path(__file__).parent.parent.parent.parent
    for candidate in (
        repo_root / "tests" / "assets" / "discs" / ELITE_DISC_FILENAME,
        repo_root / "discs" / "games" / ELITE_DISC_FILENAME,
    ):
        if candidate.exists():
            return candidate
    return None


@pytest.fixture(scope="module")
def elite_disc_filepath() -> Path:
    path = _find_elite_disc()
    if path is None:
        pytest.skip(f"Elite disc image not found: {ELITE_DISC_FILENAME}")
    return path


@pytest.fixture
def bbc_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Beebium:
    """A cold-booted Tube BBC at the BASIC prompt, no disc booted yet."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            if not run_until_or_timeout(
                bbc, lambda: screen_contains(bbc, "Acorn TUBE"),
                emulated_seconds=30.0,
            ):
                dump_diagnostics(bbc)
                pytest.fail("Tube banner not visible after boot")
            with bbc.debugger.running():
                yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@_skip_windows_ci
def test_boot_disc_autoboots_via_shift_break(
    bbc_tube: Beebium, elite_disc_filepath: Path
) -> None:
    """boot_disc runs the disc's !BOOT purely via Shift-Break (no typed command)."""
    # Precondition: nothing booted yet.
    assert not screen_contains(bbc_tube, ELITE_BANNER)

    bbc_tube.boot_disc(elite_disc_filepath)

    booted = run_until_or_timeout(
        bbc_tube, lambda: screen_contains(bbc_tube, ELITE_BANNER),
        emulated_seconds=60.0,
    )
    if not booted:
        dump_diagnostics(bbc_tube)
    assert booted, "Shift-Break did not auto-boot the disc's !BOOT"


@_skip_windows_ci
def test_naive_shift_break_without_hold_does_not_autoboot(
    bbc_tube: Beebium, elite_disc_filepath: Path
) -> None:
    """Releasing Shift immediately after Break misses DFS's read, so no auto-boot.

    This pins *why* boot_disc holds Shift past the break: with shift_hold_after
    at zero the key is gone before DFS samples it, and !BOOT never runs. If the
    emulator's reset timing changes such that this starts auto-booting, this
    test should be revisited rather than silently masking a shift-timing bug.
    """
    bbc_tube.disc.drive(0).insert(elite_disc_filepath)
    bbc_tube.debugger.ensure_running()
    # Hold Shift only during the break, release it the instant Break comes up.
    bbc_tube.keyboard.shift_break(hold_time=0.02, shift_hold_after=0.0)

    booted = run_until_or_timeout(
        bbc_tube, lambda: screen_contains(bbc_tube, ELITE_BANNER),
        emulated_seconds=20.0,
    )
    assert not booted, "expected no auto-boot when Shift is released too early"
