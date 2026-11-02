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

"""Per-register Tube protocol trace mute (issue #250 follow-up).

Register 1 P-to-H is the OSWRCH byte stream, the busiest register, so on a long
run it fills the 1024-entry trace ring and evicts the register 2 to 4 protocol
entries a user is usually chasing. SetTubeTraceConfig mutes chosen
register/direction pairs: a muted pair still advances the transfer counters,
only the trace ring stops recording it. This drives it through the public
client: muting register 1 leaves its counters moving but keeps it out of the
trace, register 2 is unaffected, and re-enabling records it again.
"""

from __future__ import annotations

import os
import sys
from collections import Counter

import pytest

from beebium.client import Beebium
from beebium.client._proto import debugger_pb2
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.tube_ula import TubeTraceConfig

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)


@_skip_windows_ci
class TestTubeTraceConfig:
    """A muted register stays out of the trace but still counts (#250)."""

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

    def _snapshot(self, bbc: Beebium):
        bbc.debugger.ensure_stopped()
        state = bbc.tube_ula._stub.GetTubeState(debugger_pb2.GetTubeStateRequest())
        bbc.debugger.run()
        return state

    def _registers_after_a_print_loop(self, bbc: Beebium) -> tuple[Counter, int]:
        """Run a short PRINT loop; return the per-register trace tally and the
        register 1 transfer-counter delta over the same window."""
        before = self._snapshot(bbc)
        bbc.keyboard.type('FOR I=1 TO 40:PRINT "X";:NEXT\r')
        bbc.keyboard.wait_until_typing_complete()
        bbc.run_for_emulated_seconds(2.0)
        after = self._snapshot(bbc)

        new = after.trace_total_count - before.trace_total_count
        assert new < 1024, f"trace ring wrapped ({new}); loop too long for the measure"
        window = list(after.trace)[-new:] if new else []
        by_register = Counter(entry.tag >> 4 for entry in window)
        r1_delta = (
            (after.counters.r1_h2p_writes - before.counters.r1_h2p_writes)
            + (after.counters.r1_h2p_reads - before.counters.r1_h2p_reads)
            + (after.counters.r1_p2h_writes - before.counters.r1_p2h_writes)
            + (after.counters.r1_p2h_reads - before.counters.r1_p2h_reads)
        )
        return by_register, r1_delta

    def test_muting_register1_keeps_it_out_of_the_trace_but_counted(
        self, bbc_tube: Beebium
    ) -> None:
        bbc = bbc_tube

        # Mute register 1 in both directions, through the public client. The
        # applied config comes back, and the state reflects it.
        applied = bbc.tube_ula.set_trace_config(
            TubeTraceConfig(mute_r1_h2p=True, mute_r1_p2h=True)
        )
        assert applied.mute_r1_h2p and applied.mute_r1_p2h
        assert not applied.mute_r2_h2p
        state_config = bbc.tube_ula.state.trace_config
        assert state_config.mute_r1_h2p and state_config.mute_r1_p2h

        by_register, r1_delta = self._registers_after_a_print_loop(bbc)
        # Register 1 is muted: no entries, though its counters moved.
        assert by_register[1] == 0, (
            f"register 1 muted but still traced: {sorted(by_register.items())}"
        )
        assert r1_delta > 0, "register 1 counters should still advance when muted"
        # Register 2 is not muted, so the protocol handshake is still visible.
        assert by_register[2] > 0, "register 2 should still be traced"

        # Re-enabling records register 1 again.
        restored = bbc.tube_ula.set_trace_config(TubeTraceConfig())
        assert not restored.mute_r1_h2p and not restored.mute_r1_p2h
        by_register_again, _ = self._registers_after_a_print_loop(bbc)
        assert by_register_again[1] > 0, "register 1 should be traced after re-enable"
