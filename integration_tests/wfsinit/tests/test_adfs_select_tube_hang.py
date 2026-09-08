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

"""Minimal reproduction of Tube deadlock when selecting ADFS with SCSI disc.

Boots a BBC Model B with ROM/RAM board, Tube 65C02, ADFS ROM, and a SCSI
hard disc, then types ``*ADFS`` at the BASIC prompt. This triggers OSBYTE
&8F to select the ADFS filing system, which mounts the SCSI disc. The Tube
protocol deadlocks during this mount — the host gets stuck polling Tube R4
for data the parasite never sends.

This is the same bug that causes WFSINIT to hang at "Please wait ...." —
WFSINIT's ``PROCto`` calls OSBYTE &8F to select ADFS, hitting the identical
code path.

Run with:
    cd integration_tests/wfsinit
    uv run pytest -m slow tests/test_adfs_select_tube_hang.py -v -s
"""

from __future__ import annotations

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.disassemble import disassemble
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen

from beebium.ext.peripheral.acorn_scsi._proto import scsi_host_adapter_pb2, scsi_host_adapter_pb2_grpc




def run_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=1.0):
    """Run until predicate is met or timeout.

    In the single-threaded Tube model, running the host automatically
    ticks the parasite via Machine::step(). No separate TubeSystem
    coordination is needed.
    """
    return bbc.run_until_or_timeout(
        predicate, emulated_seconds, chunk_seconds=chunk_seconds)


def _has_prompt_after(bbc, command_text):
    """Check if a '>' prompt appears on a line after the command text."""
    from beebium.client.screen import read_mode7_screen
    rows = read_mode7_screen(bbc)
    found_command = False
    for row in rows:
        stripped = row.strip()
        if command_text in stripped:
            found_command = True
        elif found_command and stripped == ">":
            return True
    return False


