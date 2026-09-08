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

"""Test that coprocessor client returns coprocessor state, not host state.

Boots with a 65C02 Tube, stops both processors, and verifies that
host and coprocessor report independent CPU registers and memory.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains


@pytest.fixture(scope="function")
def bbc_with_tube(beebium_roms_dirpath: Path, mos_filepath: Path, basic_filepath: Path | None):
    """Model B ROM/RAM board with Tube 65C02."""
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    server = None
    for c in [
        repo_root / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]:
        if c.exists():
            server = c
            break
    if server is None:
        pytest.skip("beebium-model-b-romram not found")

    anfs = beebium_roms_dirpath / "acorn-anfs_4_18.rom"
    if not anfs.exists():
        pytest.skip(f"ANFS ROM not found: {anfs}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server,
            extra_args=[
                "--sideways",
                f"9:rom:{anfs}",
                "--tube-65c02",
            ],
            startup_timeout=20.0,
        ) as instance:
            # Wait for Tube banner and BASIC prompt.
            ok = instance.run_until_or_timeout(
                lambda: screen_contains(instance, ">"),
                emulated_seconds=30.0,
            )
            if not ok:
                pytest.fail("Boot did not reach BASIC prompt with Tube")
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_coprocessor_cpu_differs_from_host(bbc_with_tube):
    """Host and coprocessor CPUs should report different register state."""
    bbc = bbc_with_tube
    coprocessor = bbc.connect_coprocessor()

    # Stop both processors.
    bbc.debugger.stop()
    coprocessor.debugger.stop()

    host_regs = bbc.cpu.registers
    para_regs = coprocessor.cpu.registers

    # After boot to BASIC prompt, the host PC should be in MOS/ROM territory
    # (typically $E000-$FFFF) while the coprocessor PC should be in the Tube
    # client area (typically $F800-$FFFF on the coprocessor, or in BASIC ROM).
    # The key assertion: they must not be identical. Two independent CPUs
    # will not have identical PC, A, X, Y, SP, and P simultaneously.
    assert not (
        host_regs.pc == para_regs.pc
        and host_regs.a == para_regs.a
        and host_regs.x == para_regs.x
        and host_regs.y == para_regs.y
        and host_regs.sp == para_regs.sp
    ), (
        f"Host and coprocessor registers are identical -- "
        f"coprocessor client is likely returning host state.\n"
        f"Host:     PC=${host_regs.pc:04X} A=${host_regs.a:02X} "
        f"X=${host_regs.x:02X} Y=${host_regs.y:02X} SP=${host_regs.sp:02X}\n"
        f"Coprocessor: PC=${para_regs.pc:04X} A=${para_regs.a:02X} "
        f"X=${para_regs.x:02X} Y=${para_regs.y:02X} SP=${para_regs.sp:02X}"
    )

    print(
        f"Host:     PC=${host_regs.pc:04X} A=${host_regs.a:02X} "
        f"X=${host_regs.x:02X} Y=${host_regs.y:02X} SP=${host_regs.sp:02X}"
    )
    print(
        f"Coprocessor: PC=${para_regs.pc:04X} A=${para_regs.a:02X} "
        f"X=${para_regs.x:02X} Y=${para_regs.y:02X} SP=${para_regs.sp:02X}"
    )


def test_coprocessor_memory_differs_from_host(bbc_with_tube):
    """Host and coprocessor memory should be independent address spaces."""
    bbc = bbc_with_tube
    coprocessor = bbc.connect_coprocessor()

    bbc.debugger.stop()
    coprocessor.debugger.stop()

    # On the host, $FEE0 is Tube ULA register R1 status (host side).
    # On the coprocessor, $FEE0 is not a Tube register (Tube is at $FEF8-$FEFF).
    # Read a byte from each -- they should differ.
    host_fee0 = bbc.memory.address.peek[0xFEE0]
    para_fee0 = coprocessor.memory.address.peek[0xFEE0]

    # Also check the coprocessor Tube registers at $FEF8.
    # The host has no Tube at $FEF8 (it's in the high ROM area).
    host_fef8 = bbc.memory.address.peek[0xFEF8]
    para_fef8 = coprocessor.memory.address.peek[0xFEF8]

    print(f"Host     $FEE0=${host_fee0:02X}  $FEF8=${host_fef8:02X}")
    print(f"Coprocessor $FEE0=${para_fee0:02X}  $FEF8=${para_fef8:02X}")

    # The coprocessor's $0000-$00FF (zero page) should differ from the host's
    # zero page, since they run different code with different variables.
    host_zp = bytes(bbc.memory.address.peek[0x00:0x10])
    para_zp = bytes(coprocessor.memory.address.peek[0x00:0x10])

    print(f"Host     ZP $00-$0F: {host_zp.hex()}")
    print(f"Coprocessor ZP $00-$0F: {para_zp.hex()}")

    assert host_zp != para_zp, (
        "Host and coprocessor zero page are identical -- coprocessor client is likely returning host memory."
    )
