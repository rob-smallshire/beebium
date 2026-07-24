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

import contextlib
import os
import sys
import time
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


# ---------------------------------------------------------------------------
# The auto-boot keyboard link (--auto-boot / set_startup_auto_boot).
#
# The link REVERSES the SHIFT-BREAK action: with it set, a plain BREAK boots the
# disc and holding SHIFT across BREAK suppresses the boot -- the inverse of the
# default (no link) behaviour that the tests above exercise.
# ---------------------------------------------------------------------------


@pytest.fixture
def tube_launch(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
):
    """Factory to launch a Tube machine with extra CLI args, skipping if the
    server is not found. Yields a context manager producing a Beebium."""
    base = [
        "--tube-65c02", "--fdc", "acorn-1770",
        "--sideways", f"14:rom:{dfs_1770_rom_filepath}",
    ]

    @contextlib.contextmanager
    def _launch(extra: list[str] | None = None):
        try:
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server_filepath=beebium_server_filepath,
                extra_args=base + list(extra or []),
                startup_timeout=20.0,
            ) as bbc:
                yield bbc
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    return _launch


@_skip_windows_ci
def test_auto_boot_link_boots_at_power_on(tube_launch, elite_disc_filepath: Path) -> None:
    """With the auto-boot link set (--auto-boot), the disc boots at power-on."""
    with tube_launch(["--auto-boot", "--floppy", f"0:{elite_disc_filepath}"]) as bbc:
        bbc.disc.set_spin_up_delay(False)
        booted = run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, ELITE_BANNER), emulated_seconds=60.0)
        if not booted:
            dump_diagnostics(bbc)
        assert booted, "auto-boot link did not boot the disc at power-on"


@_skip_windows_ci
def test_auto_boot_link_makes_plain_break_boot(
    tube_launch, elite_disc_filepath: Path
) -> None:
    """With the auto-boot link set, a plain BREAK boots the disc (no Shift)."""
    with tube_launch(["--auto-boot"]) as bbc:
        bbc.disc.set_spin_up_delay(False)
        assert run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, "Acorn TUBE"), emulated_seconds=30.0)
        bbc.disc.drive(0).insert(elite_disc_filepath)
        bbc.debugger.ensure_running()
        bbc.keyboard.press_break()  # plain BREAK, no Shift
        time.sleep(0.5)
        booted = run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, ELITE_BANNER), emulated_seconds=60.0)
        if not booted:
            dump_diagnostics(bbc)
        assert booted, "plain BREAK did not boot with the auto-boot link set"


@_skip_windows_ci
def test_auto_boot_link_reverses_shift_break(
    tube_launch, elite_disc_filepath: Path
) -> None:
    """With the auto-boot link set, holding Shift across BREAK SUPPRESSES boot."""
    with tube_launch(["--auto-boot"]) as bbc:
        bbc.disc.set_spin_up_delay(False)
        assert run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, "Acorn TUBE"), emulated_seconds=30.0)
        bbc.disc.drive(0).insert(elite_disc_filepath)
        bbc.debugger.ensure_running()
        # boot_disc holds Shift across the break; with the link set that inverts
        # to "do not boot".
        bbc.keyboard.shift_break()
        booted = run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, ELITE_BANNER), emulated_seconds=25.0)
        assert not booted, "Shift-Break should suppress boot when the link is set"


@_skip_windows_ci
def test_runtime_auto_boot_link_honored_by_hard_reset(
    tube_launch, elite_disc_filepath: Path
) -> None:
    """set_startup_auto_boot at runtime round-trips and is honoured by a hard
    reset (debugger.reset(), a power-on-equivalent that re-reads the links)."""
    with tube_launch([]) as bbc:
        bbc.disc.set_spin_up_delay(False)
        assert run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, "Acorn TUBE"), emulated_seconds=30.0)
        bbc.disc.drive(0).insert(elite_disc_filepath)

        assert bbc.keyboard.get_startup_auto_boot() is False
        bbc.keyboard.set_startup_auto_boot(True)
        assert bbc.keyboard.get_startup_auto_boot() is True

        bbc.debugger.reset()          # hard reset: re-reads the links
        bbc.debugger.ensure_running()
        booted = run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, ELITE_BANNER), emulated_seconds=60.0)
        if not booted:
            dump_diagnostics(bbc)
        assert booted, "runtime auto-boot link not honoured by a hard reset"


@_skip_windows_ci
def test_runtime_link_change_not_seen_by_soft_break(
    tube_launch, elite_disc_filepath: Path
) -> None:
    """A runtime link change is NOT honoured by a subsequent plain BREAK.

    This is faithful to OS 1.20, not a bug: the MOS commits the keyboard links
    to the startup-options byte (&028F) only on the power-on and hard-reset
    paths; a soft reset (plain BREAK) scans the links but branches around the
    store, so &028F keeps its power-on value, and the auto-boot decision reads
    that cached byte. So changing the link at runtime takes effect only on the
    next power-on / hard reset (see test above), never on a plain BREAK.
    """
    with tube_launch([]) as bbc:  # no --auto-boot: link off at power-on
        bbc.disc.set_spin_up_delay(False)
        assert run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, "Acorn TUBE"), emulated_seconds=30.0)
        bbc.disc.drive(0).insert(elite_disc_filepath)
        bbc.keyboard.set_startup_auto_boot(True)  # changes the live link only
        bbc.debugger.ensure_running()
        bbc.keyboard.press_break()                # soft reset: reuses &028F
        time.sleep(0.5)
        booted = run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, ELITE_BANNER), emulated_seconds=25.0)
        assert not booted, (
            "plain BREAK booted after a runtime link change; OS 1.20 only "
            "commits the links to &028F on power-on / hard reset")
