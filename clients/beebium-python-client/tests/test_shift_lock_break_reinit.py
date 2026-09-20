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

"""Characterisation of the SHIFT LOCK / lock-LED state across a Break (issue #73).

Issue #73 reports that after a crash, then Break, then typing, the machine
"loses the shift lock, showing lower case until you unshift then reshift". The
issue's own hypothesis is that the emulation is faithful -- the MOS re-inits the
keyboard lock state on Break -- and the visible defect is app-side: the macOS
host-keyboard lock mirror does not resync to the machine's reset state. These
tests exist to *verify* that server-side, not assume it.

The server exposes the lock state three independent ways, and #73 is exactly the
question of whether they can disagree:

* the logical lock latch, ``bbc.keyboard.get_lock_state()`` (exact);
* the lock LEDs, ``caps-lock-led`` / ``shift-lock-led`` from IndicatorService
  (a duty-cycle-filtered projection of the same latch, definitive at 0/255);
* the actual case of a typed letter, read back off the screen.

For letters, upper-case output means exactly one of CAPS LOCK / SHIFT LOCK is on
(caps XOR shift; both-on cancels back to lower). ``_assert_self_consistent``
pins all three signals against that relation, so a genuine server-side
divergence -- an LED or latch that disagreed with the case actually typed --
would fail it.

The finding these tests record: after every Break variant (plain, Ctrl, and a
program crash then Break) the emulator restores the power-on default (CAPS LOCK
on, SHIFT LOCK off) and all three signals stay in agreement. No server-side
divergence is reproducible, so #73 is an app-side (macOS host-mirror) bug, not an
emulation defect. If a future change breaks the Break re-init or desyncs an LED
from the latch, these turn red.
"""

from __future__ import annotations

import itertools
import time

import pytest

from beebium.client import Beebium

HOST_CLOCK_HZ = 2_000_000

# A fresh lowercase marker per screen sample, so a reading is never confused by
# an earlier sample still visible on the un-cleared screen.
_markers = ("zaz", "zbz", "zcz", "zdz", "zez", "zfz", "zgz", "zhz")
_marker_pool = itertools.cycle(_markers)


def _run_for_emulated_seconds(bbc: Beebium, seconds: float) -> None:
    clock_hz = bbc.system.clock_speed_hz or HOST_CLOCK_HZ
    target_cycles = int(seconds * clock_hz)
    start = bbc.debugger.cycle_count
    with bbc.debugger.running():
        while bbc.debugger.cycle_count - start < target_cycles:
            time.sleep(0.03)


def _boot_to_basic(bbc: Beebium) -> None:
    """Run long enough to reach the BASIC prompt, leaving the machine running."""
    _run_for_emulated_seconds(bbc, 2.0)
    bbc.debugger.ensure_running()
    bbc.expect(">", timeout=10.0)


def _settled_leds(bbc: Beebium, *, timeout: float = 5.0) -> tuple[int, int]:
    """Poll the lock LEDs in real time until both are definitive (0 or 255).

    The LED brightness is a wall-clock duty-cycle filter of the lock latch, so
    it only reads a clean 0/255 once the machine has run in real time for a
    filter window; this waits for that rather than trusting a transient.
    """
    bbc.debugger.ensure_running()
    deadline = time.monotonic() + timeout
    caps = shift = -1
    while time.monotonic() < deadline:
        caps = bbc.indicators.get("caps-lock-led")
        shift = bbc.indicators.get("shift-lock-led")
        if caps in (0, 255) and shift in (0, 255):
            break
        time.sleep(0.1)
    return caps, shift


def _typed_letter_is_upper(bbc: Beebium) -> bool:
    """Type a lowercase marker at the prompt and report whether it echoed upper.

    Reads only the current input line (the text after the last ``>``), so a
    marker left on screen by an earlier sample cannot contaminate the result,
    then abandons the line with ESCAPE so nothing is executed.
    """
    marker = next(_marker_pool)
    bbc.debugger.ensure_running()
    bbc.keyboard.type(marker)
    _run_for_emulated_seconds(bbc, 0.6)
    screen = bbc.video.screen_text().text
    lines = [line for line in screen.splitlines() if line.strip()]
    current = lines[-1] if lines else ""
    echoed = current.rsplit(">", 1)[-1]
    bbc.keyboard.press_escape()
    _run_for_emulated_seconds(bbc, 0.3)
    if marker.upper() in echoed:
        return True
    if marker in echoed:
        return False
    raise AssertionError(f"marker {marker!r} not echoed on input line {current!r} (screen: {screen!r})")


