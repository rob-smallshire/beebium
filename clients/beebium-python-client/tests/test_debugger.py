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

"""Integration tests for the debugger over gRPC from Python.

Exercises breakpoints, watchpoints, conditional expressions, hit counts,
event streaming, and execution control against a live beebium server.
Each test gets a fresh BBC Micro instance via the ``bbc`` fixture.
"""

from __future__ import annotations

import pytest

from beebium.client import Beebium
from beebium.client._proto import debugger_pb2
from beebium.client.cpu import Registers, StatusRegister
from beebium.client.debugger import Breakpoint, ExecutionStateEvent, Watchpoint
from beebium.client.exceptions import DebuggerError, InvalidConditionError


def run_and_wait_for_stop(bbc: Beebium) -> ExecutionStateEvent:
    """Resume and wait for the next stop, race-free (delegates to the client)."""
    return bbc.debugger.run_and_wait_for_stop()


def plant_and_run_from(bbc: Beebium, code: bytes, org: int = 0x0400) -> None:
    """Plant machine code at ``org`` and prepare the CPU to execute it.

    Resets the machine (which leaves it paused at an instruction boundary),
    writes the code, and sets PC to the code origin. The machine is left
    stopped, ready for breakpoint setup and ``run()``.
    """
    bbc.debugger.reset()
    # reset() leaves machine paused at a clean instruction boundary (cycle 7)
    for i, byte in enumerate(code):
        bbc.memory.address.bus[org + i] = byte
    bbc.cpu.pc = org


# ============================================================================
# Execution control
# ============================================================================


class TestExecutionControl:
    def test_stop_and_resume(self, bbc):
        state = bbc.debugger.stop()
        assert not state.is_running
        bbc.debugger.run()
        assert bbc.debugger.is_running

    def test_step_instruction(self, stopped_bbc):
        result = stopped_bbc.debugger.step(1)
        assert result.success
        assert result.instructions_executed == 1
        assert result.cycles_executed > 0

    def test_step_cycles(self, stopped_bbc):
        result = stopped_bbc.debugger.step_cycles(10)
        assert result.success
        assert result.cycles_executed == 10

    def test_reset(self, bbc):
        bbc.debugger.reset()
        state = bbc.debugger.get_state()
        assert not state.is_running  # reset leaves machine paused


# ============================================================================
# Breakpoints -- unconditional
# ============================================================================


