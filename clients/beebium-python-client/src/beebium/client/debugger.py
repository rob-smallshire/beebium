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

"""Debugger control interface for the beebium client."""

from __future__ import annotations

import time
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from dataclasses import dataclass, field

import grpc

from beebium.client._proto import debugger_pb2, debugger_pb2_grpc
from beebium.client.exceptions import DebuggerError, InvalidConditionError

DEFAULT_TIMEOUT = 30.0  # seconds

# Wall-clock allowance for running a stretch of emulated time.
#
# A fixed deadline bounds wall time, but a run is measured in emulated time,
# and how long that takes depends on the host: an unpaced Tube machine on a
# slow CI runner emulates at roughly real time, so a 25-emulated-second chunk
# sat within seconds of a 30 s deadline (#123). The allowance therefore scales
# with the emulated length: a fixed base for start-up and RPC latency, plus
# WALL_TIMEOUT_SLOWDOWN wall seconds per emulated second, so a run survives a
# host managing one fifth of the configured speed (a 25 s chunk at 1x is
# allowed 10 + 5 * 25 = 135 s). A configured speed below 1x stretches it
# further; a faster or unlimited speed is not trusted to shorten it, since the
# host may not keep up, so those are treated as 1x. It never falls below
# DEFAULT_TIMEOUT.
WALL_TIMEOUT_BASE_SECONDS = 10.0
WALL_TIMEOUT_SLOWDOWN = 5.0
_MIN_SPEED_MULTIPLIER = 0.01


def wall_timeout_for(emulated_seconds: float, speed_multiplier: float = 0.0) -> float:
    """The wall-clock deadline in seconds for running `emulated_seconds`.

    Args:
        emulated_seconds: The emulated time the run covers.
        speed_multiplier: The machine's configured speed (0.0 = unlimited).

    Returns:
        ``max(DEFAULT_TIMEOUT, base + slowdown * emulated_seconds / speed)``,
        with speeds of 1x and above, and unlimited, counted as 1x.
    """
    pace = speed_multiplier if 0.0 < speed_multiplier < 1.0 else 1.0
    pace = max(pace, _MIN_SPEED_MULTIPLIER)
    return max(
        DEFAULT_TIMEOUT,
        WALL_TIMEOUT_BASE_SECONDS + WALL_TIMEOUT_SLOWDOWN * emulated_seconds / pace,
    )


class ExecutionWaitTimeout(DebuggerError):
    """A wait for an execution-state event reached its wall-clock deadline."""


@dataclass(frozen=True)
class ExecutionState:
    """Current execution state of the emulator."""

    is_running: bool
    cycle_count: int
    halt_reason: str
    sequence: int


@dataclass(frozen=True)
class Breakpoint:
    """A breakpoint set in the emulator.

    Returned by :meth:`Debugger.add_breakpoint` and
    :meth:`Debugger.list_breakpoints`, and yielded by the ``breakpoint`` context
    manager. ``enabled`` and ``hit_count`` are a snapshot from when the object
    was obtained; the :meth:`enable`, :meth:`disable` and :meth:`remove` methods
    act on the live breakpoint. Re-query with :meth:`Debugger.list_breakpoints`
    for current state.
    """

    id: int
    address: int
    end_address: int = 0
    condition: str = ""
    stop_counterpart: bool = False
    hit_count: int = 0
    enabled: bool = True
    # The debugger this breakpoint belongs to, so it can act on itself. Excluded
    # from equality and repr so two breakpoints compare by their data alone.
    _debugger: Debugger | None = field(default=None, repr=False, compare=False)

    def remove(self) -> bool:
        """Remove this breakpoint from the emulator."""
        return self._bound().remove_breakpoint(self.id)

    def enable(self) -> None:
        """Enable this breakpoint (a no-op if already enabled)."""
        self._bound().enable_breakpoint(self.id)

    def disable(self) -> None:
        """Disable this breakpoint without removing it."""
        self._bound().disable_breakpoint(self.id)

    def _bound(self) -> Debugger:
        if self._debugger is None:
            raise DebuggerError("this Breakpoint is not bound to a debugger")
        return self._debugger


