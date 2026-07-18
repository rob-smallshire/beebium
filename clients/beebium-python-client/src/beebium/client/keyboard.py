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

"""Keyboard input interface for the beebium client."""

from __future__ import annotations

import contextlib
import time
from collections.abc import Iterator
from dataclasses import dataclass
from typing import TYPE_CHECKING, NamedTuple

from beebium.client._proto import keyboard_pb2, keyboard_pb2_grpc
from beebium.client.keyboard_map import (
    CTRL_KEY,
    SHIFT_KEY,
    char_to_matrix,
)

if TYPE_CHECKING:
    from beebium.client import Beebium


# CAPS LOCK and SHIFT LOCK live on row 4/5 column 0 of the keyboard matrix.
# These are not "modifier" keys -- a tap toggles a state held by the MOS.
CAPS_LOCK_KEY = (4, 0)
SHIFT_LOCK_KEY = (5, 0)

# Cycles the lock key is held before being released. The MOS scans the
# keyboard matrix from a ~10ms timer IRQ and debounces transitions, so
# 50ms held is comfortably above the minimum to register a clean press.
_LOCK_TAP_HOLD_CYCLES = 100_000

# Bound on emulator cycles spent waiting for the logical lock latch to flip
# after the key is released. The latch is updated by the MOS keyboard scan
# (~100Hz, every ~10ms emulated), so the flip lands within a scan period or
# two. 1,000,000 cycles == 500ms emulated -- tens of scan periods, so a
# timeout indicates a real fault. Because the bound is in emulated cycles it
# is independent of wall-clock emulation speed.
_LOCK_TAP_TIMEOUT_CYCLES = 1_000_000

# Cycles between logical-latch reads while polling for the flip. ~2.5ms
# emulated oversamples the ~10ms MOS scan period a few times over.
_LOCK_TAP_POLL_CHUNK_CYCLES = 5_000

# Cycles to spin after the LED edge is observed, giving MOS time to
# scan the key release and complete its keyboard-handler routine.
# Without this settle, a back-to-back tap of the same key can look like
# a continuous press and fail to re-trigger the toggle.
_LOCK_TAP_RELEASE_SETTLE_CYCLES = 100_000


@dataclass
class KeyboardState:
    """Current state of the keyboard matrix."""

    pressed_rows: list[int]  # 10 rows of pressed key bitmaps

    def is_pressed(self, row: int, column: int) -> bool:
        """Check if a specific key is pressed.

        Args:
            row: The keyboard matrix row (0-9).
            column: The keyboard matrix column (0-9).

        Returns:
            True if the key is pressed.
        """
        if 0 <= row < len(self.pressed_rows):
            return bool(self.pressed_rows[row] & (1 << column))
        return False


class LockState(NamedTuple):
    """Logical CAPS LOCK / SHIFT LOCK state, as held by the MOS.

    These are the exact latch bits, not the duty-cycle-filtered lock-LED
    brightness -- so they are correct at any emulation speed.
    """

    caps_lock: bool
    shift_lock: bool


class TypingStatus(NamedTuple):
    """State of the server's type-ahead queue.

    A NamedTuple so it still unpacks as ``(idle, pending, queued)``.
    """

    idle: bool
    pending_characters: int
    strings_queued: int