class TestBreakpoints:
    def test_add_and_list(self, bbc):
        bp_id = bbc.debugger.add_breakpoint(0x1000)
        assert bp_id.id > 0
        bps = bbc.debugger.list_breakpoints()
        assert len(bps) == 1
        assert bps[0].address == 0x1000

    def test_add_returns_bound_breakpoint(self, bbc):
        """add_breakpoint returns a Breakpoint carrying its config."""
        bp = bbc.debugger.add_breakpoint(0x1000, condition="A == 0x42")
        assert isinstance(bp, Breakpoint)
        assert bp.address == 0x1000
        assert bp.condition == "A == 0x42"
        assert bp.enabled is True

    def test_breakpoint_remove_method(self, bbc):
        """Breakpoint.remove() removes it from the emulator."""
        bp = bbc.debugger.add_breakpoint(0x2000)
        assert bp.remove() is True
        assert len(bbc.debugger.list_breakpoints()) == 0

    def test_breakpoint_disable_enable_methods(self, bbc):
        """Breakpoint.disable()/enable() toggle the live breakpoint."""
        bp = bbc.debugger.add_breakpoint(0x3000)
        bp.disable()
        assert bbc.debugger.list_breakpoints()[0].enabled is False
        bp.enable()
        assert bbc.debugger.list_breakpoints()[0].enabled is True

    def test_context_manager_yields_breakpoint(self, bbc):
        """The breakpoint context manager yields a Breakpoint and cleans up."""
        with bbc.debugger.breakpoint(0x4000, condition="X == 1") as bp:
            assert isinstance(bp, Breakpoint)
            assert bp.address == 0x4000
        assert len(bbc.debugger.list_breakpoints()) == 0

    def test_remove(self, bbc):
        bp_id = bbc.debugger.add_breakpoint(0x2000)
        assert bbc.debugger.remove_breakpoint(bp_id)
        assert len(bbc.debugger.list_breakpoints()) == 0

    def test_list_includes_condition_and_hit_count(self, bbc):
        """List returns condition, stop_counterpart, and live hit count."""
        # LDX #0; .loop: INX; JMP loop
        code = bytes([0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04])
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(
            0x0402,
            condition="hits == 3",
            stop_counterpart=True,
        )

        # Run until the breakpoint fires (3rd hit)
        run_and_wait_for_stop(bbc)

        bps = bbc.debugger.list_breakpoints()
        assert len(bps) == 1
        bp = bps[0]
        assert bp.id == bp_id.id
        assert bp.address == 0x0402
        assert bp.condition == "hits == 3"
        assert bp.stop_counterpart is True
        assert bp.hit_count == 3

        bbc.debugger.clear_breakpoints()

    def test_clear(self, bbc):
        for addr in [0x1000, 0x2000, 0x3000]:
            bbc.debugger.add_breakpoint(addr)
        removed = bbc.debugger.clear_breakpoints()
        assert removed == 3
        assert len(bbc.debugger.list_breakpoints()) == 0

    def test_breakpoint_stops_at_address(self, bbc):
        """Plant 6502 code and verify breakpoint stops at the right PC."""
        # LDA #$42; STA $0500; NOP
        code = bytes([0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA])
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0405)
        event = run_and_wait_for_stop(bbc)
        assert not event.state.is_running
        regs = bbc.cpu.registers
        assert regs.a == 0x42
        bbc.debugger.remove_breakpoint(bp_id)

    def test_run_to_with_condition(self, bbc):
        """run_to accepts a breakpoint condition and removes it afterward."""
        # LDX #0; .loop: INX; JMP loop
        code = bytes([0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04])
        plant_and_run_from(bbc, code)
        state = bbc.debugger.run_to(0x0402, condition="X == 5")
        assert not state.is_running
        assert bbc.cpu.registers.x == 5
        assert len(bbc.debugger.list_breakpoints()) == 0


# ============================================================================
# Conditional breakpoints
# ============================================================================


class TestConditionalBreakpoints:
    def test_condition_on_register(self, bbc):
        """Breakpoint fires only when A == 0x42."""
        # LDX #0; .loop: LDA $040B,X; INX; CPX #4; BNE loop; NOP; .table: $10,$42,$99,$FF
        code = bytes(
            [
                0xA2,
                0x00,  # LDX #0
                0xBD,
                0x0B,
                0x04,  # LDA $040B,X
                0xE8,  # INX
                0xE0,
                0x04,  # CPX #4
                0xD0,
                0xF8,  # BNE loop (-8 -> $0402)
                0xEA,  # NOP
                0x10,
                0x42,
                0x99,
                0xFF,  # table data
            ]
        )
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402, condition="A == 0x42")
        run_and_wait_for_stop(bbc)
        assert bbc.cpu.registers.a == 0x42
        bbc.debugger.remove_breakpoint(bp_id)

    def test_condition_with_hits(self, bbc):
        """Breakpoint fires on the 3rd hit using ``hits == 3``."""
        # LDX #0; .loop: INX; JMP loop
        code = bytes([0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04])
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402, condition="hits == 3")
        run_and_wait_for_stop(bbc)
        # Breakpoint fires before the 3rd INX executes; X == 2
        assert bbc.cpu.registers.x == 2
        bbc.debugger.remove_breakpoint(bp_id)

    def test_condition_hits_modulo(self, bbc):
        """Breakpoint fires every 5th hit using ``hits % 5 == 0``."""
        # LDX #0; .loop: INX; JMP loop
        code = bytes([0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04])
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402, condition="hits % 5 == 0")
        run_and_wait_for_stop(bbc)
        # First fire at hits==5, before 5th INX executes; X == 4
        assert bbc.cpu.registers.x == 4
        bbc.debugger.remove_breakpoint(bp_id)

    def test_invalid_condition_raises(self, bbc):
        """Invalid condition string raises InvalidConditionError."""
        with pytest.raises(InvalidConditionError):
            bbc.debugger.add_breakpoint(0x1000, condition="invalid !@#")


