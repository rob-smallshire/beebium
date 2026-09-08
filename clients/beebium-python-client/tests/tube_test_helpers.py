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

"""Shared utilities for Tube integration tests.

Provides coupled stepping, diagnostic dump functions, and constants
used by multiple Tube game test suites (Elite, Chuckie Egg 2023, etc.).
"""

from __future__ import annotations

from pathlib import Path

import grpc

from beebium.client import Beebium
from beebium.client.disassemble import disassemble
from beebium.client.exceptions import BeebiumError
from beebium.client.screen import dump_screen

# DFS ROM for the Acorn 1770 disc controller.
# DNFS ROMs contain an 8271-only DFS and are NOT compatible with the 1770.
DFS_1770_ROM_CANDIDATES = [
    "acorn-dfs_2_26.rom",
]


def find_dfs_1770_rom(roms_dirpath: Path) -> Path | None:
    """Find a 1770 DFS ROM in the ROM directory."""
    for name in DFS_1770_ROM_CANDIDATES:
        candidate = roms_dirpath / name
        if candidate.exists():
            return candidate
    return None


def run_until_or_timeout(bbc: Beebium, predicate, emulated_seconds: float, chunk_seconds: float = 1.0):
    """Run the emulator until predicate() returns True or a cycle budget expires.

    In the single-threaded Tube model, running the host automatically
    ticks the coprocessor via Machine::step(). The predicate is evaluated
    periodically via peek (side-effect-free) while the machine is stopped
    between chunks.

    Args:
        bbc: The Beebium instance.
        predicate: Callable returning True when the desired condition is met.
        emulated_seconds: Maximum BBC-time seconds to run.
        chunk_seconds: Emulated time between predicate checks (default 1.0).

    Returns:
        True if the predicate was satisfied, False on timeout.
    """
    return bbc.run_until_or_timeout(predicate, emulated_seconds, chunk_seconds=chunk_seconds)


def disassemble_region(memory, start: int, length: int) -> list[str]:
    """Disassemble a region of memory, returning formatted lines."""
    data = memory.address.peek.read(start, length)
    return [f"  {line}" for line in disassemble(data, start=start, length=length)]


