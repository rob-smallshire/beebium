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

"""Assembly-level Tube OSWORD &72 tests loaded from disc.

No BASIC, no keyboard entry. Programs are pre-assembled with BeebAsm
and loaded from SSD via Shift-Break auto-boot.
"""

from __future__ import annotations

from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.disassemble import disassemble
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen, read_mode7_screen

from beebium.ext.peripheral.acorn_scsi._proto import scsi_host_adapter_pb2, scsi_host_adapter_pb2_grpc


ASM_DIRPATH = Path(__file__).parent.parent / "asm"


def run_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=1.0):
    """Run until predicate is met or timeout."""
    return bbc.run_until_or_timeout(
        predicate, emulated_seconds, chunk_seconds=chunk_seconds)


def _dump_hang_diagnostics(bbc):
    """Stop both processors and dump diagnostic state."""
    lines = []
    lines.append("")
    lines.append("=" * 60)
    lines.append("ASM TEST HANG DIAGNOSTICS")
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
        parasite = bbc.connect_coprocessor()
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

    try:
        scsi_channel = grpc.insecure_channel(bbc.target)
        scsi_stub = scsi_host_adapter_pb2_grpc.ScsiHostAdapterServiceStub(scsi_channel)
        bus_status = scsi_stub.GetBusStatus(
            scsi_host_adapter_pb2.GetScsiBusStatusRequest()
        )
        lines.append("")
        lines.append("--- SCSI BUS ---")
        lines.append(f"  Phase: {bus_status.phase}")
        lines.append(f"  Selected target: {bus_status.selected_target}")
        lines.append(f"  Status register: 0x{bus_status.status_register:02X}")
        lines.append(f"  IRQ pending: {bus_status.irq_pending}")
        scsi_channel.close()
    except Exception as e:
        lines.append(f"  [SCSI unavailable: {e}]")

    lines.append("=" * 60)
    output = "\n".join(lines)
    print(output)
    return output


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_osw72_read_sense_adfs_read_asm(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """SCSI READ, MODE SENSE, select ADFS, SCSI READ -- all in 6502 ASM.

    Loaded from disc via Shift-Break. No BASIC, no keyboard entry.
    Prints T1:xx T2:xx T3:OK T4:xx DONE as it progresses.
    If any step hangs, the last printed message identifies which one.
    """
    ssd_filepath = ASM_DIRPATH / "test_osw72.ssd"
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
                "--sideways", f"slot=9:type=rom:image={anfs_filepath}",
                "--sideways", f"slot=10:type=rom:image={adfs_filepath}",
                "--sideways", f"slot=11:type=rom:image={dfs_filepath}",
                "--acorn-scsi",
                "--scsi-hdd", f"0:{scsi_hdd_filepath}",
                "--station", "254",
                "--aun", "port=0",
                "--auto-boot",
            ],
            startup_timeout=30.0,
        ) as bbc:
            # Wait for "DONE" (all tests passed) or timeout (hang).
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "DONE"),
                emulated_seconds=60.0,
            )

            screen = dump_screen(bbc)
            rows = read_mode7_screen(bbc)
            screen_text = "\n".join(rows)

            if not ok:
                # Determine which step hung by checking last output.
                print(f"Screen at timeout:\n{screen}")
                _dump_hang_diagnostics(bbc)

                if "T4:" in screen_text:
                    step = "T4 (SCSI READ after ADFS select)"
                elif "T3:" in screen_text:
                    step = "T3 (ADFS select via OSBYTE &8F)"
                elif "T2:" in screen_text:
                    step = "T2 (MODE SENSE via OSWORD &72)"
                elif "T1:" in screen_text:
                    step = "T1 (SCSI READ via OSWORD &72)"
                else:
                    step = "before T1 (program did not start)"

                pytest.fail(f"Hung at {step}. See diagnostics above.")

            # All steps completed. Print screen for verification.
            print(f"Screen:\n{screen}")

            # Verify all four steps completed.
            assert "T1:" in screen_text, "T1 output missing"
            assert "T2:" in screen_text, "T2 output missing"
            assert "T3:OK" in screen_text, "T3 (ADFS select) failed"
            assert "T4:" in screen_text, "T4 output missing"
            assert "DONE" in screen_text, "DONE marker missing"

    except ServerNotFoundError as e:
        pytest.skip(str(e))