# ============================================================================
# Watchpoints -- unconditional
# ============================================================================


class TestWatchpoints:
    def test_add_and_list(self, bbc):
        wp_id = bbc.debugger.add_watchpoint(0x1000, 0x1010, type="write")
        assert wp_id.id > 0
        wps = bbc.debugger.list_watchpoints()
        assert len(wps) == 1
        assert wps[0].start_address == 0x1000
        assert wps[0].end_address == 0x1010
        assert wps[0].type == "write"

    def test_add_returns_bound_watchpoint(self, bbc):
        """add_watchpoint returns a Watchpoint carrying its config."""
        wp = bbc.debugger.add_watchpoint(0x1000, 0x1010, type="write")
        assert isinstance(wp, Watchpoint)
        assert wp.start_address == 0x1000
        assert wp.type == "write"
        assert wp.enabled is True

    def test_watchpoint_remove_method(self, bbc):
        """Watchpoint.remove() removes it from the emulator."""
        wp = bbc.debugger.add_watchpoint(0x2000, 0x2001)
        assert wp.remove() is True
        assert len(bbc.debugger.list_watchpoints()) == 0

    def test_watchpoint_disable_enable_methods(self, bbc):
        """Watchpoint.disable()/enable() toggle the live watchpoint."""
        wp = bbc.debugger.add_watchpoint(0x3000, 0x3001)
        wp.disable()
        assert bbc.debugger.list_watchpoints()[0].enabled is False
        wp.enable()
        assert bbc.debugger.list_watchpoints()[0].enabled is True

    def test_context_manager_yields_watchpoint(self, bbc):
        """The watchpoint context manager yields a Watchpoint and cleans up."""
        with bbc.debugger.watchpoint(0x4000, 0x4001, "write") as wp:
            assert isinstance(wp, Watchpoint)
            assert wp.start_address == 0x4000
        assert len(bbc.debugger.list_watchpoints()) == 0

    def test_remove(self, bbc):
        wp_id = bbc.debugger.add_watchpoint(0x2000, 0x2001)
        assert bbc.debugger.remove_watchpoint(wp_id)
        assert len(bbc.debugger.list_watchpoints()) == 0

    def test_clear(self, bbc):
        bbc.debugger.add_watchpoint(0x1000, 0x1001)
        bbc.debugger.add_watchpoint(0x2000, 0x2001)
        removed = bbc.debugger.clear_watchpoints()
        assert removed == 2

    def test_write_watchpoint_fires(self, bbc):
        """Write watchpoint stops when code writes to monitored address."""
        # LDA #$42; STA $0500; NOP
        code = bytes([0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA])
        plant_and_run_from(bbc, code)
        wp_id = bbc.debugger.add_watchpoint(0x0500, 0x0501, type="write")
        run_and_wait_for_stop(bbc)
        assert bbc.memory.address.peek[0x0500] == 0x42
        bbc.debugger.remove_watchpoint(wp_id)

    def test_write_watchpoint_does_not_fire_on_read(self, bbc):
        """Write-only watchpoint ignores reads."""
        # LDA $0500; NOP; NOP
        code = bytes([0xAD, 0x00, 0x05, 0xEA, 0xEA])
        plant_and_run_from(bbc, code)
        bbc.memory.address.bus[0x0500] = 0xAB
        bbc.debugger.add_watchpoint(0x0500, 0x0501, type="write")
        # Use a cycle-budget breakpoint to run a deterministic number of
        # cycles. The write watchpoint should not fire (only a read occurs).
        cycle_count = bbc.debugger.get_state().cycle_count
        bp_id = bbc.debugger.add_breakpoint(
            0x0000,
            end_address=0x10000,
            condition=f"cycles >= {cycle_count + 1000}",
        )
        run_and_wait_for_stop(bbc)
        bbc.debugger.remove_breakpoint(bp_id)
        # If the write watchpoint didn't fire, we hit the cycle-budget breakpoint
        assert bbc.debugger.is_stopped