def _bp_id(breakpoint: Breakpoint | int) -> int:
    """Return the ID of a Breakpoint, or the int unchanged."""
    return breakpoint.id if isinstance(breakpoint, Breakpoint) else breakpoint


@dataclass(frozen=True)
class Watchpoint:
    """A watchpoint set in the emulator.

    Returned by :meth:`Debugger.add_watchpoint` and
    :meth:`Debugger.list_watchpoints`, and yielded by the ``watchpoint`` context
    manager. ``enabled`` and ``hit_count`` are a snapshot from when the object
    was obtained; the :meth:`enable`, :meth:`disable` and :meth:`remove` methods
    act on the live watchpoint.
    """

    id: int
    start_address: int
    end_address: int
    type: str  # "read", "write", or "both"
    condition: str = ""
    stop_counterpart: bool = False
    hit_count: int = 0
    enabled: bool = True
    _debugger: Debugger | None = field(default=None, repr=False, compare=False)

    def remove(self) -> bool:
        """Remove this watchpoint from the emulator."""
        return self._bound().remove_watchpoint(self.id)

    def enable(self) -> None:
        """Enable this watchpoint (a no-op if already enabled)."""
        self._bound().enable_watchpoint(self.id)

    def disable(self) -> None:
        """Disable this watchpoint without removing it."""
        self._bound().disable_watchpoint(self.id)

    def _bound(self) -> Debugger:
        if self._debugger is None:
            raise DebuggerError("this Watchpoint is not bound to a debugger")
        return self._debugger


def _wp_id(watchpoint: Watchpoint | int) -> int:
    """Return the ID of a Watchpoint, or the int unchanged."""
    return watchpoint.id if isinstance(watchpoint, Watchpoint) else watchpoint


@dataclass(frozen=True)
class WatchpointHitInfo:
    """Details of a watchpoint hit."""

    watchpoint_id: int
    address: int
    value: int
    is_write: bool


@dataclass(frozen=True)
class StepResult:
    """Result of a step operation."""

    success: bool
    error: str
    instructions_executed: int
    cycles_executed: int
    state: ExecutionState


@dataclass(frozen=True)
class Measurement:
    """Cycles elapsed between two PCs, from :meth:`Debugger.measure`.

    ``samples`` holds the cycle count of each pass in order. ``cycles`` is the
    first pass; ``min``, ``max`` and ``mean`` summarise all of them and equal
    ``cycles`` when there was a single pass.
    """

    samples: tuple[int, ...]

    @property
    def cycles(self) -> int:
        """Cycles elapsed in the first pass."""
        return self.samples[0]

    @property
    def min(self) -> int:
        """Fewest cycles over all passes."""
        return min(self.samples)

    @property
    def max(self) -> int:
        """Most cycles over all passes."""
        return max(self.samples)

    @property
    def mean(self) -> float:
        """Mean cycles over all passes."""
        return sum(self.samples) / len(self.samples)


@dataclass(frozen=True)
class ExecutionStateEvent:
    """An execution state change event from the server."""

    reason: int
    state: ExecutionState
    message: str


def _to_execution_state(proto_state) -> ExecutionState:
    return ExecutionState(
        is_running=proto_state.is_running,
        cycle_count=proto_state.cycle_count,
        halt_reason=proto_state.halt_reason,
        sequence=proto_state.sequence,
    )


def _to_execution_state_event(proto_event) -> ExecutionStateEvent:
    return ExecutionStateEvent(
        reason=proto_event.reason,
        state=_to_execution_state(proto_event.state),
        message=proto_event.message,
    )