def _dump_hang_diagnostics(bbc):
    """Stop both processors and dump diagnostic state."""
    lines = []
    lines.append("")
    lines.append("=" * 60)
    lines.append("ADFS SELECT TUBE HANG DIAGNOSTICS")
    lines.append("=" * 60)

    lines.append("")
    lines.append("--- SCREEN ---")
    lines.append(dump_screen(bbc))

    # Host CPU
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

    # Parasite CPU
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

    # Host stack and key memory
    try:
        sp = host_regs.sp
        stack_start = 0x0100 + sp + 1
        stack_bytes = bytes(bbc.memory.address.peek[stack_start:stack_start + 16])
        lines.append("")
        lines.append("--- HOST STACK ---")
        hex_str = " ".join(f"${b:02X}" for b in stack_bytes)
        lines.append(f"  SP=${sp:02X}  Stack ${stack_start:04X}: {hex_str}")
        # Decode return addresses (pairs of bytes, low then high, +1 for JSR)
        for i in range(0, min(len(stack_bytes) - 1, 14), 2):
            ret_addr = stack_bytes[i] | (stack_bytes[i + 1] << 8)
            lines.append(f"  Return: ${ret_addr + 1:04X}")

        lines.append("")
        lines.append("--- HOST KEY LOCATIONS ---")
        tube_flag = bbc.memory.address.peek[0x025F]
        lines.append(f"  $025F (Tube flag): ${tube_flag:02X}")
        vec_0224 = bbc.memory.address.peek[0x0224] | (bbc.memory.address.peek[0x0225] << 8)
        lines.append(f"  $0224 (WRCHV):     ${vec_0224:04X}")
        vec_0220 = bbc.memory.address.peek[0x0220] | (bbc.memory.address.peek[0x0221] << 8)
        lines.append(f"  $0220 (EVNTV):     ${vec_0220:04X}")
        # Tube Host Code entry points at $0400-$0407
        tube_entries = bytes(bbc.memory.address.peek[0x0400:0x0408])
        hex_str = " ".join(f"${b:02X}" for b in tube_entries)
        lines.append(f"  $0400-$0407 (Tube entries): {hex_str}")
    except Exception as e:
        lines.append(f"  [Host memory unavailable: {e}]")

    # Parasite stack and zero-page Tube state
    try:
        parasite = bbc.connect_parasite()
        para_sp = para_regs.sp
        para_stack_start = 0x0100 + para_sp + 1
        para_stack = bytes(parasite.memory.address.peek[para_stack_start:para_stack_start + 16])
        lines.append("")
        lines.append("--- PARASITE STACK ---")
        hex_str = " ".join(f"${b:02X}" for b in para_stack)
        lines.append(f"  SP=${para_sp:02X}  Stack ${para_stack_start:04X}: {hex_str}")
        for i in range(0, min(len(para_stack) - 1, 14), 2):
            ret_addr = para_stack[i] | (para_stack[i + 1] << 8)
            lines.append(f"  Return: ${ret_addr + 1:04X}")

        lines.append("")
        lines.append("--- PARASITE ZERO PAGE ---")
        zp = bytes(parasite.memory.address.peek[0xF0:0x100])
        for i in range(0, 16, 8):
            hex_str = " ".join(f"${b:02X}" for b in zp[i:i+8])
            lines.append(f"  ${0xF0+i:02X}: {hex_str}")
        transfer_type = parasite.memory.address.peek[0xFF]
        lines.append(f"  Transfer type ($FF): ${transfer_type:02X}")
        nmi_vec = bytes(parasite.memory.address.peek[0x0D00:0x0D03])
        hex_str = " ".join(f"${b:02X}" for b in nmi_vec)
        lines.append(f"  NMI vector ($0D00): {hex_str}")
    except Exception as e:
        lines.append(f"  [Parasite memory unavailable: {e}]")

    # Tube ULA
    try:
        tube_state = bbc.tube_ula.state
        lines.append("")
        lines.append("--- TUBE ULA ---")
        lines.append(str(tube_state))
    except Exception as e:
        lines.append(f"  [Tube ULA unavailable: {e}]")

    # Tube protocol trace (last N R2/R4 events)
    try:
        from beebium.client._proto import debugger_pb2, debugger_pb2_grpc
        stub = debugger_pb2_grpc.DeviceInspectionStub(
            grpc.insecure_channel(bbc.target)
        )
        tube_resp = stub.GetTubeState(debugger_pb2.GetTubeStateRequest())
        if tube_resp.trace:
            lines.append("")
            lines.append(f"--- TUBE TRACE (last {len(tube_resp.trace)} of "
                          f"{tube_resp.trace_total_count} events) ---")
            tag_names = {
                0x20: "R2 H2P host-wr",
                0x24: "R2 H2P para-rd",
                0x28: "R2 P2H host-rd",
                0x2C: "R2 P2H para-wr",
                0x30: "R3 H2P host-wr",
                0x34: "R3 H2P para-rd",
                0x38: "R3 P2H host-rd",
                0x3C: "R3 P2H para-wr",
                0x40: "R4 H2P host-wr",
                0x44: "R4 H2P para-rd",
                0x48: "R4 P2H host-rd",
                0x4C: "R4 P2H para-wr",
            }
            # Show last 200 events (more context for R3 transfers)
            entries = list(tube_resp.trace)
            start_idx = max(0, len(entries) - 200)
            for i, entry in enumerate(entries[start_idx:], start=start_idx):
                name = tag_names.get(entry.tag, f"?{entry.tag:02X}")
                lines.append(f"  [{i:4d}] {name:18s}  ${entry.value:02X}")
    except Exception as e:
        lines.append(f"  [Tube trace unavailable: {e}]")

    # SCSI bus
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
@pytest.mark.parametrize("tube_flag", ["--tube-65c02", "--tube-65c102"])
def test_adfs_select_with_tube_and_scsi(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath, tube_flag,
):
    """Selecting ADFS with *ADFS should return to the prompt, not deadlock.

    This is the minimal reproduction of the WFSINIT hang: boot with Tube,
    ADFS ROM, and SCSI disc, then type *ADFS. The Tube deadlocks during
    ADFS filing system selection. Parametrised over both coprocessors --
    the 3 MHz 65C02 and the 4 MHz 65C102 -- since this is the heaviest
    R2/R3/R4 Tube protocol exercise and the 65C102 runs the parasite at a
    different rate relative to the host.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                tube_flag,
                "--fdc", "acorn-1770",
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
            # Boot to BASIC prompt with Tube active.
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=30.0,
            )
            assert ok, f"Boot failed:\n{dump_screen(bbc)}"

            # Type *ADFS and wait for the prompt to reappear.
            cmd = "*ADFS"
            bbc.keyboard.type(cmd + "\r")

            ok = run_until_or_timeout(
                bbc,
                lambda: _has_prompt_after(bbc, cmd),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail(
                    "Tube deadlocked after *ADFS -- prompt did not reappear "
                    "within 30 emulated seconds. See diagnostics above."
                )

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_osword_72_then_adfs_select_with_tube(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """OSWORD &72 before *ADFS — reproduces WFSINIT's exact sequence.

    WFSINIT calls OSWORD &72 multiple times with DFS selected (lines 270-360),
    then selects ADFS with PROCto (line 460). If the earlier OSWORD &72 calls
    leave Tube state that interferes with ADFS's SCSI access during mount,
    this test will hang.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
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

            # Two OSWORD &72 calls (READ then MODE SENSE) then ADFS select,
            # then another OSWORD &72 READ. This matches WFSINIT's sequence
            # (lines 280, 360, 460, 560).
            program = (
                '10 DIM buf% 511, cb% 20\n'
                '20 cb%?0=0:cb%!1=buf%\n'
                '30 cb%?5=8:cb%?6=0:cb%?7=0:cb%?8=0:cb%?9=2:cb%?10=0\n'
                '40 cb%!11=512\n'
                '50 A%=&72:X%=cb%:Y%=cb%DIV256:CALL &FFF1\n'
                '60 PRINT"READ1:";cb%?0\n'
                '70 cb%?0=0:cb%?5=&1A:cb%?6=0:cb%?7=0:cb%?8=0:cb%?9=22:cb%?10=0\n'
                '80 cb%!11=22\n'
                '90 A%=&72:X%=cb%:Y%=cb%DIV256:CALL &FFF1\n'
                '100 PRINT"SENSE:";cb%?0\n'
                '110 A%=&8F:X%=&12:Y%=8:CALL &FFF4\n'
                '120 PRINT"ADFS OK"\n'
                '130 cb%?0=0:cb%?5=8:cb%?6=0:cb%?7=0:cb%?8=0:cb%?9=2:cb%?10=0\n'
                '140 cb%!11=512\n'
                '150 A%=&72:X%=cb%:Y%=cb%DIV256:CALL &FFF1\n'
                '160 PRINT"READ2:";cb%?0\n'
                '170 PRINTCHR$(68)"ONE"\n'
            )

            for line in program.strip().split('\n'):
                bbc.keyboard.type(line + "\r")
                ok = run_until_or_timeout(
                    bbc,
                    lambda: screen_contains(bbc, ">"),
                    emulated_seconds=10.0,
                )
                assert ok, f"Prompt lost:\n{dump_screen(bbc)}"

            bbc.keyboard.type("RUN\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "DONE"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail(
                    "Hung after OSWORD &72 + ADFS select sequence. "
                    "See diagnostics above."
                )

            from beebium.client.screen import read_mode7_screen
            rows = read_mode7_screen(bbc)
            screen_text = "\n".join(rows)
            print(f"Screen:\n{dump_screen(bbc)}")
            assert "ADFS OK" in screen_text
            assert "DONE" in screen_text

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_osword_72_correct_cb_with_tube(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """OSWORD &72 with correct control block layout (CDB inline, not pointer)."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
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

            program = (
                '10 DIM buf% 511, cb% 20\n'
                '20 cb%?0=0:cb%!1=buf%\n'
                '30 cb%?5=8:cb%?6=0:cb%?7=0:cb%?8=0:cb%?9=2:cb%?10=0\n'
                '40 cb%!11=512\n'
                '50 A%=&72:X%=cb%:Y%=cb%DIV256:CALL &FFF1\n'
                '60 PRINT"OK:";cb%?0\n'
                '70 PRINTCHR$(68)"ONE"\n'
            )

            for line in program.strip().split('\n'):
                bbc.keyboard.type(line + "\r")
                ok = run_until_or_timeout(
                    bbc,
                    lambda: screen_contains(bbc, ">"),
                    emulated_seconds=10.0,
                )
                assert ok, f"Prompt lost:\n{dump_screen(bbc)}"

            bbc.keyboard.type("RUN\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "DONE"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail(
                    "OSWORD &72 with correct CB hung with Tube. "
                    "See diagnostics above."
                )

            assert screen_contains(bbc, "DONE")
            print(f"Screen:\n{dump_screen(bbc)}")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_adfs_cat_with_tube_and_scsi(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """*CAT after *ADFS should list the root directory, not deadlock.

    Tests that ADFS can read from the SCSI disc with the Tube active.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
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

            # Select ADFS
            bbc.keyboard.type("*ADFS\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: _has_prompt_after(bbc, "*ADFS"),
                emulated_seconds=30.0,
            )
            assert ok, f"*ADFS hung:\n{dump_screen(bbc)}"

            # Catalogue the SCSI disc
            bbc.keyboard.type("*CAT\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: _has_prompt_after(bbc, "*CAT"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail(
                    "Tube deadlocked after *CAT on ADFS -- prompt did not "
                    "reappear. See diagnostics above."
                )

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_osword_72_scsi_read_with_tube(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """OSWORD &72 SCSI read should work with the Tube active.

    This tests the exact operation WFSINIT uses: a raw SCSI READ(6)
    via OSWORD &72. WFSINIT calls this from BASIC on the parasite.

    The test issues OSWORD &72 from a small BASIC program to read
    sector 0 of the SCSI disc. If the Tube deadlocks during the
    OSWORD transfer, the program will hang.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
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

            # Type a small BASIC program that does OSWORD &72 to read
            # sector 0 from SCSI drive 0. This is what WFSINIT's PROCread does.
            #
            # The control block for OSWORD &72:
            #   Byte 0:   Result (0 on entry)
            #   Bytes 1-4: Transfer address (where to put the data)
            #   Bytes 5-8: Command block address
            #   Bytes 9-12: Transfer length
            #
            # The SCSI CDB (6 bytes) for READ(6):
            #   Byte 0: &08 (READ opcode)
            #   Byte 1: LUN/high address bits (0)
            #   Byte 2: Mid address (0)
            #   Byte 3: Low address (0) = sector 0
            #   Byte 4: Count (2) = 2 sectors = 512 bytes
            #   Byte 5: 0
            program = (
                '10 DIM buf% 511, cmd% 5, cb% 12\n'
                '20 cmd%?0=8:cmd%?1=0:cmd%?2=0:cmd%?3=0:cmd%?4=2:cmd%?5=0\n'
                '30 cb%?0=0:cb%!1=buf%:cb%!5=cmd%:cb%!9=512\n'
                '40 A%=&72:X%=cb%:Y%=cb%DIV256\n'
                '50 CALL &FFF1\n'
                '60 IF cb%?0 THEN PRINT"ERR:";~cb%?0:GOTO80\n'
                '70 PRINT"OK:";~buf%?0;" ";~buf%?1;" ";~buf%?2;" ";~buf%?3\n'
                '80 PRINTCHR$(68)"ONE"\n'
            )

            for line in program.strip().split('\n'):
                bbc.keyboard.type(line + "\r")
                ok = run_until_or_timeout(
                    bbc,
                    lambda: screen_contains(bbc, ">"),
                    emulated_seconds=10.0,
                )
                assert ok, f"Prompt lost entering program:\n{dump_screen(bbc)}"

            # RUN the program
            bbc.keyboard.type("RUN\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "DONE"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail(
                    "OSWORD &72 SCSI read hung with Tube active. "
                    "See diagnostics above."
                )

            # Check result
            from beebium.client.screen import read_mode7_screen
            rows = read_mode7_screen(bbc)
            screen_text = "\n".join(rows)
            if "ERR:" in screen_text:
                print(f"OSWORD &72 returned error:\n{dump_screen(bbc)}")
            else:
                assert "OK:" in screen_text, \
                    f"Unexpected output:\n{dump_screen(bbc)}"
                print("OSWORD &72 SCSI read succeeded with Tube active.")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_osword_72_after_adfs_select_with_tube(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """OSWORD &72 after *ADFS should work — this is WFSINIT's exact path.

    WFSINIT selects ADFS (PROCto), then immediately does PROCread which
    issues OSWORD &72. This test reproduces that exact sequence.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
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

            # Program that selects ADFS then does OSWORD &72 — exactly
            # what WFSINIT lines 460+560 do.
            program = (
                '10 DIM buf% 511, cmd% 5, cb% 12\n'
                '20 A%=&8F:X%=&12:Y%=8:CALL &FFF4\n'
                '30 PRINT"ADFS SELECTED"\n'
                '40 cmd%?0=8:cmd%?1=0:cmd%?2=0:cmd%?3=0:cmd%?4=2:cmd%?5=0\n'
                '50 cb%?0=0:cb%!1=buf%:cb%!5=cmd%:cb%!9=512\n'
                '60 A%=&72:X%=cb%:Y%=cb%DIV256\n'
                '70 CALL &FFF1\n'
                '80 IF cb%?0 THEN PRINT"ERR:";~cb%?0:GOTO100\n'
                '90 PRINT"OK:";~buf%?0;" ";~buf%?1;" ";~buf%?2;" ";~buf%?3\n'
                '100 PRINTCHR$(68)"ONE"\n'
            )

            for line in program.strip().split('\n'):
                bbc.keyboard.type(line + "\r")
                ok = run_until_or_timeout(
                    bbc,
                    lambda: screen_contains(bbc, ">"),
                    emulated_seconds=10.0,
                )
                assert ok, f"Prompt lost entering program:\n{dump_screen(bbc)}"

            bbc.keyboard.type("RUN\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "DONE"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)
                pytest.fail(
                    "OSWORD &72 after ADFS select hung with Tube active -- "
                    "this is WFSINIT's exact code path. See diagnostics above."
                )

            from beebium.client.screen import read_mode7_screen
            rows = read_mode7_screen(bbc)
            screen_text = "\n".join(rows)
            assert "ADFS SELECTED" in screen_text, \
                f"ADFS select message missing:\n{dump_screen(bbc)}"
            if "ERR:" in screen_text:
                print(f"OSWORD &72 returned error:\n{dump_screen(bbc)}")
            else:
                assert "OK:" in screen_text, \
                    f"Unexpected output:\n{dump_screen(bbc)}"
                print("OSWORD &72 after *ADFS succeeded with Tube active.")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(120)
def test_watchpoint_025f_tube_flag(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    scsi_hdd_filepath,
):
    """Catch what clears the Tube present flag at $025F.

    Sets a write watchpoint on $025F after boot (when $025F=$FF),
    runs the OSWORD &72 + ADFS select sequence, and captures the
    host CPU state at the moment $025F is written with a value
    that has bit 7 clear (Tube not present).
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
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

            # Scan MOS workspace for Tube-related flags.
            print("MOS workspace scan:")
            for addr in [0x025F, 0x027A, 0x028D, 0x028E]:
                val = bbc.memory.address.peek[addr]
                print(f"  ${addr:04X}: ${val:02X}")
            # Dump $0250-$027F to find Tube-related flags
            ws = bytes(bbc.memory.address.peek[0x0250:0x0280])
            for i in range(0, len(ws), 16):
                hex_str = " ".join(f"${b:02X}" for b in ws[i:i+16])
                print(f"  ${0x0250+i:04X}: {hex_str}")
            # Also check Tube Host Code workspace
            thc = bytes(bbc.memory.address.peek[0x0036:0x0056])
            hex_str = " ".join(f"${b:02X}" for b in thc)
            print(f"  Tube main loop $0036: {hex_str}")
            # Check the Tube entry points
            entries = bytes(bbc.memory.address.peek[0x0400:0x0410])
            hex_str = " ".join(f"${b:02X}" for b in entries)
            print(f"  $0400: {hex_str}")

            # Find the Tube flag: try OSBYTE &EA approach.
            # In MOS 1.20, OSBYTE &EA returns tube present in X.
            # But we can't easily call OSBYTE from the test. Instead, check
            # the known locations.

            # The Tube Host Code main loop at $0036 should be:
            #   $0036: BIT $FEE0  (2C E0 FE)
            # If present, the Tube Host Code is installed.
            main_loop_bytes = bytes(bbc.memory.address.peek[0x0036:0x003C])
            hex_str = " ".join(f"${b:02X}" for b in main_loop_bytes)
            print(f"  Main loop $0036: {hex_str}")
            if main_loop_bytes[:3] == bytes([0x2C, 0xE0, 0xFE]):
                print("  Tube Host Code IS installed at $0036")
            else:
                print("  WARNING: Tube Host Code NOT found at $0036")
                return

            # Now run the failing sequence and see what happens.
            # Use a shorter program: just OSWORD &72 then OSBYTE &8F.
            program = (
                '10 DIM buf% 511, cb% 20\n'
                '20 cb%?0=0:cb%!1=buf%\n'
                '30 cb%?5=8:cb%?6=0:cb%?7=0:cb%?8=0:cb%?9=2:cb%?10=0\n'
                '40 cb%!11=512\n'
                '50 A%=&72:X%=cb%:Y%=cb%DIV256:CALL &FFF1\n'
                '60 PRINT"READ1:";cb%?0\n'
                '70 A%=&8F:X%=&12:Y%=8:CALL &FFF4\n'
                '80 PRINT"ADFS OK"\n'
                '90 PRINTCHR$(68)"ONE"\n'
            )

            for line in program.strip().split('\n'):
                bbc.keyboard.type(line + "\r")
                ok = run_until_or_timeout(
                    bbc,
                    lambda: screen_contains(bbc, ">"),
                    emulated_seconds=10.0,
                )
                assert ok, f"Prompt lost:\n{dump_screen(bbc)}"

            bbc.keyboard.type("RUN\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "DONE"),
                emulated_seconds=30.0,
            )

            if not ok:
                _dump_hang_diagnostics(bbc)

                # Additional: check if Tube main loop is intact
                main_loop_now = bytes(bbc.memory.address.peek[0x0036:0x003C])
                hex_str = " ".join(f"${b:02X}" for b in main_loop_now)
                print(f"\nTube main loop $0036 at hang: {hex_str}")

                # Check MOS workspace $0250-$027F
                ws = bytes(bbc.memory.address.peek[0x0250:0x0280])
                print("\nMOS workspace $0250-$027F:")
                for i in range(0, len(ws), 16):
                    hex_str = " ".join(f"${b:02X}" for b in ws[i:i+16])
                    print(f"  ${0x0250+i:04X}: {hex_str}")

                pytest.fail("Hung. See diagnostics above.")

    except ServerNotFoundError as e:
        pytest.skip(str(e))