# ============================================================================
# Conditional watchpoints
# ============================================================================


class TestConditionalWatchpoints:
    def test_condition_on_register(self, bbc):
        """Watchpoint with condition A == 0x42 skips non-matching writes."""
        # LDA #$10; STA $0500; LDA #$42; STA $0500; LDA #$99; STA $0500; NOP
        code = bytes(
            [
                0xA9,
                0x10,
                0x8D,
                0x00,
                0x05,  # LDA #$10; STA $0500
                0xA9,
                0x42,
                0x8D,
                0x00,
                0x05,  # LDA #$42; STA $0500
                0xA9,
                0x99,
                0x8D,
                0x00,
                0x05,  # LDA #$99; STA $0500
                0xEA,  # NOP
            ]
        )
        plant_and_run_from(bbc, code)
        wp_id = bbc.debugger.add_watchpoint(0x0500, 0x0501, type="write", condition="A == 0x42")
        run_and_wait_for_stop(bbc)
        # Stopped on the second STA when A was 0x42
        assert bbc.memory.address.peek[0x0500] == 0x42
        bbc.debugger.remove_watchpoint(wp_id)

    def test_condition_false_never_stops(self, bbc):
        """Watchpoint with condition ``false`` records but never stops."""
        # LDA #$42; STA $0500; NOP; NOP
        code = bytes([0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA, 0xEA])
        plant_and_run_from(bbc, code)
        bbc.debugger.add_watchpoint(0x0500, 0x0501, type="write", condition="false")
        # Use a cycle-budget breakpoint to run a deterministic number of
        # cycles instead of relying on wall-clock sleep.
        cycle_count = bbc.debugger.get_state().cycle_count
        bp_id = bbc.debugger.add_breakpoint(
            0x0000,
            end_address=0x10000,
            condition=f"cycles >= {cycle_count + 1000}",
        )
        run_and_wait_for_stop(bbc)
        bbc.debugger.remove_breakpoint(bp_id)
        # The watchpoint with condition false should not have stopped execution
        assert bbc.memory.address.peek[0x0500] == 0x42  # write happened

    def test_invalid_condition_raises(self, bbc):
        """Invalid condition string raises InvalidConditionError."""
        with pytest.raises(InvalidConditionError):
            bbc.debugger.add_watchpoint(0x1000, 0x1001, condition="bad syntax !!!")


# ============================================================================
# Event stream
# ============================================================================


class TestEventStream:
    def test_stream_delivers_breakpoint_event(self, bbc):
        """Event stream delivers running and stopped events in order."""
        # LDA #$42; NOP
        code = bytes([0xA9, 0x42, 0xEA])
        plant_and_run_from(bbc, code)
        bbc.debugger.add_breakpoint(0x0402)

        stream = bbc.debugger.watch_execution_state()
        initial = next(stream)
        assert not initial.state.is_running

        bbc.debugger.run()

        events = []
        for event in stream:
            events.append(event)
            if not event.state.is_running:
                break

        assert len(events) >= 2
        # First event should be "running"
        assert events[0].state.is_running
        # Last event should be "stopped" with breakpoint reason
        assert not events[-1].state.is_running

        bbc.debugger.clear_breakpoints()

    def test_two_concurrent_subscribers(self, bbc):
        """Two concurrent event stream subscriptions each receive all events."""
        # LDA #$42; NOP
        code = bytes([0xA9, 0x42, 0xEA])
        plant_and_run_from(bbc, code)
        bbc.debugger.add_breakpoint(0x0402)

        # Open two streams
        stream1 = bbc.debugger.watch_execution_state()
        stream2 = bbc.debugger.watch_execution_state()

        # Consume initial state from both
        init1 = next(stream1)
        init2 = next(stream2)
        assert not init1.state.is_running
        assert not init2.state.is_running

        # Run
        bbc.debugger.run()

        # Both should receive running + stopped events
        events1 = []
        saw_running1 = False
        for event in stream1:
            events1.append(event)
            if event.state.is_running:
                saw_running1 = True
            elif saw_running1:
                break

        events2 = []
        saw_running2 = False
        for event in stream2:
            events2.append(event)
            if event.state.is_running:
                saw_running2 = True
            elif saw_running2:
                break

        # Both received at least running + stopped
        assert len(events1) >= 2
        assert len(events2) >= 2
        assert not events1[-1].state.is_running
        assert not events2[-1].state.is_running

        bbc.debugger.clear_breakpoints()

    def test_run_to_uses_stream(self, bbc):
        """run_to() uses the event stream, not polling."""
        # LDA #$42; STA $0500; NOP
        code = bytes([0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA])
        plant_and_run_from(bbc, code)
        state = bbc.debugger.run_to(0x0405)
        assert not state.is_running
        assert bbc.cpu.a == 0x42