class Keyboard:
    """Keyboard input interface.

    Provides both low-level matrix access and high-level text input.

    Usage:
        # Type text (cycle-paced, use \\r for RETURN)
        bbc.keyboard.type("PRINT 42\\r")

        # Press specific keys
        bbc.keyboard.key_down('A')
        bbc.keyboard.key_up('A')

        # Matrix-level access
        bbc.keyboard.matrix_down(row=4, column=1)  # 'A' key
    """

    def __init__(
        self,
        stub: keyboard_pb2_grpc.KeyboardServiceStub,
        client: Beebium | None = None,
    ):
        """Create a keyboard interface.

        Args:
            stub: The gRPC stub for the KeyboardService.
            client: Optional reference to the parent Beebium client. Required
                by ``text_input()`` and ``tap_caps_lock()``/``tap_shift_lock()``,
                which read the addressable-latch LED state and step the
                emulator between key down and key up so the MOS can scan
                the matrix.
        """
        self._stub = stub
        self._client = client
        self._pressed_keys: set[tuple[str, bool]] = set()

    # High-level text input (cycle-paced via server-side TypeAheadQueue)

    def type(self, text: str, *, translate: bool = True) -> int:
        """Type a string of text, paced reliably by the server.

        The text is enqueued and typed character-by-character as the
        emulator runs. Use ``\\r`` for RETURN, ``\\x1b`` for ESCAPE,
        ``\\x7f`` for DELETE, and ``\\t`` for TAB.

        Returns immediately; the emulator must be running for the
        keystrokes to be processed. The server paces keystrokes to be
        reliably registered by the MOS keyboard scan, so there is no
        timing knob to tune. For deliberate custom timing, drive
        :meth:`key_down` / :meth:`key_up` directly.

        By default the server translates host text conventions into what
        the BBC keyboard can type: every line-ending convention becomes a
        single ``\\r``, typographic punctuation folds to ASCII, and the
        SAA5050 teletext glyphs map back to the codes that produce them.
        So ``\\n`` and ``\\r\\n`` both mean RETURN once, not twice.

        Pass ``translate=False`` to type the text exactly as given. Note
        that ``\\r\\n`` then presses RETURN twice, because both CR and LF
        are the RETURN key.

        Args:
            text: The text to type.
            translate: Whether the server should translate host text
                conventions. Defaults to True.

        Returns:
            Total pending characters in queue after enqueue.

        Raises:
            ValueError: If text contains characters that cannot be typed
                on the BBC keyboard. The message names the first such
                character.
        """
        request = keyboard_pb2.TypeQuicklyRequest(text=text, translate=translate)
        response = self._stub.TypeQuickly(request)
        if not response.accepted:
            raise ValueError(f"Text rejected: {response.error}")
        return response.pending_characters

    def press_return(self) -> None:
        """Enqueue a RETURN keypress."""
        self.type("\r")

    def press_escape(self) -> None:
        """Enqueue an ESCAPE keypress."""
        self.type("\x1b")

    def press_delete(self) -> None:
        """Enqueue a DELETE keypress."""
        self.type("\x7f")

    def press_space(self) -> None:
        """Enqueue a SPACE keypress."""
        self.type(" ")

    # Character-level input

    def key_down(self, char: str) -> bool:
        """Press a key for the given character.

        Automatically handles SHIFT for uppercase and shifted symbols.

        Args:
            char: The character to press.

        Returns:
            True if the character is mapped, False otherwise.
        """
        mapping = char_to_matrix(char)
        if mapping is None:
            return False

        row, column, needs_shift = mapping

        if needs_shift:
            self.matrix_down(*SHIFT_KEY)
        self.matrix_down(row, column)

        self._pressed_keys.add((char, needs_shift))
        return True

    def key_up(self, char: str) -> bool:
        """Release a key for the given character.

        Args:
            char: The character to release.

        Returns:
            True if the character is mapped, False otherwise.
        """
        mapping = char_to_matrix(char)
        if mapping is None:
            return False

        row, column, needs_shift = mapping

        self.matrix_up(row, column)

        # Only release shift if no other shifted keys are pressed
        if needs_shift:
            self._pressed_keys.discard((char, True))
            if not any(shifted for _, shifted in self._pressed_keys):
                self.matrix_up(*SHIFT_KEY)
        else:
            self._pressed_keys.discard((char, False))

        return True

    def release_all(self) -> None:
        """Release all currently pressed keys."""
        for char, _ in list(self._pressed_keys):
            self.key_up(char)

    # =========================================================================
    # Lock keys (CAPS LOCK and SHIFT LOCK)
    # =========================================================================
    # CAPS LOCK and SHIFT LOCK toggle a sticky state held by the MOS. A "tap"
    # is a complete key-down/key-up cycle with enough emulated time between
    # for the MOS keyboard scan to detect both edges. The current state can
    # be read from the addressable-latch LED bits.

    def get_lock_state(self) -> LockState:
        """Return the logical CAPS LOCK and SHIFT LOCK state.

        Reads the MOS-maintained lock latch directly, so the result is
        exact and correct at any emulation speed -- unlike the lock-LED
        brightness from :class:`Indicators`, which is duty-cycle filtered
        over a wall-clock window for display and only settles to 0/255 in
        real time.
        """
        response = self._stub.GetLockState(keyboard_pb2.GetLockStateRequest())
        return LockState(
            caps_lock=response.caps_lock_on,
            shift_lock=response.shift_lock_on,
        )

    def tap_caps_lock(self, *, timeout_cycles: int = _LOCK_TAP_TIMEOUT_CYCLES) -> None:
        """Tap the CAPS LOCK key, toggling the MOS caps-lock state.

        Polls the logical lock latch (via :meth:`get_lock_state`) until it
        has flipped, confirming that the MOS keyboard scan has registered
        the toggle before returning.

        The emulator must be running on entry -- the MOS keyboard scan
        is interrupt-driven and only progresses while CPU cycles are
        being executed.

        Args:
            timeout_cycles: Maximum emulator cycles to wait for the latch
                to flip. Default 1,000,000 cycles (500ms emulated).

        Raises:
            RuntimeError: If the emulator is not running, or if the latch
                fails to flip within ``timeout_cycles``.
        """
        self._tap_lock_key(
            *CAPS_LOCK_KEY,
            which="CAPS LOCK",
            timeout_cycles=timeout_cycles,
        )

    def tap_shift_lock(self, *, timeout_cycles: int = _LOCK_TAP_TIMEOUT_CYCLES) -> None:
        """Tap the SHIFT LOCK key, toggling the MOS shift-lock state.

        See :meth:`tap_caps_lock` for the polling, running-mode
        precondition, and timeout semantics.
        """
        self._tap_lock_key(
            *SHIFT_LOCK_KEY,
            which="SHIFT LOCK",
            timeout_cycles=timeout_cycles,
        )

    def _lock_is_on(self, which: str) -> bool:
        """Return the logical on/off state of one lock from the latch."""
        state = self.get_lock_state()
        return state.caps_lock if which == "CAPS LOCK" else state.shift_lock

    def _tap_lock_key(
        self,
        row: int,
        column: int,
        *,
        which: str,
        timeout_cycles: int,
    ) -> None:
        client = self._require_client("tap")
        debugger = client.debugger
        if not debugger.is_running:
            raise RuntimeError(
                "Lock key tap requires a running emulator: the MOS "
                "keyboard scan is interrupt-driven and only progresses "
                "while the CPU is executing. Call bbc.debugger.run() "
                "before tapping."
            )

        # The logical latch is exact and immediate, so the starting state is
        # read in a single query -- no waiting for a filter to settle.
        initial = self._lock_is_on(which)
        self.matrix_down(row, column)
        self._wait_emulated_cycles(_LOCK_TAP_HOLD_CYCLES)
        self.matrix_up(row, column)

        start_cycles = debugger.cycle_count
        while debugger.cycle_count - start_cycles < timeout_cycles:
            if self._lock_is_on(which) != initial:
                # MOS may still be inside its keyboard-handler IRQ
                # routine, processing the press. Wait for the release
                # to be scanned and the key-debounce window to clear
                # before returning, otherwise an immediately-following
                # tap of the same key looks like a continuous press
                # and never re-triggers the toggle.
                self._wait_emulated_cycles(_LOCK_TAP_RELEASE_SETTLE_CYCLES)
                return
            self._wait_emulated_cycles(_LOCK_TAP_POLL_CHUNK_CYCLES)
        # Final check after the budget elapsed
        if self._lock_is_on(which) != initial:
            self._wait_emulated_cycles(_LOCK_TAP_RELEASE_SETTLE_CYCLES)
            return
        raise RuntimeError(
            f"Lock key tap not registered: the {which} latch did not flip "
            f"within {timeout_cycles} emulator cycles. Is the emulator "
            f"running fast enough to advance the MOS keyboard scan?"
        )

    def _wait_emulated_cycles(self, cycles: int) -> None:
        """Block until the running emulator has advanced ``cycles`` cycles.

        Assumes the emulator is running; the call sites that require
        cycle-paced waits already enforce that precondition.
        """
        client = self._require_client("tap")
        target = client.debugger.cycle_count + cycles
        # 2 MHz host clock; sleep approximately the equivalent wall-clock
        # time, then top up by polling. Small enough not to overshoot but
        # large enough to keep gRPC chatter low.
        sleep_seconds = max(0.005, cycles / 2_000_000 / 2)
        while client.debugger.cycle_count < target:
            time.sleep(sleep_seconds)

    @contextlib.contextmanager
    def text_input(self) -> Iterator[None]:
        """Context manager that disables CAPS LOCK and SHIFT LOCK for the
        duration of the block, restoring the original state on exit.

        The BBC Micro boots with CAPS LOCK on, so without this guard
        ``bbc.keyboard.type("hello")`` echoes as "HELLO" on screen, and
        ``bbc.keyboard.type("HELLO")`` (which presses SHIFT for each
        letter) echoes as "hello". Wrapping the typing in this context
        manager makes the screen match the source string.

        The emulator must be running on entry, both so the entry tap
        registers and so any queued typing inside the block can drain.
        On exit the queue is drained via :meth:`wait_for_typing` before
        the locks are restored, so any pending characters are typed under
        the same lock state that produced them.

        Usage::

            bbc.debugger.run()
            with bbc.keyboard.text_input():
                bbc.keyboard.type("Hello, World!\\r")
                bbc.run_until_or_timeout(predicate, emulated_seconds=2.0)

        Raises:
            RuntimeError: If the emulator is not running on entry.
        """
        client = self._require_client("text_input()")
        if not client.debugger.is_running:
            raise RuntimeError(
                "text_input() requires a running emulator. Call bbc.debugger.run() before entering the with-block."
            )

        # One latch query gives the exact starting state at any speed.
        initial = self.get_lock_state()

        if initial.caps_lock:
            self.tap_caps_lock()
        if initial.shift_lock:
            self.tap_shift_lock()

        try:
            yield
        finally:
            # Drain pending typing under the disabled-locks state so the
            # restoring tap doesn't change the case of in-flight characters.
            self.wait_for_typing()

            current = self.get_lock_state()
            if current.caps_lock != initial.caps_lock:
                self.tap_caps_lock()
            if current.shift_lock != initial.shift_lock:
                self.tap_shift_lock()

    def _require_client(self, feature: str) -> Beebium:
        if self._client is None:
            raise RuntimeError(
                f"{feature} requires Keyboard to be constructed with a client "
                "reference (use bbc.keyboard rather than constructing Keyboard "
                "directly)."
            )
        return self._client

    # Modifier keys

    def shift_down(self) -> None:
        """Press the SHIFT key."""
        self.matrix_down(*SHIFT_KEY)

    def shift_up(self) -> None:
        """Release the SHIFT key."""
        self.matrix_up(*SHIFT_KEY)

    def ctrl_down(self) -> None:
        """Press the CTRL key."""
        self.matrix_down(*CTRL_KEY)

    def ctrl_up(self) -> None:
        """Release the CTRL key."""
        self.matrix_up(*CTRL_KEY)

    # Matrix-level input

    def matrix_down(self, row: int, column: int) -> bool:
        """Press a key by BBC keyboard matrix position.

        Args:
            row: The keyboard matrix row (0-9).
            column: The keyboard matrix column (0-9).

        Returns:
            True if accepted by server.
        """
        request = keyboard_pb2.KeyRequest(ik_number=(row << 4) | column)
        response = self._stub.KeyDown(request)
        return response.accepted

    def matrix_up(self, row: int, column: int) -> bool:
        """Release a key by BBC keyboard matrix position.

        Args:
            row: The keyboard matrix row (0-9).
            column: The keyboard matrix column (0-9).

        Returns:
            True if accepted by server.
        """
        request = keyboard_pb2.KeyRequest(ik_number=(row << 4) | column)
        response = self._stub.KeyUp(request)
        return response.accepted

    def get_state(self) -> KeyboardState:
        """Get current keyboard state (pressed keys bitmap).

        Returns:
            The current keyboard state.
        """
        request = keyboard_pb2.GetStateRequest()
        response = self._stub.GetState(request)
        return KeyboardState(pressed_rows=list(response.pressed_rows))

    # Type-ahead status

    def typing_status(self) -> TypingStatus:
        """Get type-ahead queue status.

        For waiting on a paste, prefer :meth:`wait_until_typing_complete`,
        which the server drives, over polling this.

        Returns:
            TypingStatus, which still unpacks as
            (idle, pending_characters, strings_queued).
        """
        request = keyboard_pb2.GetTypingStatusRequest()
        response = self._stub.GetTypingStatus(request)
        return TypingStatus(
            idle=response.idle,
            pending_characters=response.pending_characters,
            strings_queued=response.strings_queued,
        )

    def watch_typing_status(self, *, min_interval_ms: int = 0) -> Iterator[TypingStatus]:
        """Stream type-ahead status, pushed by the server on change.

        Yields an initial snapshot immediately, then a fresh one whenever the
        queue changes -- a character consumed, a string started or finished,
        text enqueued, or the queue cleared. The stream stays open until the
        iterator is closed or the connection drops.

        Prefer this over polling :meth:`typing_status`. The server is the only
        party that knows when the queue drains; polling forces the caller to
        infer it, and any inference needs a timeout that is wrong for some
        paste. Typing throughput depends on how fast the host can emulate and
        on the text itself, since every capital costs an extra SHIFT press.

        Args:
            min_interval_ms: Minimum interval between change checks (0 = server
                default of 20ms).
        """
        request = keyboard_pb2.WatchTypingStatusRequest(min_interval_ms=min_interval_ms)
        for response in self._stub.WatchTypingStatus(request):
            yield TypingStatus(
                idle=response.idle,
                pending_characters=response.pending_characters,
                strings_queued=response.strings_queued,
            )

    def wait_until_typing_complete(self, *, min_interval_ms: int = 0) -> None:
        """Block until the server reports nothing left to type.

        Returns as soon as the queue is empty, including immediately if it
        already was. Also returns if the stream ends for any other reason -- a
        cleared queue, a dropped connection, a stopped server -- because in
        every case there is nothing further to wait for.

        There is deliberately no timeout. Use the underlying channel's
        cancellation, or :meth:`clear_typing` from another thread, to stop
        waiting early.
        """
        for status in self.watch_typing_status(min_interval_ms=min_interval_ms):
            if status.idle:
                return

    def clear_typing(self) -> int:
        """Clear the type-ahead queue.

        Returns:
            Number of characters that were cleared.
        """
        request = keyboard_pb2.ClearTypingRequest()
        response = self._stub.ClearTyping(request)
        return response.characters_cleared

    def wait_for_typing(self, poll_interval: float = 0.01) -> None:
        """Block until type-ahead queue is empty.

        Args:
            poll_interval: Seconds between status checks.
        """
        while True:
            idle, _, _ = self.typing_status()
            if idle:
                return
            time.sleep(poll_interval)

    # Character-to-key mapping (server-side canonical mapping)

    def get_key_mapping(self, char: str) -> tuple[int, bool, str] | None:
        """Get the BBC key mapping for a character from the server.

        This queries the canonical mapping table maintained by the server.

        Args:
            char: A single character.

        Returns:
            Tuple of (ik_number, needs_shift, name), or None if unmappable.
        """
        request = keyboard_pb2.GetKeyMappingRequest(character=char)
        entry = self._stub.GetKeyMapping(request)
        if not entry.found:
            return None
        return (entry.ik_number, entry.needs_shift, entry.name)

    def get_all_mappings(self) -> list[tuple[str, int, bool, str]]:
        """Get the complete key mapping table from the server.

        Returns:
            List of (character, ik_number, needs_shift, name) tuples.
            Character is empty string for named-only keys (function keys, etc.).
        """
        request = keyboard_pb2.GetAllKeyMappingsRequest()
        response = self._stub.GetAllKeyMappings(request)
        return [(entry.character, entry.ik_number, entry.needs_shift, entry.name) for entry in response.mappings]

    def is_typeable_on_server(self, text: str) -> bool:
        """Check if all characters in text can be typed (using server mapping).

        Args:
            text: The text to check.

        Returns:
            True if all characters are typeable according to server.
        """
        return all(self.get_key_mapping(c) is not None for c in text)

    # =========================================================================
    # Break key operations
    # =========================================================================
    # The Break key is NOT part of the keyboard matrix. It is directly
    # connected to the reset circuit (IC16 NE555 timer) via pin 4.
    #
    # While Break is held: CPU is halted (reset line held low)
    # On Break release: Soft reset sequence begins
    #
    # Note: Hardware always does a soft reset. MOS checks if Ctrl is held
    # during its reset sequence to decide between warm/cold reset behavior.

    def break_down(self) -> bool:
        """Hold the Break key (halt the CPU).

        This asserts the reset line, halting the CPU. The CPU remains
        halted until break_up() is called.

        Returns:
            True if successful.
        """
        request = keyboard_pb2.BreakDownRequest()
        response = self._stub.BreakDown(request)
        return response.success

    def break_up(self) -> bool:
        """Release the Break key (begin soft reset sequence).

        This releases the reset line. The CPU will execute the 7-cycle
        reset sequence and then begin execution from the reset vector.

        MOS checks if Ctrl is held at this moment to determine reset type:
        - If Ctrl pressed: MOS performs "hard reset" (clears VIA config)
        - If Ctrl not pressed: MOS performs "warm reset" (preserves state)

        Returns:
            True if successful.
        """
        request = keyboard_pb2.BreakUpRequest()
        response = self._stub.BreakUp(request)
        return response.success

    def is_break_held(self) -> bool:
        """Check if the Break key is currently held.

        Returns:
            True if Break is currently held (CPU halted).
        """
        request = keyboard_pb2.GetBreakStateRequest()
        response = self._stub.GetBreakState(request)
        return response.is_held

    def press_break(self, hold_time: float = 0.02) -> bool:
        """Press and release the Break key (perform soft reset).

        This is a convenience method that calls break_down(), waits
        briefly, then calls break_up().

        Args:
            hold_time: How long to hold Break (seconds).

        Returns:
            True if both operations succeeded.
        """
        down_ok = self.break_down()
        time.sleep(hold_time)
        up_ok = self.break_up()
        return down_ok and up_ok

    def ctrl_break(self, hold_time: float = 0.02) -> bool:
        """Press Ctrl-Break (trigger MOS hard reset).

        This holds Ctrl while pressing Break, which causes MOS to
        detect a "hard reset" and perform full reinitialization.

        Note: The hardware always performs a soft reset. MOS checks
        the keyboard matrix during its reset routine and clears the
        VIA configuration if Ctrl is held, simulating a hard reset.

        Args:
            hold_time: How long to hold Break (seconds).

        Returns:
            True if all operations succeeded.
        """
        self.ctrl_down()
        time.sleep(0.01)  # Brief delay to ensure Ctrl is registered
        down_ok = self.break_down()
        time.sleep(hold_time)
        up_ok = self.break_up()
        time.sleep(0.01)  # Brief delay before releasing Ctrl
        self.ctrl_up()
        return down_ok and up_ok

    # =========================================================================
    # Keyboard links (DIP switches)
    # =========================================================================
    # The BBC Micro has 8 keyboard links (row 0, columns 2-9) that form a
    # startup options byte. Active-low: bit SET = link broken (open),
    # bit CLEAR = link made.
    #
    # Bit layout:
    #   Bits 0-2: Screen mode (XOR'd with 7 by MOS, so 0x07 = Mode 7)
    #   Bit 3: SHIFT-BREAK action (1 = normal, 0 = reversed/auto-boot)
    #   Bits 4-7: ROM-dependent (disc timing, filing system, etc.)
    #
    # Default value 0xFF (all links broken) = Mode 7, normal SHIFT-BREAK.

    def get_links(self) -> int:
        """Get the raw keyboard links byte (8 bits).

        Returns:
            The current 8-bit links value (0-255).
        """
        request = keyboard_pb2.GetLinksRequest()
        response = self._stub.GetLinks(request)
        return response.value

    def set_links(self, value: int) -> bool:
        """Set the raw keyboard links byte (8 bits).

        Args:
            value: The 8-bit links value (0-255).

        Returns:
            True if successful.

        Raises:
            ValueError: If value is not in range 0-255.
        """
        if not 0 <= value <= 255:
            raise ValueError("Links value must be 0-255")
        request = keyboard_pb2.SetLinksRequest(value=value)
        response = self._stub.SetLinks(request)
        if not response.success:
            raise ValueError(response.error)
        return True

    def get_startup_screen_mode(self) -> int:
        """Get the startup screen mode (0-7).

        Returns:
            The configured startup screen mode.
        """
        request = keyboard_pb2.GetStartupScreenModeRequest()
        response = self._stub.GetStartupScreenMode(request)
        return response.mode

    def set_startup_screen_mode(self, mode: int) -> bool:
        """Set the startup screen mode (0-7).

        Args:
            mode: The screen mode (0-7).

        Returns:
            True if successful.

        Raises:
            ValueError: If mode is not in range 0-7.
        """
        if not 0 <= mode <= 7:
            raise ValueError("Mode must be 0-7")
        request = keyboard_pb2.SetStartupScreenModeRequest(mode=mode)
        response = self._stub.SetStartupScreenMode(request)
        if not response.success:
            raise ValueError(response.error)
        return True

    def get_startup_auto_boot(self) -> bool:
        """Check if auto-boot on SHIFT-BREAK is enabled.

        Returns:
            True if SHIFT-BREAK triggers auto-boot.
        """
        request = keyboard_pb2.GetStartupAutoBootRequest()
        response = self._stub.GetStartupAutoBoot(request)
        return response.enabled

    def set_startup_auto_boot(self, enabled: bool) -> bool:
        """Enable or disable auto-boot on SHIFT-BREAK.

        When enabled, SHIFT-BREAK causes MOS to load and run !Boot.

        Args:
            enabled: True to enable auto-boot.

        Returns:
            True if successful.
        """
        request = keyboard_pb2.SetStartupAutoBootRequest(enabled=enabled)
        response = self._stub.SetStartupAutoBoot(request)
        return response.success
