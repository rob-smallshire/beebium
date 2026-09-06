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

"""README snippet: a taste of the debugger (uses the pytest `bbc` fixture)."""


def test_debugger(bbc):
    bbc.expect("BASIC")
    # readme:begin
    state = bbc.debugger.stop()            # pause; returns the execution state
    print(state.is_running)                # -> False
    print(bbc.cpu.registers)               # A=.. X=.. Y=.. SP=.. PC=.. P=.. [flags]

    # Break the next time the OS writes a character, then let it run. The
    # breakpoint is removed automatically when the block exits.
    with bbc.debugger.breakpoint(0xFFEE):  # OSWRCH entry point
        bbc.keyboard.type("X")             # the echoed key drives OSWRCH
        bbc.debugger.run_and_wait_for_stop()
        print(f"stopped at PC=0x{bbc.cpu.pc:04X}")
    # readme:end