# ============================================================================
# Full-range breakpoints (cycle budgets, predicates)
# ============================================================================


class TestFullRangeBreakpoints:
    def test_cycle_budget_breakpoint(self, bbc):
        """Full-range breakpoint with cycle condition stops after N cycles."""
        bbc.debugger.stop()
        start_cycles = bbc.debugger.cycle_count

        # Full-range breakpoint: stop after 1000 cycles
        target = start_cycles + 1000
        bp_id = bbc.debugger.add_breakpoint(
            0x0000,
            end_address=0x10000,
            condition=f"cycles >= {target}",
        )

        event = run_and_wait_for_stop(bbc)
        assert not event.state.is_running
        # Should have stopped at or very near the target cycle count
        final_cycles = bbc.debugger.cycle_count
        assert final_cycles >= target
        # Should not have overshot by more than one instruction (~6 cycles max)
        assert final_cycles < target + 10

        bbc.debugger.remove_breakpoint(bp_id)

    def test_memory_predicate_breakpoint(self, bbc):
        """Full-range breakpoint with memory condition."""
        # LDA #$00; .loop: CLC; ADC #$01; STA $0500; JMP loop
        code = bytes(
            [
                0xA9,
                0x00,  # LDA #$00
                0x18,  # CLC
                0x69,
                0x01,  # ADC #$01
                0x8D,
                0x00,
                0x05,  # STA $0500
                0x4C,
                0x02,
                0x04,  # JMP $0402
            ]
        )
        plant_and_run_from(bbc, code)

        # Stop when $0500 reaches $0A
        bp_id = bbc.debugger.add_breakpoint(
            0x0000,
            end_address=0x10000,
            condition="mem[0x0500] == 0x0A",
        )

        event = run_and_wait_for_stop(bbc)
        assert not event.state.is_running
        assert bbc.memory.address.peek[0x0500] == 0x0A

        bbc.debugger.remove_breakpoint(bp_id)


# ============================================================================
# Enable/disable breakpoints
# ============================================================================


