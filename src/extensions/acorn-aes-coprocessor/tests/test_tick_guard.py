# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.


# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# test_tick_guard.py, a whole program's tick total, read by the debugger, against a recorded number
# version 2.0.0, 20 November 2026.

"""Step 4.6: the emulator's ticks for one whole Tak(18,12,6), our AsTAK1t on the frozen arm_tak_1
disc, between two breakpoints: the MOV R1,#18 that starts the call (&102C) and the instruction the
BL returns to (&103C). The tripwire sees only centisecond readings, so a one-tick change in a
cycle's cost passes it; here it moves the total by thousands. A regression guard on the emulator,
not evidence about a real board: the number is what this emulator charged on 19 Nov 2026."""
import time

from aes_host import Aes, copy_frozen

from beebium.client._proto import debugger_pb2 as pb

START, END = 0x102C, 0x103C  # AsTAK1t, read from the disc's binary (INF, a disassembly).
TICKS = 5_464_479  # Recorded, step 4.6 (19 Nov 2026), on plugin 98c0495b1480 and the 1.01 ROM.
# Refresh runs on its own clock (AesTiming, time.refresh_period 192 ticks), so where the call
# starts against it can add or drop one refresh: one time.refresh_cost, 9 ticks, either way.
ONE_REFRESH = 9
TAK = 7  # Tak(18,12,6), the program's own printed result.


def stopped(stub):
    return not stub.GetState(pb.Empty()).is_running


def test_tak_ticks(tmp_path):
    disc = copy_frozen("arm_tak_1", tmp_path)
    with Aes(disc=disc) as aes:
        aes.boot(hooks=False)
        stub = aes.bbc._connection.coprocessor_debugger_stub
        for at in (START, END):
            assert stub.AddBreakpoint(pb.AddBreakpointRequest(start_address=at)).success
        aes.type("AsTAK1t\r")
        assert aes.run(lambda: stopped(stub), 30), aes.screen()
        first = stub.GetState(pb.Empty()).cycle_count
        assert {r.name: r.value for r in stub.GetCpuState(pb.Empty()).registers}["R15"] == START
        # A Run is refused while the host is stopped (test_debugger.py), so the host runs too.
        aes.bbc.debugger.run()
        try:
            assert stub.Run(pb.RunRequest()).success
            end = time.monotonic() + 30
            while not stopped(stub) and time.monotonic() < end:
                time.sleep(0.01)
        finally:
            aes.bbc.debugger.stop()
        assert stopped(stub)
        second = stub.GetState(pb.Empty()).cycle_count
        regs = {r.name: r.value for r in stub.GetCpuState(pb.Empty()).registers}
    assert regs["R15"] == END and regs["R0"] == TAK
    assert abs((second - first) - TICKS) <= ONE_REFRESH, f"{second - first} ticks, recorded {TICKS}"