def dump_diagnostics(bbc: Beebium) -> None:
    """Print comprehensive diagnostics for debugging boot failures.

    Attempts to connect to the coprocessor for additional diagnostics.
    """
    print("\n=== DIAGNOSTICS ===")

    # Host CPU state
    host_pc = None
    try:
        regs = bbc.cpu.registers
        host_pc = regs.pc
        print(f"Host CPU: {regs}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host CPU: error reading - {e}")

    # Host execution state
    try:
        state = bbc.debugger.get_state()
        print(f"Host execution: running={state.is_running}, cycles={state.cycle_count}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host execution: error reading - {e}")

    # Disassemble around host PC + key MOS routines
    if host_pc is not None:
        try:
            dis_start = max(0, host_pc - 16)
            lines = disassemble_region(bbc.memory, dis_start, 64)
            print(f"Host code around PC=${host_pc:04X}:")
            for line in lines:
                addr_str = line.strip().split(":")[0]
                addr_val = int(addr_str.lstrip("$"), 16)
                marker = " >>>" if addr_val == host_pc else ""
                print(f"{line}{marker}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Host disassembly: error - {e}")

        # Disassemble key MOS Tube routines
        for label, addr, length in [
            ("$FB50 (Tube OSRDCH setup)", 0xFB50, 64),
            ("$F720 (Tube OSRDCH handler)", 0xF720, 48),
            ("$DC93 (IRQ1V handler)", 0xDC93, 64),
        ]:
            try:
                lines = disassemble_region(bbc.memory, addr, length)
                print(f"Host code at {label}:")
                for line in lines:
                    print(f"{line}")
            except (BeebiumError, grpc.RpcError) as e:
                print(f"Host code at {label}: error - {e}")

        # Read host ZP $C2 (state variable from OSRDCH loop)
        try:
            c2 = bbc.memory.address.peek[0x00C2]
            print(f"Host ZP $C2 (OSRDCH state): ${c2:02X}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Host ZP $C2: error - {e}")

    # Tube status
    try:
        tube_status = bbc.tube.status
        print(
            f"Tube: enabled={tube_status.enabled}, "
            f"connected={tube_status.coprocessor_connected}, "
            f"type={tube_status.coprocessor_type}, "
            f"clock={tube_status.coprocessor_clock_hz}Hz, "
            f"coprocessor_addr={tube_status.coprocessor_grpc_address}"
        )
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Tube: error reading - {e}")

    # Tube ULA device inspection (side-effect-free)
    try:
        tube_ula_state = bbc.tube_ula.state
        print(tube_ula_state)
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Tube ULA inspection: error - {e}")

    # Disc drive status
    try:
        disc_status = bbc.disc.status
        print(f"Disc controller: {disc_status.controller_type}")
        for drive in disc_status.drives:
            print(
                f"  Drive {drive.drive}: state={drive.state.value}, "
                f"motor={'on' if drive.motor_on else 'off'}, "
                f"track={drive.current_track}, "
                f"disc={drive.disc_name}"
            )
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Disc: error reading - {e}")

    # MOS workspace variables
    try:
        tube_flag = bbc.memory.address.peek[0x027A]
        fs_byte = bbc.memory.address.peek[0x028C]
        exec_handle = bbc.memory.address.peek[0x0257]
        spool_handle = bbc.memory.address.peek[0x0256]
        zp_eb = bbc.memory.address.peek[0x00EB]
        zp_ff = bbc.memory.address.peek[0x00FF]
        print(f"MOS Tube flag (&027A): ${tube_flag:02X}")
        print(f"MOS filing system (&028C): ${fs_byte:02X}")
        print(f"MOS exec handle (&0257): ${exec_handle:02X}")
        print(f"MOS spool handle (&0256): ${spool_handle:02X}")
        print(f"MOS ZP $EB (exec check): ${zp_eb:02X}")
        print(f"MOS ZP $FF (escape flag): ${zp_ff:02X}")
        # OSRDCH vector
        rdch_lo = bbc.memory.address.peek[0x0238]
        rdch_hi = bbc.memory.address.peek[0x0239]
        print(f"OSRDCH vector (&0238): ${rdch_hi:02X}{rdch_lo:02X}")
        # IRQ1V
        irq1v_lo = bbc.memory.address.peek[0x0204]
        irq1v_hi = bbc.memory.address.peek[0x0205]
        print(f"IRQ1V (&0204): ${irq1v_hi:02X}{irq1v_lo:02X}")
        # IRQ2V
        irq2v_lo = bbc.memory.address.peek[0x0206]
        irq2v_hi = bbc.memory.address.peek[0x0207]
        print(f"IRQ2V (&0206): ${irq2v_hi:02X}{irq2v_lo:02X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"MOS workspace: error reading - {e}")

    # Host stack
    if host_pc is not None:
        try:
            sp = bbc.cpu.registers.sp
            stack = bbc.memory.address.peek.read(0x0100, 256)
            stack_top = sp + 1
            if stack_top < 256:
                stack_bytes = stack[stack_top : min(stack_top + 32, 256)]
                hex_str = " ".join(f"{b:02X}" for b in stack_bytes)
                print(f"Host stack (${0x100 + stack_top:04X}+): {hex_str}")
                # Decode return addresses
                i = 0
                while i + 1 < len(stack_bytes):
                    addr = stack_bytes[i] | (stack_bytes[i + 1] << 8)
                    print(f"  Stack ${0x100 + stack_top + i:04X}: ${addr:04X} (return to ${addr + 1:04X}?)")
                    i += 2
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Host stack: error reading - {e}")

    # Host screen
    try:
        print("Host screen:")
        print(dump_screen(bbc))
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host screen: error reading - {e}")

    # Coprocessor diagnostics
    if bbc.tube.coprocessor_connected:
        with bbc.coprocessor() as coprocessor:
            dump_coprocessor_diagnostics(coprocessor)

    print("=== END DIAGNOSTICS ===\n")


def dump_coprocessor_diagnostics(coprocessor: Beebium) -> None:
    """Print coprocessor-side diagnostics."""
    coprocessor_pc = None
    try:
        p_regs = coprocessor.cpu.registers
        coprocessor_pc = p_regs.pc
        print(f"Coprocessor CPU: {p_regs}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Coprocessor CPU: error reading - {e}")

    try:
        p_state = coprocessor.debugger.get_state()
        print(f"Coprocessor execution: running={p_state.is_running}, cycles={p_state.cycle_count}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Coprocessor execution: error reading - {e}")

    # Disassemble around coprocessor PC
    if coprocessor_pc is not None:
        try:
            dis_start = max(0, coprocessor_pc - 16)
            lines = disassemble_region(coprocessor.memory, dis_start, 48)
            print(f"Coprocessor code around PC=${coprocessor_pc:04X}:")
            for line in lines:
                addr_str = line.strip().split(":")[0]
                addr_val = int(addr_str.lstrip("$"), 16)
                marker = " >>>" if addr_val == coprocessor_pc else ""
                print(f"{line}{marker}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Coprocessor disassembly: error - {e}")

    # Coprocessor Tube register status (coprocessor view)
    try:
        pr1s = coprocessor.memory.address.peek[0xFEF8]
        pr1d = coprocessor.memory.address.peek[0xFEF9]
        pr2s = coprocessor.memory.address.peek[0xFEFA]
        pr2d = coprocessor.memory.address.peek[0xFEFB]
        pr3s = coprocessor.memory.address.peek[0xFEFC]
        pr3d = coprocessor.memory.address.peek[0xFEFD]
        pr4s = coprocessor.memory.address.peek[0xFEFE]
        pr4d = coprocessor.memory.address.peek[0xFEFF]
        print("Coprocessor Tube regs (coprocessor view):")
        print(f"  R1: status=${pr1s:02X} data=${pr1d:02X}  [b7={'DATA' if pr1s & 0x80 else 'empty'}]")
        print(f"  R2: status=${pr2s:02X} data=${pr2d:02X}")
        print(f"  R3: status=${pr3s:02X} data=${pr3d:02X}  [b7={'DATA' if pr3s & 0x80 else 'empty'}]")
        print(f"  R4: status=${pr4s:02X} data=${pr4d:02X}  [b7={'DATA' if pr4s & 0x80 else 'empty'}]")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Coprocessor Tube regs: error reading - {e}")

    # Coprocessor MOS workspace
    try:
        p_exec = coprocessor.memory.address.peek[0x0257]
        p_spool = coprocessor.memory.address.peek[0x0256]
        p_tube = coprocessor.memory.address.peek[0x027A]
        p_fs = coprocessor.memory.address.peek[0x028C]
        p_eb = coprocessor.memory.address.peek[0x00EB]
        print(f"Coprocessor exec handle (&0257): ${p_exec:02X}")
        print(f"Coprocessor spool handle (&0256): ${p_spool:02X}")
        print(f"Coprocessor Tube flag (&027A): ${p_tube:02X}")
        print(f"Coprocessor FS (&028C): ${p_fs:02X}")
        print(f"Coprocessor ZP $EB (exec check): ${p_eb:02X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Coprocessor MOS workspace: error reading - {e}")

    # Full coprocessor zero page
    try:
        zp = coprocessor.memory.address.peek.read(0x0000, 256)
        print("Coprocessor zero page:")
        for row in range(16):
            offset = row * 16
            hex_str = " ".join(f"{zp[offset + i]:02X}" for i in range(16))
            print(f"  ${offset:02X}: {hex_str}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Coprocessor ZP: error reading - {e}")

    # Coprocessor vectors and NMIV
    try:
        nmiv = coprocessor.memory.address.peek.read(0x0200, 2)
        nmiv_addr = nmiv[0] | (nmiv[1] << 8)
        irqv = coprocessor.memory.address.peek.read(0x0202, 2)
        irqv_addr = irqv[0] | (irqv[1] << 8)
        nmi_vec = coprocessor.memory.address.peek.read(0xFFFA, 2)
        nmi_addr = nmi_vec[0] | (nmi_vec[1] << 8)
        irq_vec = coprocessor.memory.address.peek.read(0xFFFE, 2)
        irq_addr = irq_vec[0] | (irq_vec[1] << 8)
        print(f"Coprocessor vectors: NMIV=$0200={nmiv_addr:04X}, IRQ1V=$0202={irqv_addr:04X}")
        print(f"Coprocessor HW vectors: NMI=$FFFA={nmi_addr:04X}, IRQ=$FFFE={irq_addr:04X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Coprocessor vectors: error reading - {e}")

    # Coprocessor stack
    try:
        stack = coprocessor.memory.address.peek.read(0x0100, 256)
        sp = p_regs.sp if coprocessor_pc is not None else 0xFF
        stack_top = sp + 1
        if stack_top < 256:
            stack_bytes = stack[stack_top : min(stack_top + 16, 256)]
            hex_str = " ".join(f"{b:02X}" for b in stack_bytes)
            print(f"Coprocessor stack (${0x100 + stack_top:04X}+): {hex_str}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Coprocessor stack: error reading - {e}")
