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

"""The station number in force versus the configured one, on a real machine (#172).

A Model B with ANFS takes its station number from the station links when it
boots. Renumbering it changes what the links present (``station_id``) but not
the number the filing system is using (``station_in_force``) until the guest
re-reads the links at Break; the status reports the change as pending until
then, and the watch stream reports the moment the two converge.
"""

from __future__ import annotations

import threading
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.pytest_plugin import LaunchBbc

#: The committed test ROMs.
TEST_ROMS_DIRPATH = Path(__file__).parent.parent.parent.parent / "tests" / "assets" / "roms"

#: Emulated seconds for the machine to reach the BASIC prompt (ANFS reads the
#: station links on the way).
BOOT_SECONDS = 10.0


def _at_prompt(bbc: Beebium) -> bool:
    lines = [line.strip() for line in bbc.video.screen_text().text.splitlines() if line.strip()]
    return bool(lines) and lines[-1] == ">"


def _boot(bbc: Beebium) -> None:
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc), BOOT_SECONDS, chunk_seconds=0.25), (
        "did not reach the BASIC prompt"
    )
    bbc.debugger.ensure_running()


@pytest.fixture
def station_80(launch_bbc: LaunchBbc) -> Beebium:
    anfs_filepath = TEST_ROMS_DIRPATH / "acorn-anfs_4_18.rom"
    if not anfs_filepath.is_file():
        pytest.skip("acorn-anfs_4_18.rom not available")
    bbc = launch_bbc(
        extra_args=[
            "--sideways",
            f"slot=14:type=rom:image={anfs_filepath}",
            "--station",
            "80",
            # Hermetic: no mDNS, no map file; nothing on the network is needed.
            "--aun",
            "port=0:discovery=off:map-file=none",
        ],
        startup_timeout=30.0,
    )
    _boot(bbc)
    return bbc


def test_a_renumber_is_pending_until_break_then_converges(station_80: Beebium) -> None:
    bbc = station_80
    status = bbc.econet.status
    assert status.station_id == 80
    assert status.station_in_force == 80
    assert status.station_change_pending is False

    bbc.econet.set_station_id(81)
    status = bbc.econet.status
    assert status.station_id == 81
    assert status.station_in_force == 80
    assert status.station_change_pending is True

    # The guest keeps using 80 while it runs: its INTOFF reads of the links
    # do not adopt the new number.
    bbc.run_until_or_timeout(lambda: False, 1.0, chunk_seconds=0.25)
    bbc.debugger.ensure_running()
    assert bbc.econet.status.station_in_force == 80

    # Watch, then Break: the stream reports the moment the two converge.
    converged: list = []
    stream = bbc.econet.watch_status(min_interval_ms=25)

    def watch() -> None:
        for update in stream:
            if update.station_in_force == 81:
                converged.append(update)
                return

    watcher = threading.Thread(target=watch, daemon=True)
    watcher.start()
    assert bbc.keyboard.press_break()
    _boot(bbc)
    # A backstop for the stream thread, not a measurement; the stream ends
    # when the machine is torn down.
    watcher.join(timeout=30.0)

    assert converged, "the watch stream never reported the guest adopting 81"
    assert converged[0].station_id == 81
    assert converged[0].station_change_pending is False
    status = bbc.econet.status
    assert status.station_in_force == 81
    assert status.station_change_pending is False
