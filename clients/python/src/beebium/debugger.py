# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

"""Debugger control interface for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium._proto import debugger_pb2, debugger_pb2_grpc
from beebium.exceptions import DebuggerError


@dataclass(frozen=True)
class ExecutionState:
    """Current execution state of the emulator."""

    is_running: bool
    cycle_count: int
    halt_reason: str
    sequence: int


@dataclass(frozen=True)
class Breakpoint:
    """A breakpoint set in the emulator."""

    id: int
    address: int


@dataclass(frozen=True)
class StepResult:
    """Result of a step operation."""

    success: bool
    error: str
    instructions_executed: int
    cycles_executed: int
    state: ExecutionState


class Debugger:
    """Debugger control interface.

    Provides execution control, stepping, and breakpoint management.

    Usage:
        # Stop execution
        state = bbc.debugger.stop()

        # Step through instructions
        result = bbc.debugger.step(10)

        # Set breakpoint and run until hit
        bp_id = bbc.debugger.add_breakpoint(0xC000)
        state = bbc.debugger.run_until(0xC000)
    """

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        """Create a debugger interface.

        Args:
            stub: The gRPC stub for the DebuggerControl service.
        """
        self._stub = stub

    # Execution control

    def get_state(self) -> ExecutionState:
        """Get current execution state."""
        response = self._stub.GetState(debugger_pb2.Empty())
        return ExecutionState(
            is_running=response.is_running,
            cycle_count=response.cycle_count,
            halt_reason=response.halt_reason,
            sequence=response.sequence,
        )

    def run(self) -> None:
        """Resume execution.

        Raises:
            DebuggerError: If the operation fails.
        """
        response = self._stub.Run(debugger_pb2.Empty())
        if not response.success:
            raise DebuggerError(f"Failed to start execution: {response.error}")

    def stop(self) -> ExecutionState:
        """Pause execution.

        Returns:
            The execution state after stopping.
        """
        response = self._stub.Stop(debugger_pb2.Empty())
        if not response.success:
            raise DebuggerError("Failed to stop execution")
        return ExecutionState(
            is_running=response.state.is_running,
            cycle_count=response.state.cycle_count,
            halt_reason=response.state.halt_reason,
            sequence=response.state.sequence,
        )

    def reset(self) -> None:
        """Reset the machine.

        Raises:
            DebuggerError: If the reset fails.
        """
        response = self._stub.Reset(debugger_pb2.Empty())
        if not response.success:
            raise DebuggerError("Failed to reset machine")

    def step(self, count: int = 1) -> StepResult:
        """Step by instruction(s).

        Machine must be stopped first.

        Args:
            count: Number of instructions to execute.

        Returns:
            The step result including state after stepping.

        Raises:
            DebuggerError: If machine is running or step fails.
        """
        request = debugger_pb2.StepRequest(count=count)
        response = self._stub.StepInstruction(request)
        if not response.success:
            raise DebuggerError(f"Step failed: {response.error}")
        return StepResult(
            success=response.success,
            error=response.error,
            instructions_executed=response.instructions_executed,
            cycles_executed=response.cycles_executed,
            state=ExecutionState(
                is_running=response.state.is_running,
                cycle_count=response.state.cycle_count,
                halt_reason=response.state.halt_reason,
                sequence=response.state.sequence,
            ),
        )

    def step_cycles(self, count: int = 1) -> StepResult:
        """Step by CPU cycle(s).

        Machine must be stopped first.

        Args:
            count: Number of cycles to execute.

        Returns:
            The step result including state after stepping.

        Raises:
            DebuggerError: If machine is running or step fails.
        """
        request = debugger_pb2.StepRequest(count=count)
        response = self._stub.StepCycle(request)
        if not response.success:
            raise DebuggerError(f"Step cycles failed: {response.error}")
        return StepResult(
            success=response.success,
            error=response.error,
            instructions_executed=response.instructions_executed,
            cycles_executed=response.cycles_executed,
            state=ExecutionState(
                is_running=response.state.is_running,
                cycle_count=response.state.cycle_count,
                halt_reason=response.state.halt_reason,
                sequence=response.state.sequence,
            ),
        )

    # Convenience properties

    @property
    def is_running(self) -> bool:
        """True if the machine is currently running."""
        return self.get_state().is_running

    @property
    def is_stopped(self) -> bool:
        """True if the machine is currently stopped/paused."""
        return not self.is_running

    @property
    def cycle_count(self) -> int:
        """Current CPU cycle count."""
        return self.get_state().cycle_count

    # Breakpoints

    def add_breakpoint(self, address: int) -> int:
        """Add a breakpoint at the given address.

        Args:
            address: The address to break on (0x0000-0xFFFF).

        Returns:
            The breakpoint ID.

        Raises:
            DebuggerError: If the breakpoint cannot be added.
        """
        request = debugger_pb2.AddBreakpointRequest(address=address)
        response = self._stub.AddBreakpoint(request)
        if not response.success:
            raise DebuggerError(f"Failed to add breakpoint at ${address:04X}")
        return response.id

    def remove_breakpoint(self, breakpoint_id: int) -> bool:
        """Remove a breakpoint by ID.

        Args:
            breakpoint_id: The breakpoint ID returned by add_breakpoint().

        Returns:
            True if removed, False if not found.
        """
        request = debugger_pb2.RemoveBreakpointRequest(id=breakpoint_id)
        response = self._stub.RemoveBreakpoint(request)
        return response.success

    def list_breakpoints(self) -> list[Breakpoint]:
        """List all active breakpoints.

        Returns:
            List of active breakpoints.
        """
        response = self._stub.ListBreakpoints(debugger_pb2.Empty())
        return [
            Breakpoint(id=bp.id, address=bp.address) for bp in response.breakpoints
        ]

    def clear_breakpoints(self) -> int:
        """Remove all breakpoints.

        Returns:
            The number of breakpoints removed.
        """
        response = self._stub.ClearBreakpoints(debugger_pb2.Empty())
        return response.count_removed

    # Run-until helpers

    def run_until(
        self, address: int, timeout_cycles: int | None = None
    ) -> ExecutionState:
        """Run until the PC reaches the given address.

        Sets a temporary breakpoint, runs, waits for the breakpoint,
        and then clears the breakpoint.

        Args:
            address: The address to run until.
            timeout_cycles: Maximum cycles to run (not currently implemented).

        Returns:
            The execution state after hitting the address.

        Raises:
            DebuggerError: If the breakpoint cannot be set.
        """
        # Set temporary breakpoint
        bp_id = self.add_breakpoint(address)

        try:
            # Start execution
            self.run()

            # Wait for execution to stop (polling)
            import time

            while self.is_running:
                time.sleep(0.001)

            return self.get_state()
        finally:
            # Always remove the temporary breakpoint
            self.remove_breakpoint(bp_id)