class TestEnableDisableBreakpoints:
    def test_enable_disable_roundtrip(self, bbc):
        """enable/disable is reflected in list_breakpoints."""
        bp_id = bbc.debugger.add_breakpoint(0x1000)
        assert bbc.debugger.list_breakpoints()[0].enabled is True

        bbc.debugger.disable_breakpoint(bp_id)
        assert bbc.debugger.list_breakpoints()[0].enabled is False

        bbc.debugger.enable_breakpoint(bp_id)
        assert bbc.debugger.list_breakpoints()[0].enabled is True

        bbc.debugger.clear_breakpoints()

    def test_add_disabled_breakpoint(self, bbc):
        """Breakpoint created with enabled=False starts disabled."""
        bbc.debugger.add_breakpoint(0x1000, enabled=False)
        bps = bbc.debugger.list_breakpoints()
        assert len(bps) == 1
        assert bps[0].enabled is False
        bbc.debugger.clear_breakpoints()

    def test_disabled_breakpoint_does_not_fire(self, bbc):
        """Disabled breakpoint is skipped; enabled one fires."""
        # LDA #$42; STA $0500; NOP
        code = bytes([0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA])
        plant_and_run_from(bbc, code)

        # Add breakpoint at STA, disable it
        bp1_id = bbc.debugger.add_breakpoint(0x0402)
        bbc.debugger.disable_breakpoint(bp1_id)

        # Add enabled breakpoint at NOP
        bbc.debugger.add_breakpoint(0x0405)

        run_and_wait_for_stop(bbc)
        # Should have stopped at the NOP, not the STA
        assert bbc.cpu.registers.pc == 0x0405

        bbc.debugger.clear_breakpoints()

    def test_suppressed_breakpoint_reenables_on_exit(self, bbc):
        """suppressed_breakpoint context manager re-enables on exit."""
        bp_id = bbc.debugger.add_breakpoint(0x1000)
        with bbc.debugger.suppressed_breakpoint(bp_id):
            assert bbc.debugger.list_breakpoints()[0].enabled is False
        assert bbc.debugger.list_breakpoints()[0].enabled is True
        bbc.debugger.clear_breakpoints()

    def test_suppressed_breakpoint_reenables_on_exception(self, bbc):
        """suppressed_breakpoint re-enables even if body raises."""
        bp_id = bbc.debugger.add_breakpoint(0x1000)
        with pytest.raises(RuntimeError):
            with bbc.debugger.suppressed_breakpoint(bp_id):
                raise RuntimeError("test")
        assert bbc.debugger.list_breakpoints()[0].enabled is True
        bbc.debugger.clear_breakpoints()

    def test_hit_count_preserved_across_disable_enable(self, bbc):
        """Hit count survives a disable/enable cycle."""
        # LDX #0; .loop: INX; JMP loop
        code = bytes([0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04])
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402, condition="hits == 3")

        run_and_wait_for_stop(bbc)

        bps = bbc.debugger.list_breakpoints()
        assert bps[0].hit_count == 3

        # Disable and re-enable
        bbc.debugger.disable_breakpoint(bp_id)
        bbc.debugger.enable_breakpoint(bp_id)

        bps = bbc.debugger.list_breakpoints()
        assert bps[0].hit_count == 3

        bbc.debugger.clear_breakpoints()

    def test_enable_nonexistent_raises(self, bbc):
        """Enabling a non-existent breakpoint raises DebuggerError."""
        with pytest.raises(DebuggerError):
            bbc.debugger.enable_breakpoint(9999)


# ============================================================================
# Enable/disable watchpoints
# ============================================================================


class TestEnableDisableWatchpoints:
    def test_enable_disable_roundtrip(self, bbc):
        """enable/disable is reflected in list_watchpoints."""
        wp_id = bbc.debugger.add_watchpoint(0x1000, 0x1001, type="write")
        assert bbc.debugger.list_watchpoints()[0].enabled is True

        bbc.debugger.disable_watchpoint(wp_id)
        assert bbc.debugger.list_watchpoints()[0].enabled is False

        bbc.debugger.enable_watchpoint(wp_id)
        assert bbc.debugger.list_watchpoints()[0].enabled is True

        bbc.debugger.clear_watchpoints()

    def test_add_disabled_watchpoint(self, bbc):
        """Watchpoint created with enabled=False starts disabled."""
        bbc.debugger.add_watchpoint(0x1000, 0x1001, enabled=False)
        wps = bbc.debugger.list_watchpoints()
        assert len(wps) == 1
        assert wps[0].enabled is False
        bbc.debugger.clear_watchpoints()

    def test_suppressed_watchpoint_reenables_on_exit(self, bbc):
        """suppressed_watchpoint context manager re-enables on exit."""
        wp_id = bbc.debugger.add_watchpoint(0x1000, 0x1001)
        with bbc.debugger.suppressed_watchpoint(wp_id):
            assert bbc.debugger.list_watchpoints()[0].enabled is False
        assert bbc.debugger.list_watchpoints()[0].enabled is True
        bbc.debugger.clear_watchpoints()

    def test_suppressed_watchpoint_reenables_on_exception(self, bbc):
        """suppressed_watchpoint re-enables even if body raises."""
        wp_id = bbc.debugger.add_watchpoint(0x1000, 0x1001)
        with pytest.raises(RuntimeError):
            with bbc.debugger.suppressed_watchpoint(wp_id):
                raise RuntimeError("test")
        assert bbc.debugger.list_watchpoints()[0].enabled is True
        bbc.debugger.clear_watchpoints()

    def test_enable_nonexistent_raises(self, bbc):
        """Enabling a non-existent watchpoint raises DebuggerError."""
        with pytest.raises(DebuggerError):
            bbc.debugger.enable_watchpoint(9999)


