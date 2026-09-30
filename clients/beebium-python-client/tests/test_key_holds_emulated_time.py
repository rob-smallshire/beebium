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

"""Break and Shift-Break hold keys for emulated time, not wall time (#125).

A slow host runs the machine below real time, so a hold measured in wall time
lasts fewer emulated cycles there and the reset path can miss the key. The
holds are measured in emulated cycles, so the machine sees the same press at
any speed; 0.2x stands in for a slow CI host.
"""

from __future__ import annotations

import contextlib
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import DebuggerError, ServerNotFoundError
from beebium.client.screen import screen_contains

from tube_test_helpers import dump_diagnostics

ELITE_DISC_FILENAME = "Disc999-EliteSNG45.ssd"
ELITE_BANNER = "6502 Second Processor ELITE"
SLOW_HOST_SPEED = 0.2


@pytest.fixture(scope="module")
def elite_disc_filepath() -> Path:
    repo_root = Path(__file__).parent.parent.parent.parent
    for candidate in (
        repo_root / "tests" / "assets" / "discs" / ELITE_DISC_FILENAME,
        repo_root / "discs" / "games" / ELITE_DISC_FILENAME,
    ):
        if candidate.exists():
            return candidate
    pytest.skip(f"Elite disc image not found: {ELITE_DISC_FILENAME}")


@pytest.fixture
def tube_launch(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
):
    base = [
        "--tube-65c02", "--fdc", "acorn-1770",
        "--sideways", f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
    ]

    @contextlib.contextmanager
    def _launch(extra: list[str] | None = None):
        try:
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server=beebium_server_filepath,
                extra_args=base + list(extra or []),
                startup_timeout=20.0,
            ) as bbc:
                bbc.disc.set_spin_up_delay(False)
                assert _fast_forward_until(bbc, lambda: screen_contains(bbc, "Acorn TUBE"), 30.0)
                yield bbc
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    return _launch


def _fast_forward_until(bbc: Beebium, predicate, emulated_seconds: float,
                        chunk_seconds: float = 1.0) -> bool:
    """Run unpaced until `predicate` or the emulated budget, then return to 1x."""
    bbc.system.set_speed_multiplier(0.0)
    try:
        return bbc.run_until_or_timeout(predicate, emulated_seconds, chunk_seconds=chunk_seconds)
    finally:
        bbc.system.set_speed_multiplier(1.0)


def _boots(bbc: Beebium) -> bool:
    booted = _fast_forward_until(bbc, lambda: screen_contains(bbc, ELITE_BANNER), 60.0)
    if not booted:
        dump_diagnostics(bbc)
    return booted


@pytest.mark.parametrize("attempt", range(5))
def test_press_break_boots_with_the_auto_boot_link_on_a_slow_host(
    tube_launch, elite_disc_filepath: Path, attempt: int
) -> None:
    with tube_launch(["--auto-boot"]) as bbc:
        bbc.disc.drive(0).insert(elite_disc_filepath)
        bbc.system.set_speed_multiplier(SLOW_HOST_SPEED)
        bbc.debugger.ensure_running()
        bbc.keyboard.press_break()  # plain BREAK, no Shift
        assert _boots(bbc), "plain BREAK did not boot with the auto-boot link set"


def test_shift_break_boots_on_a_slow_host(tube_launch, elite_disc_filepath: Path) -> None:
    with tube_launch() as bbc:
        bbc.disc.drive(0).insert(elite_disc_filepath)
        bbc.system.set_speed_multiplier(SLOW_HOST_SPEED)
        bbc.debugger.ensure_running()
        bbc.keyboard.shift_break()
        assert _boots(bbc), "Shift-Break did not auto-boot"


def test_shift_released_at_break_does_not_boot_on_a_slow_host(
    tube_launch, elite_disc_filepath: Path
) -> None:
    # The negative case keeps its meaning at any speed: with no hold after
    # Break, DFS reads Shift after it has gone.
    with tube_launch() as bbc:
        bbc.disc.drive(0).insert(elite_disc_filepath)
        bbc.system.set_speed_multiplier(SLOW_HOST_SPEED)
        bbc.debugger.ensure_running()
        bbc.keyboard.shift_break(shift_hold_after=0.0)
        _fast_forward_until(bbc, lambda: False, 25.0, chunk_seconds=5.0)
        assert not screen_contains(bbc, ELITE_BANNER), "booted with Shift released at Break"


@pytest.mark.parametrize("speed", [1.0, SLOW_HOST_SPEED])
def test_press_break_holds_for_its_emulated_time(bbc: Beebium, speed: float) -> None:
    bbc.system.set_speed_multiplier(speed)
    bbc.debugger.ensure_running()
    before = bbc.debugger.cycle_count
    bbc.keyboard.press_break(hold_time=0.02)
    # 0.02 emulated seconds is 40,000 cycles at 2 MHz, whatever the speed.
    assert bbc.debugger.cycle_count - before >= 40_000
    assert not bbc.keyboard.is_break_held()


def test_key_holds_refuse_a_stopped_machine_without_leaving_keys_down(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    with pytest.raises(DebuggerError, match="machine is stopped"):
        bbc.keyboard.shift_break()
    with pytest.raises(DebuggerError, match="machine is stopped"):
        bbc.keyboard.press_break()
    with pytest.raises(DebuggerError, match="machine is stopped"):
        bbc.keyboard.ctrl_break()
    assert not bbc.keyboard.is_break_held()
    assert not any(bbc.keyboard.get_state().pressed_rows)
