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

"""The automatic-station preset against a real server (issue #67).

The bundled ``model-b-disc-aun-auto`` preset carries ``"station": "auto"``: the
server chooses a free Econet station number at launch, browsing ``_aun._udp``,
so several clients can be started without renumbering. This checks the preset
launches and comes up with a station in the default auto range. It skips when no
server is available (the ``server_installation`` fixture handles that)."""

from __future__ import annotations

import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.installation import ServerInstallation


def test_auto_station_preset_comes_up_in_range(
    mos_filepath: Path,
    server_installation: ServerInstallation,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # Disable the per-host counter so the test is deterministic and never
    # touches the user's real state file (issue #161).
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_STATE_FILEPATH", "none")
    # The auto browse runs during server start-up, so allow a little longer for
    # the gRPC endpoint to become ready than the default.
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        preset="model-b-disc-aun-auto",
        startup_timeout=30.0,
    ) as bbc:
        # The station is chosen just after the port is up (deferred selection),
        # so poll until Econet is enabled rather than reading immediately.
        deadline = time.monotonic() + 30.0
        status = bbc.econet.status
        while time.monotonic() < deadline and not status.enabled:
            time.sleep(0.25)
            status = bbc.econet.status
        assert status.enabled
        # A number was chosen in the preset's default range (1-253), not left
        # at 0 and not out of range. The exact value depends on what else is on
        # the net, so only the range is asserted.
        assert 1 <= status.station_id <= 253


def test_run_during_auto_selection_starts_the_machine(
    mos_filepath: Path,
    server_installation: ServerInstallation,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # Reproduces the macOS app defect (black screen on the auto preset): the app
    # launches with --wait=api and calls DebuggerControl.Run ONCE, immediately
    # after the port is reachable -- which is while the deferred station
    # selection is still running. That early Run must be honoured so the machine
    # then runs; it must not fail and must not leave the machine paused forever.
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_MIN_OBSERVE_MS", "3000")
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_STATE_FILEPATH", "none")
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_BUDGET_MS", "4000")
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        preset="model-b-disc-aun-auto",
        extra_args=["--wait=api"],
        startup_timeout=3.0,  # port is up well before the 3 s selection ends
    ) as bbc:
        # Call Run now, while selection is still in progress. Post-fix this
        # blocks until selection completes, then starts the machine; it must not
        # raise ("already running" or otherwise).
        bbc.debugger.run()

        # The machine actually executes: the cycle count advances.
        start_cycles = bbc.debugger.cycle_count
        deadline = time.monotonic() + 20.0
        advanced = False
        while time.monotonic() < deadline:
            if bbc.debugger.cycle_count > start_cycles + 100_000:
                advanced = True
                break
            time.sleep(0.2)
        assert advanced, "machine never started running after Run during selection"

        # Econet came up on the auto-chosen number.
        status = bbc.econet.status
        assert status.enabled
        assert 1 <= status.station_id <= 253

        # And it really booted: the MOS banner names the Econet station.
        assert "Econet Station" in bbc.expect("Econet Station", timeout=20.0)


def test_port_is_listening_before_auto_selection_finishes(
    mos_filepath: Path,
    server_installation: ServerInstallation,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # Issue #67 launch-path fix: the gRPC port must be printed (and the server
    # listening) BEFORE the station browse runs, so a launcher's port-wait does
    # not time out on a slow selection. Force a long observation window via the
    # env hook, then launch with a SHORT startup timeout: the connection must
    # still succeed (the port is up early), and Econet is enabled with a station
    # in range once the deferred selection completes.
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_MIN_OBSERVE_MS", "4000")
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_STATE_FILEPATH", "none")
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_BUDGET_MS", "5000")
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--station", "auto", "--aun", "port=0:map-file=none"],
        # Well under the 4 s selection: a pass proves the port appeared first.
        startup_timeout=3.0,
    ) as bbc:
        # Poll until the deferred selection has enabled Econet (the selection
        # runs after the port is up; allow for a populated-LAN teardown too).
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            status = bbc.econet.status
            if status.enabled:
                break
            time.sleep(0.25)
        assert status.enabled
        assert 1 <= status.station_id <= 253
