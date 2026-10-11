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

"""Integration tests for the acorn-rtc and acorn-scsi extension adapters.

Each launches a real server with the extension attached and drives it through
the typed bridge (bbc.extensions[AcornRtc] / AcornScsi.attach(bbc)).
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.ext.peripheral.acorn_rtc import AcornRtc, RtcTime
from beebium.ext.peripheral.acorn_scsi import AcornScsi, ScsiBusStatus, ScsiTarget


@pytest.fixture(scope="module")
def bbc_rtc(mos_filepath: Path, basic_filepath, beebium_server_filepath):
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            # The default 4-bit-year layout only spans 1981-1996 and refuses to
            # start from the host's (2026) wall clock. The 7-bit-year layout
            # spans 1981-2099, so the server starts from the real clock with no
            # pinned date -- and SetTime honours that range too.
            extra_args=["--acorn-rtc", "layout=7bit-year-in-r7"],
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture(scope="module")
def bbc_scsi(mos_filepath: Path, basic_filepath, beebium_server_filepath):
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=["--acorn-scsi"],
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_rtc_adapter_is_bound(bbc_rtc):
    rtc = bbc_rtc.extensions[AcornRtc]
    assert isinstance(rtc, AcornRtc)
    assert rtc.name == "acorn-rtc"


def test_rtc_set_get_round_trip(bbc_rtc):
    # A contemporary date, representable under the 7-bit-year layout -- SetTime
    # honours the active layout's range rather than a fixed 1981-2000 cap.
    rtc = AcornRtc.attach(bbc_rtc)
    rtc.set_time("2026-04-02T10:15")
    now = rtc.get_time()
    assert isinstance(now, RtcTime)
    assert now.year == 2026
    assert now.month == 4
    assert now.day == 2
    assert now.hour == 10
    assert now.minute == 15
    assert now.iso8601 == "2026-04-02T10:15"


def test_rtc_registers_are_eight_bytes(bbc_rtc):
    registers = bbc_rtc.extensions[AcornRtc].registers
    assert len(registers) == 8
    assert all(0 <= r <= 0xFF for r in registers)


def test_scsi_targets_enumerate(bbc_scsi):
    targets = bbc_scsi.extensions[AcornScsi].targets
    assert isinstance(targets, list)
    for target in targets:
        assert isinstance(target, ScsiTarget)
        assert 0 <= target.id <= 7


def test_scsi_bus_status(bbc_scsi):
    status = bbc_scsi.extensions[AcornScsi].bus_status
    assert isinstance(status, ScsiBusStatus)
    assert status.phase  # a non-empty phase name, e.g. "BUS_FREE"