# ============================================================================
# CPU state
# ============================================================================


class TestCpuState:
    def test_get_registers(self, stopped_bbc):
        regs = stopped_bbc.cpu.registers
        assert hasattr(regs, "a")
        assert hasattr(regs, "x")
        assert hasattr(regs, "y")
        assert hasattr(regs, "sp")
        assert hasattr(regs, "pc")
        assert hasattr(regs, "p")

    def test_set_registers(self, stopped_bbc):
        stopped_bbc.cpu.a = 0xAA
        stopped_bbc.cpu.x = 0xBB
        regs = stopped_bbc.cpu.registers
        assert regs.a == 0xAA
        assert regs.x == 0xBB

    def test_update_returns_new_snapshot(self, stopped_bbc):
        # update() writes atomically and returns the resulting snapshot -- the
        # returned values reflect the write with no separate read.
        regs = stopped_bbc.cpu.update(a=0x12, x=0x34, pc=0xC000)
        assert isinstance(regs, Registers)
        assert (regs.a, regs.x, regs.pc) == (0x12, 0x34, 0xC000)

    def test_update_is_partial(self, stopped_bbc):
        stopped_bbc.cpu.update(y=0x77)
        before_y = stopped_bbc.cpu.registers.y
        regs = stopped_bbc.cpu.update(a=0x66)  # y is left untouched
        assert regs.a == 0x66
        assert regs.y == before_y

    def test_setters_route_through_update(self, stopped_bbc):
        stopped_bbc.cpu.a = 0x5A
        assert stopped_bbc.cpu.registers.a == 0x5A


class TestStatusRegister:
    """The processor status register value object (pure -- no server)."""

    def test_flag_decoding(self):
        status = StatusRegister(0b1000_0011)  # N, Z and C set
        assert status.negative and status.zero and status.carry
        assert not status.overflow and not status.decimal
        assert not status.interrupt_disable and not status.break_flag

    def test_str_is_conventional_flag_string(self):
        assert str(StatusRegister(0b1000_0011)) == "Nv-bdiZC"

    def test_int_returns_raw_byte(self):
        assert int(StatusRegister(0x42)) == 0x42

    def test_flags_read_by_name(self):
        status = StatusRegister(0b1000_0011, ["C", "Z", "I", "D", "B", "", "V", "N"])
        assert status.flag("C") and status.flag("Z") and status.flag("N")
        assert status["C"] and not status["V"]
        assert "C" in status and "V" in status

    def test_aliases_absent_when_flag_names_differ(self):
        # A CPU whose flags register uses other names has no 6502 aliases.
        status = StatusRegister(0xFF, ["HALT", "OVER", "", "SIGN"])
        assert status.flag("HALT") and status.flag("SIGN")
        with pytest.raises(AttributeError):
            _ = status.carry
        with pytest.raises(AttributeError):
            _ = status.overflow

    def test_registers_status_property(self):
        # Registers is built from a descriptor; .status finds the FLAGS register.
        descriptor = debugger_pb2.CpuDescriptor(family="6502")
        for name in ("A", "X", "Y", "SP", "PC"):
            descriptor.registers.add(name=name)
        descriptor.registers.add(
            name="P",
            role=debugger_pb2.FLAGS,
            flag_names=["C", "Z", "I", "D", "B", "", "V", "N"],
        )
        regs = Registers(
            descriptor, {"A": 0, "X": 0, "Y": 0, "SP": 0, "PC": 0, "P": 0x40}
        )  # V set
        assert isinstance(regs.status, StatusRegister)
        assert regs.status.overflow and not regs.status.carry