def _assert_self_consistent(bbc: Beebium, context: str) -> None:
    """The latch, both LEDs, and the typed letter case must all agree.

    This is the core #73 check: it fails if any of the three lock signals
    disagrees with the others -- e.g. the shift-lock LED lit while the letters
    typed lower, which is the divergence #73 describes.
    """
    latch = bbc.keyboard.get_lock_state()
    caps_led, shift_led = _settled_leds(bbc)
    typed_upper = _typed_letter_is_upper(bbc)

    assert caps_led == (255 if latch.caps_lock else 0), (
        f"{context}: caps-lock LED {caps_led} disagrees with latch caps={latch.caps_lock}"
    )
    assert shift_led == (255 if latch.shift_lock else 0), (
        f"{context}: shift-lock LED {shift_led} disagrees with latch shift={latch.shift_lock}"
    )
    expected_upper = latch.caps_lock ^ latch.shift_lock
    assert typed_upper == expected_upper, (
        f"{context}: typed letter {'upper' if typed_upper else 'lower'} but latch "
        f"caps={latch.caps_lock} shift={latch.shift_lock} implies "
        f"{'upper' if expected_upper else 'lower'}"
    )


def _engage_shift_lock_caps_off(bbc: Beebium) -> None:
    """Reach caps-off / shift-lock-on from the caps-on / shift-off boot default."""
    bbc.debugger.ensure_running()
    bbc.keyboard.tap_caps_lock()   # default is on -> off
    bbc.keyboard.tap_shift_lock()  # off -> on
    latch = bbc.keyboard.get_lock_state()
    assert latch.caps_lock is False and latch.shift_lock is True, latch


def _assert_reset_default(bbc: Beebium, context: str) -> None:
    """After Break the MOS restores CAPS LOCK on, SHIFT LOCK off."""
    latch = bbc.keyboard.get_lock_state()
    assert latch.caps_lock is True, f"{context}: expected caps re-init on, got {latch}"
    assert latch.shift_lock is False, f"{context}: expected shift-lock re-init off, got {latch}"


def test_shift_lock_engaged_state_is_self_consistent(bbc: Beebium) -> None:
    """Baseline: with SHIFT LOCK on and CAPS off, LED/latch/typed-case agree."""
    _boot_to_basic(bbc)
    _assert_self_consistent(bbc, "boot default")

    _engage_shift_lock_caps_off(bbc)
    _assert_self_consistent(bbc, "shift-lock engaged, caps off")


def test_plain_break_reinits_lock_state_without_divergence(bbc: Beebium) -> None:
    """The reported path's core: engage SHIFT LOCK, Break, and confirm the
    machine re-inits to the boot default with every lock signal still in
    agreement -- i.e. no server-side divergence (#73 is app-side)."""
    _boot_to_basic(bbc)
    _engage_shift_lock_caps_off(bbc)
    _assert_self_consistent(bbc, "before break")

    bbc.debugger.ensure_running()
    bbc.keyboard.press_break()
    _run_for_emulated_seconds(bbc, 1.0)
    bbc.debugger.ensure_running()
    bbc.expect(">", timeout=10.0)

    _assert_reset_default(bbc, "after plain break")
    _assert_self_consistent(bbc, "after plain break")


def test_ctrl_break_reinits_lock_state_without_divergence(bbc: Beebium) -> None:
    """Ctrl-Break (hard reset) likewise re-inits the locks self-consistently."""
    _boot_to_basic(bbc)
    _engage_shift_lock_caps_off(bbc)
    _assert_self_consistent(bbc, "before ctrl-break")

    bbc.debugger.ensure_running()
    bbc.keyboard.ctrl_break()
    _run_for_emulated_seconds(bbc, 1.5)
    bbc.debugger.ensure_running()
    bbc.expect(">", timeout=10.0)

    _assert_reset_default(bbc, "after ctrl-break")
    _assert_self_consistent(bbc, "after ctrl-break")


def test_crash_then_break_reinits_lock_state_without_divergence(bbc: Beebium) -> None:
    """The literal #73 sequence: a program crash, then Break, then type.

    The crash is induced by executing a BRK (a zero byte) in user RAM, which
    drops back to the BASIC prompt exactly as the Fingerprint crash (#72) does.
    It is typed via text_input() so SHIFT LOCK does not mangle the command,
    while SHIFT LOCK remains the engaged state carried into the Break.
    """
    _boot_to_basic(bbc)
    _engage_shift_lock_caps_off(bbc)
    _assert_self_consistent(bbc, "before crash")

    bbc.debugger.ensure_running()
    with bbc.keyboard.text_input():
        bbc.keyboard.type("?&2000=0:CALL &2000\r")
    _run_for_emulated_seconds(bbc, 1.0)
    bbc.debugger.ensure_running()
    bbc.expect(">", timeout=10.0)  # crashed back to the prompt
    # Shift lock survives a crash-to-prompt (no reset happened yet).
    assert bbc.keyboard.get_lock_state().shift_lock is True

    bbc.keyboard.press_break()
    _run_for_emulated_seconds(bbc, 1.0)
    bbc.debugger.ensure_running()
    bbc.expect(">", timeout=10.0)

    _assert_reset_default(bbc, "after crash then break")
    _assert_self_consistent(bbc, "after crash then break")
