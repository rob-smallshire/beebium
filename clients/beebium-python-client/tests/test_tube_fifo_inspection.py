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

"""Scenario guard for issue #252: the R1 P-to-H FIFO inspection snapshot.

DeviceInspectionService once clamped TubeFifo24State.count to 0 or 1 and copied
only the head byte, because the inspection surface offered no way to see past
the head of the FIFO. The proto has always defined count as the number of bytes
in the FIFO (0-24) and data as the contents in read order, so the view was
wrong when more than one byte was queued.

With ADFS as the only sideways ROM the host runs no Tube host code (its Tube
host code lives in the DFS ROM), so the second processor's start-up banner
piles up unread in the R1 P-to-H FIFO. After a short run the FIFO holds the full
banner, and GetTubeState must report every byte, not just the head.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)

ADFS_ROM_FILENAME = "acorn-adfs_1_30.rom"

# The 65C02 second processor's start-up banner, as it reaches the host over R1
# P-to-H: a line feed, "Acorn TUBE 6502 64K", then the trailing control bytes
# that fill the 24-byte FIFO. The host never reads it here, so all 24 bytes stay
# queued.
EXPECTED_BANNER_PREFIX = b"\nAcorn TUBE 6502 64K"


@pytest.fixture
def adfs_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    path = beebium_roms_dirpath / ADFS_ROM_FILENAME
    if not path.exists():
        pytest.skip(f"ADFS ROM not found: {path}")
    return path


@_skip_windows_ci
class TestTubeFifoInspection:
    """GetTubeState reports the whole R1 P-to-H FIFO, not just its head (#252)."""

    @pytest.fixture
    def bbc_tube(
        self,
        mos_filepath: Path,
        basic_filepath: Path | None,
        beebium_server_filepath: Path | None,
        dfs_1770_rom_filepath: Path,
        adfs_rom_filepath: Path,
    ) -> Beebium:
        """A Model B with ADFS as the only sideways ROM and a 65C02 coprocessor.

        ADFS carries no Tube host code, so nothing drains R1 P-to-H and the
        coprocessor's banner stays queued for inspection.
        """
        try:
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server=beebium_server_filepath,
                extra_args=[
                    "--fdc",
                    "acorn-1770",
                    "--sideways",
                    f"slot=14:type=rom:image={adfs_rom_filepath}",
                    "--tube-65c02",
                ],
                startup_timeout=20.0,
            ) as bbc:
                yield bbc
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    def test_r1_p2h_reports_full_fifo(self, bbc_tube: Beebium) -> None:
        """The P-to-H FIFO holds the whole 24-byte banner, count and all."""
        bbc_tube.run_for_emulated_seconds(6.0)

        state = bbc_tube.tube_ula.state
        r1_p2h = state.r1_p2h
        data = bytes(r1_p2h.data)

        # The FIFO is 24 bytes and the host never drained it, so it is full.
        assert r1_p2h.count == 24, (
            f"r1_p2h.count {r1_p2h.count} != 24 (issue #252: FIFO count was "
            f"clamped to 0 or 1)"
        )
        # Every queued byte is reported, in read order -- not just the head.
        assert len(data) == 24, f"r1_p2h.data has {len(data)} bytes, expected 24"
        assert r1_p2h.data_available is True
        assert data.startswith(EXPECTED_BANNER_PREFIX), (
            f"r1_p2h.data {data!r} does not start with the second-processor banner"
        )

        # The count agrees with the write-minus-read transfer counters, which
        # was the reporter's workaround for the clamped count.
        delta = state.counters.r1_p2h_writes - state.counters.r1_p2h_reads
        assert r1_p2h.count == delta, (
            f"r1_p2h.count {r1_p2h.count} != counter delta {delta}"
        )
