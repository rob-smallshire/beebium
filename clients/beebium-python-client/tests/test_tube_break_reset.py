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

"""Regression tests for issue #38: Break / Ctrl-Break must reset the coprocessor.

Configuration mirrors the bug report:

    beebium-model-b-romram --sideways slot=9:type=rom:image=roms/acorn-anfs_4_18.rom --tube-65C02

Originally, pressing Break only reset the host CPU; the coprocessor kept its
pre-Break state (typically blocked in a Tube R2 OSRDCH wait inside BASIC's
input loop). The host's post-reset Tube banner sequence then had no
respondent and the user saw a blank Mode 7 screen with only the cursor.

These tests boot the machine, blank the banner from screen RAM, press
Break (or Ctrl-Break), and assert that the "Acorn TUBE 6502 64K" banner
reappears -- evidence that the coprocessor was reset alongside the host.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import dump_screen, read_mode7_screen, screen_contains

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)


TUBE_BANNER = "Acorn TUBE 6502 64K"
BOOT_TIMEOUT_SECONDS = 30.0
RESET_TIMEOUT_SECONDS = 30.0


@pytest.fixture
def bbc_anfs_tube(
    beebium_roms_dirpath: Path,
    mos_filepath: Path,
    basic_filepath: Path | None,
):
    """Model B with ROM/RAM board, ANFS in slot 9, and a 65C02 Tube.

    Boots, waits for the Tube banner, then yields the running emulator.
    Each test gets a fresh emulator instance.
    """
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    server_filepath = None
    for candidate in [
        repo_root / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]:
        if candidate.exists():
            server_filepath = candidate
            break
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
                lambda: screen_contains(bbc, TUBE_BANNER),
                emulated_seconds=BOOT_TIMEOUT_SECONDS,
            )
            if not ok:
                rows = read_mode7_screen(bbc)
                print("\nScreen at boot timeout:")
                for i, row in enumerate(rows):
                    print(f"Row {i:2d}: [{row}]")
                pytest.fail(
                    f"Tube banner '{TUBE_BANNER}' did not appear within {BOOT_TIMEOUT_SECONDS}s of emulated time"
                )
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _erase_banner(bbc: Beebium) -> None:
    """Overwrite the Mode 7 banner so we can detect it being redrawn.

    Stops the host, fills screen memory with spaces, then resumes. Without
    this, screen_contains would return True immediately after Break because
    soft_reset preserves screen RAM until MOS clears it.
    """
    bbc.debugger.ensure_stopped()
    blank = bytes([0x20] * (0x8000 - 0x7C00))
    bbc.memory.address.bus[0x7C00:0x8000] = blank
    bbc.debugger.ensure_running()


def _press_break_via_grpc(bbc: Beebium, hold_seconds: float = 0.05) -> None:
    """Press and release the Break key, ensuring the host actually halts.

    The keyboard service callbacks call Machine::break_down() and break_up()
    directly, so the reset is delivered without going through the keyboard
    matrix. The host machine is left running.
    """
    bbc.debugger.ensure_running()
    assert bbc.keyboard.break_down(), "BreakDown RPC failed"
    time.sleep(hold_seconds)
    assert bbc.keyboard.break_up(), "BreakUp RPC failed"


def _press_ctrl_break_via_grpc(bbc: Beebium, hold_seconds: float = 0.05) -> None:
    """Press Ctrl + Break (the MOS hard reset combination)."""
    bbc.debugger.ensure_running()
    bbc.keyboard.ctrl_down()
    time.sleep(0.01)
    assert bbc.keyboard.break_down(), "BreakDown RPC failed"
    time.sleep(hold_seconds)
    assert bbc.keyboard.break_up(), "BreakUp RPC failed"
    time.sleep(0.01)
    bbc.keyboard.ctrl_up()


def _diagnostics(bbc: Beebium) -> str:
    """Build a diagnostic dump for failure messages (host + coprocessor)."""
    lines = []
    try:
        host_regs = bbc.cpu.registers
        lines.append(
            f"Host CPU: PC=${host_regs.pc:04X} A=${host_regs.a:02X} "
            f"X=${host_regs.x:02X} Y=${host_regs.y:02X} "
            f"SP=${host_regs.sp:02X}"
        )
    except Exception as e:  # pragma: no cover - diagnostics only
        lines.append(f"Host CPU: error {e!r}")
    try:
        if bbc.tube.is_enabled:
            coprocessor = bbc.connect_coprocessor()
            p_regs = coprocessor.cpu.registers
            lines.append(
                f"Coprocessor CPU: PC=${p_regs.pc:04X} A=${p_regs.a:02X} "
                f"X=${p_regs.x:02X} Y=${p_regs.y:02X} "
                f"SP=${p_regs.sp:02X}"
            )
        else:
            lines.append("Coprocessor: Tube not enabled")
    except Exception as e:  # pragma: no cover - diagnostics only
        lines.append(f"Coprocessor CPU: error {e!r}")
    try:
        lines.append("Host Mode 7 screen:")
        lines.append(dump_screen(bbc))
    except Exception as e:  # pragma: no cover - diagnostics only
        lines.append(f"Host screen: error {e!r}")
    return "\n".join(lines)


@_skip_windows_ci
class TestTubeBreakReset:
    """Issue #38: Break and Ctrl-Break must restore the Tube boot banner."""

    def test_break_restores_tube_banner(self, bbc_anfs_tube: Beebium) -> None:
        """After pressing Break, the Tube boot banner must redisplay."""
        bbc = bbc_anfs_tube
        _erase_banner(bbc)
        _press_break_via_grpc(bbc)

        ok = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, TUBE_BANNER),
            emulated_seconds=RESET_TIMEOUT_SECONDS,
        )
        assert ok, (
            f"Tube banner '{TUBE_BANNER}' did not redisplay within "
            f"{RESET_TIMEOUT_SECONDS}s of emulated time after Break.\n"
            f"{_diagnostics(bbc)}"
        )

    def test_ctrl_break_restores_tube_banner(self, bbc_anfs_tube: Beebium) -> None:
        """After pressing Ctrl-Break, the Tube boot banner must redisplay."""
        bbc = bbc_anfs_tube
        _erase_banner(bbc)
        _press_ctrl_break_via_grpc(bbc)

        ok = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, TUBE_BANNER),
            emulated_seconds=RESET_TIMEOUT_SECONDS,
        )
        assert ok, (
            f"Tube banner '{TUBE_BANNER}' did not redisplay within "
            f"{RESET_TIMEOUT_SECONDS}s of emulated time after Ctrl-Break.\n"
            f"{_diagnostics(bbc)}"
        )
