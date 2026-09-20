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

"""Stress tests for repeated Break-key resets.

Issue #27 (no Tube) and issue #39 (with Tube) report occasional segfaults
when the user presses Break ~1 Hz: the server crashes after as few as 1-5
attempts (Tube) or up to ~28 attempts (no Tube). These tests cycle Break
many times and assert the server stays alive.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.exceptions import BeebiumError, ServerNotFoundError
from beebium.client.screen import dump_screen, read_mode7_screen, screen_contains

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Break stress test is timing-sensitive on CI runners",
)


BREAK_HOLD_SECONDS = 0.05
BREAK_INTERVAL_EMULATED_SECONDS = 1.0
BREAK_CYCLES = 20


def _find_romram_server() -> Path | None:
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    for candidate in [
        repo_root / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]:
        if candidate.exists():
            return candidate
    return None


def _server_alive(bbc: Beebium) -> bool:
    """True if the underlying server process is still running."""
    return bbc._server is not None and bbc._server.is_running


def _press_break(bbc: Beebium) -> None:
    """Press and release the Break key via the keyboard service."""
    assert bbc.keyboard.break_down(), "BreakDown RPC failed"
    time.sleep(BREAK_HOLD_SECONDS)
    assert bbc.keyboard.break_up(), "BreakUp RPC failed"


def _diagnostics(bbc: Beebium, attempt: int) -> str:
    """Best-effort diagnostic snapshot for the failure message."""
    parts = [f"Crashed/failed on Break attempt {attempt}/{BREAK_CYCLES}"]
    if not _server_alive(bbc):
        parts.append("Server process is no longer running.")
        if bbc._server is not None and bbc._server._process is not None:
            rc = bbc._server._process.poll()
            parts.append(f"  exit code: {rc}")
            try:
                _, stderr = bbc._server._process.communicate(timeout=0.5)
                if stderr:
                    parts.append("  stderr (last 2000 bytes):")
                    parts.append(stderr[-2000:].decode("utf-8", errors="replace"))
            except Exception:
                pass
        return "\n".join(parts)

    # Server still alive but the test predicate failed -- collect state.
    try:
        regs = bbc.cpu.registers
        parts.append(f"Host CPU: PC=${regs.pc:04X} A=${regs.a:02X} X=${regs.x:02X} Y=${regs.y:02X} SP=${regs.sp:02X}")
    except (BeebiumError, grpc.RpcError) as e:
        parts.append(f"Host CPU: error {e!r}")
    try:
        parts.append("Host Mode 7 screen:")
        parts.append(dump_screen(bbc))
    except (BeebiumError, grpc.RpcError) as e:
        parts.append(f"Host screen: error {e!r}")
    return "\n".join(parts)


def _stress_break_cycles(
    bbc: Beebium, banner_text: str, settle_seconds: float = BREAK_INTERVAL_EMULATED_SECONDS
) -> None:
    """Cycle Break BREAK_CYCLES times, asserting the server stays up.

    After each Break, runs `settle_seconds` of emulated time and checks
    that `banner_text` reappears on the Mode 7 screen (so we know reset
    actually completed). Then erases the banner before the next cycle so
    the next iteration's "did the banner come back" check is meaningful.
    """
    for attempt in range(1, BREAK_CYCLES + 1):
        if not _server_alive(bbc):
            pytest.fail(_diagnostics(bbc, attempt))

        # Erase the banner so we can detect it being redrawn this cycle.
        try:
            bbc.debugger.ensure_stopped()
            bbc.memory.address.bus[0x7C00:0x8000] = bytes([0x20] * 0x400)
            bbc.debugger.ensure_running()
        except (BeebiumError, grpc.RpcError) as e:
            pytest.fail(f"RPC failure before Break attempt {attempt}: {e!r}")

        try:
            _press_break(bbc)
        except (BeebiumError, grpc.RpcError) as e:
            pytest.fail(f"Break RPC failed on attempt {attempt}: {e!r}\n" + _diagnostics(bbc, attempt))

        # Give the machine emulated time to complete reset and redraw banner.
        try:
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, banner_text),
                emulated_seconds=settle_seconds,
            )
        except (BeebiumError, grpc.RpcError) as e:
            pytest.fail(f"RPC failure waiting for banner on attempt {attempt}: {e!r}\n" + _diagnostics(bbc, attempt))

        if not ok:
            pytest.fail(
                f"Banner '{banner_text}' did not redisplay after Break attempt {attempt}.\n"
                + _diagnostics(bbc, attempt)
            )

        # Tiny wall-clock delay between cycles to approximate the ~1 Hz user
        # cadence from the bug reports. Some races may depend on real time
        # spent in gRPC handlers between resets.
        time.sleep(0.05)

    # Final liveness check.
    assert _server_alive(bbc), _diagnostics(bbc, BREAK_CYCLES)


# =============================================================================
# Issue #27: no Tube
# =============================================================================


@pytest.fixture
def bbc_romram(
    mos_filepath: Path,
    basic_filepath: Path | None,
):
    """Model B with ROM/RAM board, no extensions, booted to BASIC."""
    server_filepath = _find_romram_server()
    if server_filepath is None:
        pytest.skip("beebium-model-b-romram not found")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            startup_timeout=20.0,
        ) as bbc:
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "BASIC"),
                emulated_seconds=15.0,
            )
            if not ok:
                rows = read_mode7_screen(bbc)
                print("\nScreen at boot timeout:")
                for i, row in enumerate(rows):
                    print(f"Row {i:2d}: [{row}]")
                pytest.fail("BASIC banner did not appear after boot")
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@_skip_windows_ci
class TestBreakSegfaultNoTube:
    """Issue #27: repeated Break must not crash the server (no Tube)."""

    def test_repeated_break_does_not_crash(self, bbc_romram: Beebium) -> None:
        """Press Break BREAK_CYCLES times; server must stay alive each time."""
        _stress_break_cycles(bbc_romram, banner_text="BASIC")


# =============================================================================
# Issue #39: with Tube
# =============================================================================


@pytest.fixture
def bbc_anfs_tube(
    beebium_roms_dirpath: Path,
    mos_filepath: Path,
    basic_filepath: Path | None,
):
    """Model B with ROM/RAM board, ANFS in slot 9, and a 65C02 Tube, booted."""
    server_filepath = _find_romram_server()
    if server_filepath is None:
        pytest.skip("beebium-model-b-romram not found")

    anfs_filepath = beebium_roms_dirpath / "acorn-anfs_4_18.rom"
    if not anfs_filepath.exists():
        pytest.skip(f"ANFS ROM not found: {anfs_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--sideways",
                f"slot=9:type=rom:image={anfs_filepath}",
                "--tube-65c02",
            ],
            startup_timeout=20.0,
        ) as bbc:
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "Acorn TUBE 6502 64K"),
                emulated_seconds=30.0,
            )
            if not ok:
                rows = read_mode7_screen(bbc)
                print("\nScreen at boot timeout:")
                for i, row in enumerate(rows):
                    print(f"Row {i:2d}: [{row}]")
                pytest.fail("Tube banner did not appear after boot")
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@_skip_windows_ci
class TestBreakSegfaultWithTube:
    """Issue #39: repeated Break must not crash the server (with Tube)."""

    def test_repeated_break_does_not_crash(self, bbc_anfs_tube: Beebium) -> None:
        """Press Break BREAK_CYCLES times; server must stay alive each time."""
        _stress_break_cycles(bbc_anfs_tube, banner_text="Acorn TUBE 6502 64K")
