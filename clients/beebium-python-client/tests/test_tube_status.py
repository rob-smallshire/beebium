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

"""The Tube status reports the coprocessor plugin attached at launch (#205)."""

from __future__ import annotations

import pytest


@pytest.mark.parametrize(
    ("variant", "flag", "cpu", "clock_hz"),
    [
        ("model-b", "--tube-65c02", "65C02", 3_000_000),
        ("model-b-plus-128k", "--tube-65c02", "65C02", 3_000_000),
        ("model-b-integra-b", "--tube-65c102", "65C102", 4_000_000),
    ],
)
def test_reports_the_attached_coprocessor(launch_bbc, variant, flag, cpu, clock_hz) -> None:
    bbc = launch_bbc(variant=variant, extra_args=[flag])
    status = bbc.tube.status
    assert status.has_tube_socket
    assert status.coprocessor_connected
    assert status.coprocessor_cpu == cpu
    assert status.coprocessor_clock_hz == clock_hz
    listed = bbc.extensions.info(status.coprocessor_extension_id)
    assert listed is not None and listed.id == status.coprocessor_extension_id
    assert "tube" in listed.attaches_to


def test_reports_no_coprocessor_when_none_is_attached(launch_bbc) -> None:
    status = launch_bbc().tube.status
    assert status.has_tube_socket
    assert not status.coprocessor_connected
    assert status.coprocessor_cpu == ""
    assert status.coprocessor_clock_hz == 0
    assert status.coprocessor_extension_id == ""
