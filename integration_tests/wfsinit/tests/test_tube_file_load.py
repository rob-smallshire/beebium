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

"""Test Tube R3/R4 data transfer by loading a file from floppy into parasite memory.

This isolates the disc-to-parasite data path: DFS reads sectors from floppy,
the Tube Host Code claims the transfer address and forwards data via R3/R4
to the parasite. This is the code path that fails during auto-boot.
"""

from __future__ import annotations

from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.disassemble import disassemble
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen

from beebium.ext.peripheral.acorn_scsi._proto import scsi_host_adapter_pb2, scsi_host_adapter_pb2_grpc


ASM_DIRPATH = Path(__file__).parent.parent / "asm"


def run_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=1.0):
    return bbc.run_until_or_timeout(
        predicate, emulated_seconds, chunk_seconds=chunk_seconds)


def _has_prompt_after(bbc, command_text):
    """True once a '>' prompt appears on a line after the command text.

    Typing is paced by the server, so the prompt and a partly typed command
    are both on screen before the command has been entered. Only a prompt on
    a later line shows the command has completed.
    """
    from beebium.client.screen import read_mode7_screen
    found_command = False
    for row in read_mode7_screen(bbc):
        stripped = row.strip()
        if command_text in stripped:
            found_command = True
        elif found_command and stripped == ">":
            return True
    return False


def _dump_hang_diagnostics(bbc):
    lines = []
    lines.append("")
    lines.append("=" * 60)
    lines.append("TUBE FILE LOAD HANG DIAGNOSTICS")
    lines.append("=" * 60)

    lines.append("")
    lines.append("--- SCREEN ---")
    lines.append(dump_screen(bbc))

    try:
        bbc.debugger.stop()
        host_regs = bbc.cpu.registers
        lines.append("--- HOST CPU ---")
        lines.append(f"  PC=${host_regs.pc:04X}  A=${host_regs.a:02X}  "
                      f"X=${host_regs.x:02X}  Y=${host_regs.y:02X}  "
                      f"SP=${host_regs.sp:02X}  P=${host_regs.p:02X}")
        host_pc = host_regs.pc
        start = max(0, host_pc - 16)
        code = bytes(bbc.memory.address.peek[start:start + 48])
        lines.append(f"  Code around ${host_pc:04X}:")
        for line in disassemble(code, start=start, length=48):
            marker = " >>>" if line.startswith(f"${host_pc:04X}") else "    "
            lines.append(f"  {marker} {line}")
    except Exception as e:
        lines.append(f"  [Host CPU unavailable: {e}]")

    try:
        parasite = bbc.connect_parasite()
        parasite.debugger.stop()
        para_regs = parasite.cpu.registers
        lines.append("")
        lines.append("--- PARASITE CPU ---")
        lines.append(f"  PC=${para_regs.pc:04X}  A=${para_regs.a:02X}  "
                      f"X=${para_regs.x:02X}  Y=${para_regs.y:02X}  "
                      f"SP=${para_regs.sp:02X}  P=${para_regs.p:02X}")
        para_pc = para_regs.pc
        start = max(0, para_pc - 16)
        code = bytes(parasite.memory.address.peek[start:start + 48])
        lines.append(f"  Code around ${para_pc:04X}:")
        for line in disassemble(code, start=start, length=48):
            marker = " >>>" if line.startswith(f"${para_pc:04X}") else "    "
            lines.append(f"  {marker} {line}")
    except Exception as e:
        lines.append(f"  [Parasite CPU unavailable: {e}]")

    try:
        tube_state = bbc.tube_ula.state
        lines.append("")
        lines.append("--- TUBE ULA ---")
        lines.append(str(tube_state))
    except Exception as e:
        lines.append(f"  [Tube ULA unavailable: {e}]")

    lines.append("=" * 60)
    output = "\n".join(lines)
    print(output)
    return output


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_load_file_via_tube(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """*LOAD a file from floppy into parasite memory via the Tube.

    Boot to BASIC prompt (no auto-boot), then type *LOAD to trigger
    the Tube R3/R4 data transfer path. The hello SSD has a small
    TEST file at &1F00.
    """
    # Use the full-size oaknut-dfs SSD (verified to work without Tube)
    ssd_filepath = ASM_DIRPATH / "test_hello_proper.ssd"
    if not ssd_filepath.exists():
        pytest.skip(f"Test SSD not found: {ssd_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
                "--floppy", f"0:{ssd_filepath}",
                "--sideways", f"9:rom:{anfs_filepath}",
                "--sideways", f"10:rom:{adfs_filepath}",
                "--sideways", f"11:rom:{dfs_filepath}",
                "--acorn-scsi",
                "--scsi-hdd", f"0:{scsi_hdd_filepath}",
                "--station", "254",
                "--aun", "port=0",
            ],
            startup_timeout=30.0,
        ) as bbc:
            # Boot to BASIC prompt (no auto-boot).
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=30.0,
            )
            assert ok, f"Boot failed:\n{dump_screen(bbc)}"

            # Type *LOAD to load the TEST file into parasite memory.
            # This triggers the Tube address claim + R3/R4 data transfer.
            bbc.keyboard.type('*LOAD TEST 1F00\r')

            ok = run_until_or_timeout(
                bbc,
                lambda: _has_prompt_after(bbc, "LOAD TEST"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail("*LOAD hung with Tube active. See diagnostics.")

            screen = dump_screen(bbc)
            print(f"*LOAD completed. Screen:\n{screen}")

            # Verify the file was loaded by checking parasite memory.
            parasite = bbc.connect_parasite()
            # The hello program starts with LDX #0 (A2 00)
            first_bytes = bytes(parasite.memory.address.peek[0x1F00:0x1F04])
            print(f"Parasite memory at $1F00: {first_bytes.hex()}")
            assert first_bytes[0] == 0xA2, \
                f"Expected LDX opcode (A2) at $1F00, got ${first_bytes[0]:02X}"

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_call_loaded_file_via_tube(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """*LOAD then CALL the loaded program via the Tube.

    Tests the full sequence: load file from floppy into parasite memory
    via Tube data transfer, then execute it.
    """
    ssd_filepath = ASM_DIRPATH / "test_hello_proper.ssd"
    if not ssd_filepath.exists():
        pytest.skip(f"Test SSD not found: {ssd_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
                "--floppy", f"0:{ssd_filepath}",
                "--sideways", f"9:rom:{anfs_filepath}",
                "--sideways", f"10:rom:{adfs_filepath}",
                "--sideways", f"11:rom:{dfs_filepath}",
                "--acorn-scsi",
                "--scsi-hdd", f"0:{scsi_hdd_filepath}",
                "--station", "254",
                "--aun", "port=0",
            ],
            startup_timeout=30.0,
        ) as bbc:
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=30.0,
            )
            assert ok, f"Boot failed:\n{dump_screen(bbc)}"

            # Load the file
            bbc.keyboard.type('*LOAD TEST 1F00\r')
            ok = run_until_or_timeout(
                bbc,
                lambda: _has_prompt_after(bbc, "LOAD TEST"),
                emulated_seconds=30.0,
            )
            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail("*LOAD hung. See diagnostics.")

            # Execute the loaded program
            bbc.keyboard.type('CALL &1F00\r')
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "DONE"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail("CALL &1F00 hung. See diagnostics.")

            screen = dump_screen(bbc)
            print(f"Screen:\n{screen}")
            assert screen_contains(bbc, "HELLO")

    except ServerNotFoundError as e:
        pytest.skip(str(e))
