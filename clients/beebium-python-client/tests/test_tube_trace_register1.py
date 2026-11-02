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

"""Scenario guard for issue #250: the Tube protocol trace records register 1.

Register 1 was never hooked into the protocol trace, so its OSWRCH byte stream
(and host-to-parasite Escape and events) was invisible while registers 2 to 4
were recorded. This drives the reporter's measure: after a short PRINT loop the
number of register 1 trace entries equals the register 1 transfer-counter
deltas, with the loop kept small so the 1024-entry ring cannot wrap.

The trace is not on the public client surface, so this reaches the service stub
directly, exactly as the reporter did.
"""

from __future__ import annotations

import os
import sys
from collections import Counter

import pytest

from beebium.client import Beebium
from beebium.client._proto import debugger_pb2
from beebium.client.exceptions import ServerNotFoundError

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)


@_skip_windows_ci
class TestTubeTraceRegister1:
    """The protocol trace records register 1 traffic like the others (#250)."""

    @pytest.fixture
    def bbc_tube(
        self,
        mos_filepath,
        basic_filepath,
        beebium_server_filepath,
        dfs_1770_rom_filepath,
    ) -> Beebium:
        try:
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server=beebium_server_filepath,
                extra_args=[
                    "--tube-65c02",
                    "--fdc",
                    "acorn-1770",
                    "--sideways",
                    f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
                ],
                startup_timeout=20.0,
            ) as bbc:
                assert bbc.expect(">", timeout=60), "BASIC prompt never appeared"
                bbc.run_for_emulated_seconds(1.0)
                yield bbc
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    def test_register1_is_traced(self, bbc_tube: Beebium) -> None:
        bbc = bbc_tube

        def snapshot():
            bbc.debugger.ensure_stopped()
            state = bbc.tube_ula._stub.GetTubeState(debugger_pb2.GetTubeStateRequest())
            bbc.debugger.run()
            return state

        before = snapshot()
        bbc.keyboard.type('FOR I=1 TO 40:PRINT "X";:NEXT\r')
        bbc.keyboard.wait_until_typing_complete()
        bbc.run_for_emulated_seconds(2.0)
        after = snapshot()

        new = after.trace_total_count - before.trace_total_count
        assert 0 < new < 1024, (
            f"trace grew by {new}; the loop must be small enough that the "
            f"1024-entry ring does not wrap for this measure to hold"
        )

        window = list(after.trace)[-new:]
        by_register = Counter(entry.tag >> 4 for entry in window)

        # The fix: register 1 is traced. Before #250 this count was zero.
        assert by_register[1] > 0, (
            f"no register 1 trace entries; registers seen: {sorted(by_register.items())}"
        )
        # And the other registers are still traced (no regression).
        assert by_register[2] > 0, "register 2 traffic should still be traced"

        # The reporter's measure: register 1 trace entries equal the register 1
        # transfer-counter deltas across both directions, since each counted
        # byte now emits exactly one trace entry and the ring has not wrapped.
        r1_counter_delta = (
            (after.counters.r1_h2p_writes - before.counters.r1_h2p_writes)
            + (after.counters.r1_h2p_reads - before.counters.r1_h2p_reads)
            + (after.counters.r1_p2h_writes - before.counters.r1_p2h_writes)
            + (after.counters.r1_p2h_reads - before.counters.r1_p2h_reads)
        )
        assert by_register[1] == r1_counter_delta, (
            f"register 1 trace entries {by_register[1]} != register 1 counter "
            f"delta {r1_counter_delta}"
        )