class Debugger:
    """Debugger control interface.

    Provides execution control, stepping, and breakpoint management.

    Usage:
        # Stop execution
        state = bbc.debugger.stop()

        # Step through instructions
        result = bbc.debugger.step(10)

        # Run to an address (sets a temporary breakpoint)
        state = bbc.debugger.run_to(0xC000)
    """

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        """Create a debugger interface.

        Args:
            stub: The gRPC stub for the DebuggerControl service.
        """
        self._stub = stub
        self._program_counter_name: str | None = None

    def _program_counter(self) -> int:
        """The CPU's current program counter, located by its register role."""
        if self._program_counter_name is None:
            descriptor = self._stub.GetCpuDescriptor(debugger_pb2.Empty())
            names = [reg.name for reg in descriptor.registers if reg.role == debugger_pb2.PROGRAM_COUNTER]
            if not names:
                raise DebuggerError("CPU descriptor names no program counter register")
            self._program_counter_name = names[0]
        state = self._stub.GetCpuState(debugger_pb2.Empty())
        for reg in state.registers:
            if reg.name == self._program_counter_name:
                return reg.value
        raise DebuggerError(f"CPU state has no {self._program_counter_name} register")

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
        """Perform a hard (power-on-equivalent) reset and leave the CPU stopped.

        This is a cold reset, not a BREAK: it clears main RAM, resets the System
        VIA, and re-reads the keyboard links, then runs the reset sequence to the
        first instruction boundary and leaves the machine stopped (call
        ``ensure_running`` / ``run`` to resume). Because it re-reads the links, a
        runtime change via ``keyboard.set_startup_auto_boot`` / ``set_links``
        takes effect on the next ``reset``.

        This differs from the two keyboard-driven resets, which are warm (soft)
        resets that preserve RAM: ``keyboard.press_break`` (plain BREAK) and
        ``keyboard.ctrl_break`` (BREAK with Ctrl held, which MOS treats as a hard
        reset in software but which does not clear RAM here).

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

    def ensure_running(self) -> None:
        """Resume execution if not already running."""
        if not self.is_running:
            self.run()

    def ensure_stopped(self) -> None:
        """Pause execution if not already stopped."""
        if self.is_running:
            self.stop()

    @contextmanager
    def running(self) -> Iterator[None]:
        """Context manager that ensures the emulator is running on entry
        and restores the previous execution state on exit.

        Usage::

            with bbc.debugger.running():
                # emulator is executing
                ...
            # emulator restored to whatever state it was in before
        """
        was_running = self.is_running
        self.ensure_running()
        try:
            yield
        finally:
            if was_running:
                self.ensure_running()
            else:
                self.ensure_stopped()

    # Breakpoints

    def add_breakpoint(
        self,
        address: int,
        *,
        end_address: int = 0,
        condition: str = "",
        stop_counterpart: bool = False,
        enabled: bool = True,
    ) -> Breakpoint:
        """Add a breakpoint on an address range.

        The breakpoint is evaluated when the program counter reaches any address
        in ``[address, end_address)`` at an instruction boundary, and stops the
        machine when its ``condition`` (if any) is true.

        Args:
            address: Start address (inclusive).
            end_address: End address (exclusive). 0 means ``address + 1`` (a
                single address). Use ``0x10000`` for a full-range breakpoint,
                evaluated at every instruction boundary -- typically paired with
                a ``cycles`` or ``hits`` condition.
            condition: An expression evaluated each time the breakpoint is
                reached; the machine only stops when it is non-zero (true). Empty
                means unconditional. The expression language is C-like:

                - Registers: ``A``, ``X``, ``Y``, ``SP``, ``PC``, ``P`` (``P`` is
                  the full status byte).
                - Status flags, each 0 or 1: ``C``, ``Z``, ``I``, ``D``, ``V``,
                  ``N``.
                - ``cycles`` -- the total CPU cycle count.
                - ``hits`` -- how many times this breakpoint has been reached,
                  counting the current hit.
                - ``mem[expr]`` -- a side-effect-free byte read of memory (e.g.
                  ``mem[0x0070]``).
                - Literals: decimal, ``0x`` hex, ``0b`` binary, ``true``,
                  ``false``.
                - Operators: arithmetic ``+ - * / %``, bitwise ``& | ^``,
                  comparison ``== != < > <= >=``, logical ``&& || !``, and
                  parentheses for grouping.

                Examples: ``"A == 0x42"``, ``"hits == 5"``, ``"hits % 10 == 0"``,
                ``"cycles >= 100000"``, ``"X > 0 && mem[0x0070] == 0xFF"``,
                ``"N && !Z"``.

                A ``cycles`` condition is evaluated at instruction boundaries, so
                on a running CPU it stops at the first boundary at or after the
                target (never mid-instruction; it may be up to one instruction
                late). While the CPU is halted -- Break/reset held, or jammed on
                a KIL opcode -- there are no boundaries, so a *whole-address-space*
                (``address=0x0000, end_address=0x10000``) conditional breakpoint
                is evaluated every cycle and stops at exactly the target cycle.
                Unconditional or partial-range breakpoints do not fire while the
                CPU is halted. This is what makes :meth:`Beebium.run_until_or_timeout`
                complete on a halted machine.
            stop_counterpart: Also signal the counterpart processor (host or
                coprocessor) to stop -- used to coordinate breakpoints across the
                Tube.
            enabled: Whether the breakpoint is active immediately. Pass ``False``
                to add it disabled; toggle later with :meth:`enable_breakpoint`
                and :meth:`disable_breakpoint`.

        Returns:
            The new :class:`Breakpoint` (bound to this debugger, so you can call
            ``.remove()`` / ``.disable()`` / ``.enable()`` on it).

        Raises:
            InvalidConditionError: If ``condition`` is not a valid expression.
            DebuggerError: If the breakpoint cannot be added.
        """
        request = debugger_pb2.AddBreakpointRequest(
            start_address=address,
            end_address=end_address,
            condition=condition,
            stop_counterpart=stop_counterpart,
        )
        if not enabled:
            request.enabled = False
        try:
            response = self._stub.AddBreakpoint(request)
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.INVALID_ARGUMENT:
                raise InvalidConditionError(e.details()) from e
            raise
        if not response.success:
            raise DebuggerError(f"Failed to add breakpoint at ${address:04X}")
        return Breakpoint(
            id=response.id,
            address=address,
            end_address=end_address or (address + 1),
            condition=condition,
            stop_counterpart=stop_counterpart,
            hit_count=0,
            enabled=enabled,
            _debugger=self,
        )

    @contextmanager
    def breakpoint(
        self,
        address: int,
        *,
        end_address: int = 0,
        condition: str = "",
        stop_counterpart: bool = False,
        enabled: bool = True,
    ) -> Iterator[Breakpoint]:
        """Add a breakpoint for the duration of a ``with`` block.

        Adds the breakpoint on entry and removes it on exit (also on an
        exception). The parameters -- ``address``, ``end_address``, ``condition``
        (including its expression grammar), ``stop_counterpart`` and ``enabled``
        -- are exactly those of :meth:`add_breakpoint`. Yields the new
        :class:`Breakpoint`.

        Usage::

            with bbc.debugger.breakpoint(0xC000, condition="A == 0x42") as bp:
                bbc.debugger.run()
                event = bbc.debugger.wait_for_stop()
                print(bp.id)
        """
        bp = self.add_breakpoint(
            address,
            end_address=end_address,
            condition=condition,
            stop_counterpart=stop_counterpart,
            enabled=enabled,
        )
        try:
            yield bp
        finally:
            bp.remove()

    def remove_breakpoint(self, breakpoint: Breakpoint | int) -> bool:
        """Remove a breakpoint.

        Args:
            breakpoint: A :class:`Breakpoint` (as returned by add_breakpoint) or
                its ID.

        Returns:
            True if removed, False if not found.
        """
        request = debugger_pb2.RemoveBreakpointRequest(id=_bp_id(breakpoint))
        response = self._stub.RemoveBreakpoint(request)
        return response.success

    def list_breakpoints(self) -> list[Breakpoint]:
        """List all active breakpoints.

        Returns:
            List of active breakpoints.
        """
        response = self._stub.ListBreakpoints(debugger_pb2.Empty())
        return [
            Breakpoint(
                id=bp.id,
                address=bp.start_address,
                end_address=bp.end_address,
                condition=bp.condition,
                stop_counterpart=bp.stop_counterpart,
                hit_count=bp.hit_count,
                enabled=bp.enabled,
                _debugger=self,
            )
            for bp in response.breakpoints
        ]

    def enable_breakpoint(self, breakpoint: Breakpoint | int) -> None:
        """Enable a breakpoint (a Breakpoint or its ID)."""
        bp_id = _bp_id(breakpoint)
        request = debugger_pb2.EnableBreakpointRequest(id=bp_id, enabled=True)
        response = self._stub.EnableBreakpoint(request)
        if not response.success:
            raise DebuggerError(f"Breakpoint {bp_id} not found")

    def disable_breakpoint(self, breakpoint: Breakpoint | int) -> None:
        """Disable a breakpoint. Hit count and configuration are preserved."""
        bp_id = _bp_id(breakpoint)
        request = debugger_pb2.EnableBreakpointRequest(id=bp_id, enabled=False)
        response = self._stub.EnableBreakpoint(request)
        if not response.success:
            raise DebuggerError(f"Breakpoint {bp_id} not found")

    @contextmanager
    def suppressed_breakpoint(self, breakpoint: Breakpoint | int) -> Iterator[int]:
        """Temporarily disable a breakpoint, re-enabling on exit."""
        bp_id = _bp_id(breakpoint)
        self.disable_breakpoint(bp_id)
        try:
            yield bp_id
        finally:
            self.enable_breakpoint(bp_id)

    def clear_breakpoints(self) -> int:
        """Remove all breakpoints.

        Returns:
            The number of breakpoints removed.
        """
        response = self._stub.ClearBreakpoints(debugger_pb2.Empty())
        return response.count_removed

    # Watchpoints

    def add_watchpoint(
        self,
        start_address: int,
        end_address: int,
        type: str = "both",
        *,
        condition: str = "",
        stop_counterpart: bool = False,
        enabled: bool = True,
    ) -> Watchpoint:
        """Add a watchpoint on an address range.

        The watchpoint fires when the CPU reads and/or writes (per ``type``) an
        address in ``[start_address, end_address)`` and its ``condition`` is true.

        Args:
            start_address: Start of range (inclusive).
            end_address: End of range (exclusive).
            type: ``"read"``, ``"write"`` or ``"both"``.
            condition: An expression evaluated on each hit; the machine only stops
                when it is non-zero. Empty means unconditional. See
                :meth:`add_breakpoint` for the full expression grammar. Use
                ``"false"`` for a recording-only watchpoint that never stops.
            stop_counterpart: Also signal the counterpart processor to stop.
            enabled: Whether the watchpoint is active immediately; pass ``False``
                to add it disabled.

        Returns:
            The new :class:`Watchpoint` (bound to this debugger, so you can call
            ``.remove()`` / ``.disable()`` / ``.enable()`` on it).

        Raises:
            InvalidConditionError: If ``condition`` is not a valid expression.
            DebuggerError: If the watchpoint cannot be added.
        """
        type_map = {
            "read": debugger_pb2.WATCHPOINT_READ,
            "write": debugger_pb2.WATCHPOINT_WRITE,
            "both": debugger_pb2.WATCHPOINT_BOTH,
        }
        request = debugger_pb2.AddWatchpointRequest(
            start_address=start_address,
            end_address=end_address,
            type=type_map.get(type, debugger_pb2.WATCHPOINT_BOTH),
            condition=condition,
            stop_counterpart=stop_counterpart,
        )
        if not enabled:
            request.enabled = False
        try:
            response = self._stub.AddWatchpoint(request)
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.INVALID_ARGUMENT:
                raise InvalidConditionError(e.details()) from e
            raise
        if not response.success:
            raise DebuggerError(
                f"Failed to add watchpoint at ${start_address:04X}-${end_address:04X}"
            )
        return Watchpoint(
            id=response.id,
            start_address=start_address,
            end_address=end_address,
            type=type,
            condition=condition,
            stop_counterpart=stop_counterpart,
            hit_count=0,
            enabled=enabled,
            _debugger=self,
        )

    @contextmanager
    def watchpoint(
        self,
        start_address: int,
        end_address: int,
        type: str = "both",
        *,
        condition: str = "",
        stop_counterpart: bool = False,
        enabled: bool = True,
    ) -> Iterator[Watchpoint]:
        """Add a watchpoint for the duration of a ``with`` block.

        Adds it on entry and removes it on exit (also on an exception). The
        parameters are those of :meth:`add_watchpoint`. Yields the new
        :class:`Watchpoint`.

        Usage::

            with bbc.debugger.watchpoint(0xFE00, 0xFF00, "write") as wp:
                bbc.debugger.run()
                event = bbc.debugger.wait_for_stop()
        """
        wp = self.add_watchpoint(
            start_address,
            end_address,
            type,
            condition=condition,
            stop_counterpart=stop_counterpart,
            enabled=enabled,
        )
        try:
            yield wp
        finally:
            wp.remove()

    def remove_watchpoint(self, watchpoint: Watchpoint | int) -> bool:
        """Remove a watchpoint (a Watchpoint or its ID)."""
        request = debugger_pb2.RemoveWatchpointRequest(id=_wp_id(watchpoint))
        response = self._stub.RemoveWatchpoint(request)
        return response.success

    def list_watchpoints(self) -> list[Watchpoint]:
        """List all active watchpoints."""
        response = self._stub.ListWatchpoints(debugger_pb2.Empty())
        type_map = {
            debugger_pb2.WATCHPOINT_READ: "read",
            debugger_pb2.WATCHPOINT_WRITE: "write",
            debugger_pb2.WATCHPOINT_BOTH: "both",
        }
        return [
            Watchpoint(
                id=wp.id,
                start_address=wp.start_address,
                end_address=wp.end_address,
                type=type_map.get(wp.type, "both"),
                condition=wp.condition,
                stop_counterpart=wp.stop_counterpart,
                hit_count=wp.hit_count,
                enabled=wp.enabled,
                _debugger=self,
            )
            for wp in response.watchpoints
        ]

    def enable_watchpoint(self, watchpoint: Watchpoint | int) -> None:
        """Enable a watchpoint (a Watchpoint or its ID)."""
        wp_id = _wp_id(watchpoint)
        request = debugger_pb2.EnableWatchpointRequest(id=wp_id, enabled=True)
        response = self._stub.EnableWatchpoint(request)
        if not response.success:
            raise DebuggerError(f"Watchpoint {wp_id} not found")

    def disable_watchpoint(self, watchpoint: Watchpoint | int) -> None:
        """Disable a watchpoint. Hit count and configuration are preserved."""
        wp_id = _wp_id(watchpoint)
        request = debugger_pb2.EnableWatchpointRequest(id=wp_id, enabled=False)
        response = self._stub.EnableWatchpoint(request)
        if not response.success:
            raise DebuggerError(f"Watchpoint {wp_id} not found")

    @contextmanager
    def suppressed_watchpoint(self, watchpoint: Watchpoint | int) -> Iterator[int]:
        """Temporarily disable a watchpoint, re-enabling on exit."""
        wp_id = _wp_id(watchpoint)
        self.disable_watchpoint(wp_id)
        try:
            yield wp_id
        finally:
            self.enable_watchpoint(wp_id)

    def clear_watchpoints(self) -> int:
        """Remove all watchpoints. Returns the count removed."""
        response = self._stub.ClearWatchpoints(debugger_pb2.Empty())
        return response.count_removed

    # Event streaming

    def watch_execution_state(self, *, timeout: float = DEFAULT_TIMEOUT) -> Iterator[ExecutionStateEvent]:
        """Stream execution state change events from the server.

        The server sends the current state immediately, then pushes events
        whenever the execution state changes (breakpoint hit, manual stop,
        run resumed, etc.).

        Args:
            timeout: Wall-clock deadline in seconds. When expired, the gRPC
                stream raises ``DEADLINE_EXCEEDED`` which is converted to
                :class:`DebuggerError`.

        Yields:
            ExecutionStateEvent for each state change.

        Raises:
            DebuggerError: If the timeout expires before the stream ends
                naturally.
        """
        request = debugger_pb2.WatchExecutionStateRequest()
        try:
            for response in self._stub.WatchExecutionState(request, timeout=timeout):
                yield _to_execution_state_event(response)
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
                raise ExecutionWaitTimeout(
                    f"Timed out waiting for execution state event after {timeout}s"
                ) from None
            raise

    def wait_for_stop(self, *, timeout: float = DEFAULT_TIMEOUT) -> ExecutionStateEvent:
        """Wait for the machine to stop executing.

        Subscribes to the execution state stream and waits for a stopped
        event with a higher sequence number than the initial snapshot.
        This handles the case where the server coalesces running+stopped
        events when a breakpoint fires immediately.

        To *resume and then* wait for the next stop, use
        :meth:`run_and_wait_for_stop`: pairing ``ensure_running()`` with this
        method races a breakpoint that fires before this call subscribes.

        Args:
            timeout: Wall-clock deadline in seconds for the entire wait.

        Returns:
            The event that caused the machine to stop.

        Raises:
            DebuggerError: If the timeout expires or the stream ends without
                a stop event.
        """
        initial_sequence = None
        for event in self.watch_execution_state(timeout=timeout):
            if initial_sequence is None:
                initial_sequence = event.state.sequence
                continue
            if event.state.is_running:
                continue
            if event.state.sequence > initial_sequence:
                return event
        raise DebuggerError("Execution state stream ended without a stop event")

    def run_and_wait_for_stop(
        self, *, timeout: float = DEFAULT_TIMEOUT
    ) -> ExecutionStateEvent:
        """Resume execution and wait for the next stop; return the stop event.

        Race-free: it subscribes to the execution-state stream *before* resuming,
        so a breakpoint or watchpoint that fires immediately is never missed.
        Prefer this over ``ensure_running()`` followed by :meth:`wait_for_stop`,
        which can miss a stop that happens before ``wait_for_stop`` subscribes.

        Args:
            timeout: Wall-clock deadline in seconds for the wait.

        Returns:
            The event that stopped the machine.

        Raises:
            DebuggerError: If the timeout expires or the stream ends without a
                stop event.
        """
        return self._resume_and_wait_for_stop(self.ensure_running, timeout)

    def _resume_and_wait_for_stop(
        self, resume: Callable[[], None], timeout: float
    ) -> ExecutionStateEvent:
        """Subscribe, call `resume`, and return the next stop event."""
        stream = self.watch_execution_state(timeout=timeout)
        initial_sequence = next(stream).state.sequence
        resume()
        for event in stream:
            if not event.state.is_running and event.state.sequence > initial_sequence:
                return event
        raise DebuggerError("Execution state stream ended without a stop event")

    def run_to_cycle(
        self,
        target_cycles: int,
        *,
        clock_hz: int,
        wall_timeout: float,
        stop_counterpart: bool = False,
        resume: Callable[[], None] | None = None,
    ) -> ExecutionState:
        """Run until the cycle count reaches `target_cycles`, then return the stopped state.

        Installs a whole-address-space ``cycles >= N`` breakpoint (removed again
        on return), so the machine stops at the first instruction boundary at
        or after the target. The wait is bounded by `wall_timeout`; see
        :func:`wall_timeout_for` for a deadline scaled to the emulated length.

        On expiry the wait never retries: it reads the cycle count and raises a
        :class:`DebuggerError` saying which of two things happened -- the host
        is slow ("slow host: ran X of Y emulated seconds in Z s wall
        (throughput W x)") or the machine made no progress at all ("no emulated
        progress in Z s: machine stuck"). The machine is left as it was at the
        deadline; callers stop it in their own cleanup.

        Args:
            target_cycles: The cycle count to run to.
            clock_hz: The CPU clock, for reporting progress in emulated seconds.
            wall_timeout: Wall-clock deadline in seconds for the wait.
            stop_counterpart: Also stop the counterpart processor at the target.
            resume: Starts execution once the wait is subscribed; defaults to
                resuming this processor.

        Raises:
            DebuggerError: If the target is not reached within `wall_timeout`.
        """
        start_cycles = self.cycle_count
        started = time.monotonic()
        with self.breakpoint(
            0x0000,
            end_address=0x10000,
            condition=f"cycles >= {target_cycles}",
            stop_counterpart=stop_counterpart,
        ):
            try:
                return self._resume_and_wait_for_stop(
                    resume or self.ensure_running, wall_timeout
                ).state
            except ExecutionWaitTimeout:
                elapsed = time.monotonic() - started
                ran_seconds = (self.cycle_count - start_cycles) / clock_hz
                wanted_seconds = (target_cycles - start_cycles) / clock_hz
                if ran_seconds > 0:
                    raise DebuggerError(
                        f"slow host: ran {ran_seconds:.1f} of {wanted_seconds:.1f} emulated "
                        f"seconds in {elapsed:.0f} s wall "
                        f"(throughput {ran_seconds / elapsed:.2f}x)"
                    ) from None
                raise DebuggerError(
                    f"no emulated progress in {elapsed:.0f} s: machine stuck"
                ) from None

    # Run-to helper

    def run_to(
        self,
        address: int,
        *,
        end_address: int = 0,
        condition: str = "",
        stop_counterpart: bool = False,
        skip_current: bool = False,
        timeout: float = DEFAULT_TIMEOUT,
    ) -> ExecutionState:
        """Run to a temporary breakpoint, then return the stopped state.

        Sets a breakpoint (removed again on return, even on error), subscribes to
        the execution-state stream *before* starting -- so it never misses a
        breakpoint that fires immediately -- runs, and waits for the stop. The
        machine is left stopped at the breakpoint.

        By default, if the PC is already at ``address`` (or within the range up
        to ``end_address``) the breakpoint fires at once and no cycles run. Pass
        ``skip_current=True`` to run to the *next* arrival instead: the
        instruction at the current PC is stepped first, then the breakpoint is
        armed. This steps a loop one iteration at a time::

            while ...:
                bbc.debugger.run_to(labels["tick_done"], skip_current=True)

        This is debugger control -- it stops at a *PC address* in real time.
        Contrast :meth:`Beebium.run_until_or_timeout`, which fast-forwards
        emulated time until an arbitrary *predicate* (checked via peek) holds.

        Args:
            address: Start address to break at.
            end_address: End of the range (exclusive); 0 means a single address.
            condition: An expression that must be true to stop; see
                :meth:`add_breakpoint` for the grammar. Empty is unconditional.
            stop_counterpart: Also signal the counterpart processor to stop.
            skip_current: If the machine is stopped with the PC already at the
                breakpoint, step one instruction before arming it, so the run
                stops at the next arrival rather than immediately.
            timeout: Wall-clock deadline in seconds. If the breakpoint is not hit
                within this time, a :class:`DebuggerError` is raised.

        Returns:
            The execution state after hitting the breakpoint.

        Raises:
            DebuggerError: If the breakpoint cannot be set, or if the timeout
                expires before the breakpoint is hit.
        """
        # A running CPU has no "current" PC to skip; only a stopped one does.
        if skip_current and not self.get_state().is_running:
            pc = self._program_counter()
            at_breakpoint = address <= pc < end_address if end_address else pc == address
            if at_breakpoint:
                self.step(1)
        with self.breakpoint(
            address,
            end_address=end_address,
            condition=condition,
            stop_counterpart=stop_counterpart,
        ):
            return self.run_and_wait_for_stop(timeout=timeout).state

    def measure(
        self,
        start_pc: int,
        end_pc: int,
        *,
        repeat: int = 1,
        timeout: float = DEFAULT_TIMEOUT,
    ) -> Measurement:
        """Measure the CPU cycles taken to get from ``start_pc`` to ``end_pc``.

        Each pass runs to ``start_pc`` (starting at once if the machine is
        already stopped there), notes the cycle count, then runs to the next
        arrival at ``end_pc`` (see ``run_to``'s ``skip_current``). Both stops
        are at instruction boundaries, so a pass counts the cycles of every
        instruction from the one at ``start_pc`` up to, but not including, the
        one at ``end_pc``. With ``start_pc == end_pc`` a pass is one trip round
        a loop, and repeated passes are consecutive trips. The machine is left
        stopped at ``end_pc``.

        Args:
            start_pc: Address of the first instruction measured.
            end_pc: Address of the instruction that ends the measurement.
            repeat: Number of passes, each starting at the first arrival at
                ``start_pc`` from where the previous pass ended.
            timeout: Wall-clock deadline in seconds for each run to a PC.

        Returns:
            The cycles elapsed in each pass.

        Raises:
            ValueError: If ``repeat`` is less than 1.
            DebuggerError: If a PC is not reached within the timeout.
        """
        if repeat < 1:
            raise ValueError(f"repeat must be at least 1, not {repeat}")
        samples = []
        for _ in range(repeat):
            start = self.run_to(start_pc, timeout=timeout)
            end = self.run_to(end_pc, skip_current=True, timeout=timeout)
            samples.append(end.cycle_count - start.cycle_count)
        return Measurement(samples=tuple(samples))
