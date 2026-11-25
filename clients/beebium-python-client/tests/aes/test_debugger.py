# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.


# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# test_debugger.py, tests the debugger's registers, memory, steps, resets and stops through Beebium's client
# version 2.0.0, 20 November 2026.

"""The AES's registers as Beebium's coprocessor debugger shows them (step 4.2; design,
docs/handoff.md at commit e9ac40f; the owner's answers, 18 Nov 2026). A 14-word program loads
every bank from a table, so each expected value is one the ARM itself loaded, not one read
through the debugger's own table. Says what the plugin does; not evidence about an AES."""
import contextlib
import time

import pytest

from pathlib import Path

from aes_host import ROM, Aes, LIMIT_OF_WORDS

from beebium.client._proto import debugger_pb2 as pb

CODE, TABLE = 0x1000, 0x1100
USR, SVC, FIQ, IRQ = 0, 3, 1, 2  # DOC, SW p.1: the mode bits.
I_AND_F = 0x0C000000  # DOC, SW p.1.


# The words, built from their fields as ArmTest.hpp builds them (dp_imm, ldr_imm, block, branch,
# swi), AL condition throughout. The result is checked by what the ARM loads, not by decoding.
def add_pc(rd, offset):
    return 0xE28F0000 | rd << 12 | offset  # ADD Rd,PC,#offset (offset < 256)


def ldmia_wb(rn, regs):
    return 0xE8B00000 | rn << 16 | sum(1 << r for r in regs)  # LDMIA Rn!,{regs}


def str_at(rd, rn):
    return 0xE5800000 | rn << 16 | rd << 12  # STR Rd,[Rn]


def teqp(mode):
    # TEQP PC,#(I_AND_F | mode): the eight bits 0x03 | mode << 6 rotated right by 6. As Rn, R15
    # is the PC alone (DOC, SW p.17), so the PSR becomes exactly I, F and the mode.
    word = 0xE33FF300 | 0x03 | mode << 6
    imm, rot = word & 0xFF, 2 * (word >> 8 & 15)
    assert (imm >> rot | imm << (32 - rot)) & 0xFFFFFFFF == I_AND_F | mode
    return word


def branch(at, target):
    return 0xEA000000 | ((target - at - 8) >> 2 & 0xFFFFFF)


MOV_R0_R0, SWI_0 = 0xE1A00000, 0xEF000000
SVC_ENTRY = CODE + 5 * 4  # The sixth word: the SWI vector at &08 is pointed here.


def program(final_mode):
    words = [
        add_pc(0, TABLE - (CODE + 8)),  # R0 = the table (DOC, SW p.1: PC reads 8 on)
        ldmia_wb(0, [1, 2]),            # R1 = &08, R2 = a branch to SVC_ENTRY
        str_at(2, 1),                   # the SWI vector (no memory protection on the AES)
        ldmia_wb(0, range(1, 15)),      # user R1-R14, in user mode (GO's mode)
        SWI_0,                          # into SVC mode, through the vector just written
        ldmia_wb(0, [13, 14]),          # SVC R13, R14
        teqp(FIQ), MOV_R0_R0,
        ldmia_wb(0, range(10, 15)),     # FIQ R10-R14
        teqp(IRQ), MOV_R0_R0,
        ldmia_wb(0, [13, 14]),          # IRQ R13, R14
        teqp(final_mode),
        branch(CODE + 13 * 4, CODE + 13 * 4),  # spin here
    ]
    assert len(words) <= LIMIT_OF_WORDS
    return words


def value(bank, n):
    return {USR: 0x55000000, SVC: 0x5C000000, FIQ: 0xF1000000, IRQ: 0x1A000000}[bank] | n


TABLE_WORDS = ([0x08, branch(0x08, SVC_ENTRY)] + [value(USR, n) for n in range(1, 15)]
               + [value(SVC, n) for n in (13, 14)] + [value(FIQ, n) for n in range(10, 15)]
               + [value(IRQ, n) for n in (13, 14)])
R0_AT_END = TABLE + 4 * len(TABLE_WORDS)
SPIN = CODE + 13 * 4
NAMES = {USR: "usr", FIQ: "fiq", IRQ: "irq", SVC: "svc"}


def expected(mode):
    """Every register the debugger lists, from the table: what the ARM loaded (DOC, SW p.1 banks:
    FIQ has R10-R14, IRQ and SVC R13-R14)."""
    def seen(n, bank):
        if n == 0:
            return R0_AT_END
        if n >= 13 or (n >= 10 and bank == FIQ):
            return value(bank, n)
        return value(USR, n)
    regs = {f"R{n}": seen(n, mode) for n in range(15)}
    for bank, first in ((USR, 10), (FIQ, 10), (IRQ, 13), (SVC, 13)):
        for n in range(first, 15):
            regs[f"R{n}_{NAMES[bank]}"] = seen(n, bank)
    return regs


@contextlib.contextmanager
def running(final_mode):
    """The program run to its spin, then the AES stopped by the debugger."""
    with Aes() as aes:
        aes.boot()
        aes.poke(TABLE, TABLE_WORDS)
        aes.poke(CODE, program(final_mode))
        aes.start(f"GO {CODE:X}")
        stub = aes.bbc._connection.coprocessor_debugger_stub
        stub.Stop(pb.Empty())
        yield stub


def state(stub):
    return {r.name: r.value for r in stub.GetCpuState(pb.Empty()).registers}


@pytest.mark.parametrize("mode", [USR, FIQ, IRQ, SVC], ids=["user", "fiq", "irq", "svc"])
def test_registers_agree_in_each_mode(mode):
    """4.2 (a), (b): every entry as the ARM loaded it, in each mode; R15 the spinning branch."""
    with running(mode) as stub:
        seen = state(stub)
    want = expected(mode)
    assert {name: seen[name] for name in want} == want
    assert set(seen) == set(want) | {"R15", "PSR"}
    assert seen["R15"] == SPIN
    assert seen["PSR"] == I_AND_F | mode


def test_descriptor():
    """Design 4.1: arm1, 26 address bits, R15 the program counter, PSR the flags, IRQ and FIQ."""
    with running(SVC) as stub:
        d = stub.GetCpuDescriptor(pb.Empty())
    assert (d.family, d.address_bits, d.little_endian) == ("arm1", 26, True)
    roles = {r.name: r.role for r in d.registers}
    assert roles["R15"] == pb.PROGRAM_COUNTER
    assert roles["PSR"] == pb.FLAGS
    assert list(d.signals) == ["IRQ", "FIQ"]


def test_psr_write_ignored_and_registers_set():
    """4.2 (c), (e): a PSR write changes nothing and the read-back is the true PSR; R0-R14 and
    a bank register set and read back."""
    with running(SVC) as stub:
        request = pb.CpuState()
        request.registers.add(name="PSR", value=0)
        for n in range(15):
            request.registers.add(name=f"R{n}", value=0x7000 + n)
        request.registers.add(name="R13_irq", value=0x1234)
        back = {r.name: r.value for r in stub.SetCpuState(request).registers}
        assert back["PSR"] == I_AND_F | SVC
        assert all(back[f"R{n}"] == 0x7000 + n for n in range(15))
        assert back["R13_svc"] == 0x700D and back["R13_irq"] == 0x1234
        assert state(stub) == back


# Memory (step 4.3; design 4.1). The core's own view is the ARM's: the Executive's MEMORY command,
# which the ARM runs; under the boot latch, the ROM file itself (DOC, HW pp.34-35).
ROM_AT, RAM_PATTERN_AT = 0x3000000, 0x2000


def debugger_bytes(stub, address, length):
    """The same bytes three ways: PeekMemory, ReadMemory, and the region that holds them."""
    peeked = stub.PeekMemory(pb.PeekMemoryRequest(address=address, length=length)).data
    read = stub.ReadMemory(pb.ReadMemoryRequest(address=address, length=length)).data
    name = "rom" if address >= ROM_AT else "ram"
    in_region = stub.PeekRegion(pb.RegionAccessRequest(region_name=name, address=address, length=length)).data
    return peeked, read, in_region


@pytest.mark.parametrize("ram_mb", [2, 4])
def test_regions_listed(ram_mb):
    """4.3 (a): ram, tube and rom, at their addresses, none with side effects."""
    with Aes(settings=[f"ram={ram_mb}"], start_stopped=True) as aes:
        stub = aes.bbc._connection.coprocessor_debugger_stub
        regions = {r.name: r for r in stub.GetMemoryRegions(pb.GetMemoryRegionsRequest()).regions}
    assert {n: (r.base_address, r.size) for n, r in regions.items()} == {
        "ram": (0, ram_mb << 20), "tube": (0x1000000, 32), "rom": (ROM_AT, 0x4000)}
    assert not any(r.has_side_effects for r in regions.values())
    assert [n for n, r in regions.items() if r.writable] == ["ram"]


def test_memory_under_the_latch():
    """4.3 (b): before the ARM has run, the low addresses read the ROM; the ram region is RAM."""
    rom = Path(ROM).read_bytes()
    with Aes(start_stopped=True) as aes:
        stub = aes.bbc._connection.coprocessor_debugger_stub
        low = (stub.PeekMemory(pb.PeekMemoryRequest(address=0, length=64)).data,
               stub.ReadMemory(pb.ReadMemoryRequest(address=0, length=64)).data)
        high = debugger_bytes(stub, ROM_AT, 64)
        ram = stub.PeekRegion(pb.RegionAccessRequest(region_name="ram", address=0, length=64)).data
    assert low == (rom[:64], rom[:64])
    assert high == (rom[:64],) * 3
    assert ram == bytes(64)  # ram.power_on's default, zero (USER: a convenience)


def test_memory_after_the_latch():
    """4.3 (b): after boot, RAM and ROM as the ARM's own MEMORY command shows them."""
    pattern = [0x11223344 + 0x01010101 * i for i in range(8)]
    with Aes() as aes:
        aes.boot()
        aes.poke(RAM_PATTERN_AT, pattern)
        stub = aes.bbc._connection.coprocessor_debugger_stub
        for address in (RAM_PATTERN_AT, 0x0, ROM_AT):
            seen = aes.memory(address, 32)
            assert debugger_bytes(stub, address, 32) == (seen,) * 3, f"{address:X}"
    assert seen == Path(ROM).read_bytes()[:32]


# Steps and resets (step 4.4a; design 4.1; OI-42, owner, 18 Nov 2026).
LOOP = [0xE2811001, branch(CODE + 4, CODE)]  # ADD R1,R1,#1; B the ADD
HOST_TUBE_STATUS, SET_P, CLEAR_P = 0xFEE0, 0xA0, 0x20  # DOC, AN004 pp.11-13: R1 status, P


def stopped_in_loop(aes):
    aes.boot()
    aes.poke(CODE, LOOP)
    aes.start(f"GO {CODE:X}")
    stub = aes.bbc._connection.coprocessor_debugger_stub
    stub.Stop(pb.Empty())
    return stub


def test_steps():
    """4.4a (a): each step runs one instruction: R1 rises by one each ADD, R15 alternates,
    sequence by one, cycle_count by the step's cycles."""
    with Aes() as aes:
        stub = stopped_in_loop(aes)
        before = state(stub)
        if before["R15"] != CODE:
            stub.StepInstruction(pb.StepRequest(count=1))
            before = state(stub)
        for i in range(1, 7):
            old = stub.GetState(pb.Empty())
            step = stub.StepInstruction(pb.StepRequest(count=1))
            new = stub.GetState(pb.Empty())
            now = state(stub)
            assert step.success and step.instructions_executed == 1
            assert new.sequence == old.sequence + 1
            assert step.cycles_executed > 0 and new.cycle_count == old.cycle_count + step.cycles_executed
            assert now["R15"] == (CODE + 4 if i % 2 else CODE)
            assert now["R1"] == (before["R1"] + (i + 1) // 2) & 0xFFFFFFFF


def test_reset():
    """4.4a (b): Reset leaves the ARM at the reset vector in SVC mode, the latch set, RAM kept."""
    rom = Path(ROM).read_bytes()
    with Aes() as aes:
        stub = stopped_in_loop(aes)
        assert stub.Reset(pb.Empty()).success
        now = state(stub)
        low = stub.PeekMemory(pb.PeekMemoryRequest(address=0, length=16)).data
        loop = stub.PeekRegion(pb.RegionAccessRequest(region_name="ram", address=CODE, length=8)).data
    assert now["R15"] == 0 and now["PSR"] & 0x0C000003 == 0x0C000003
    assert low == rom[:16]
    assert loop == b"".join(w.to_bytes(4, "little") for w in LOOP)


def test_p_ending_in_a_pause_restarts():
    """4.4a (c), OI-42: P set and cleared by the host while the ARM is stopped restarts it at
    once: before it runs again, the latch is set and its next instruction is the reset entry."""
    rom = Path(ROM).read_bytes()
    with Aes() as aes:
        stub = stopped_in_loop(aes)
        host = aes.bbc.memory.address.bus
        host.write(HOST_TUBE_STATUS, bytes([SET_P]))
        aes.settle(0.001)
        host.write(HOST_TUBE_STATUS, bytes([CLEAR_P]))
        aes.settle(0.001)
        assert not stub.GetState(pb.Empty()).is_running
        low = stub.PeekMemory(pb.PeekMemoryRequest(address=0, length=16)).data
        stub.StepInstruction(pb.StepRequest(count=1))
        now = state(stub)
    assert low == rom[:16]
    assert now["R15"] == 0 and now["PSR"] & 0x0C000003 == 0x0C000003


# Breakpoints and watchpoints (step 4.4b; design 4.1). The AES runs only while the host does
# (SRC, CoprocessorDebuggerAdapter.hpp: a Run with the host stopped is refused), so the host
# runs freely while the AES is run to its next stop.
STORE_LOOP = [0xE3A02B48,  # MOV R2,#&12000 (&48 rotated right by 22), above 64 KB
              0xE5821000,  # STR R1,[R2]
              0xE2811001,  # ADD R1,R1,#1
              branch(CODE + 12, CODE + 4)]


def stopped_by_itself(stub, seconds=5.0):
    """True if the AES stops within the time, as a breakpoint or watchpoint stops it."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if not stub.GetState(pb.Empty()).is_running:
            return True
        time.sleep(0.01)
    return False


@contextlib.contextmanager
def host_running(aes):
    aes.bbc.debugger.run()
    try:
        yield
    finally:
        aes.bbc.debugger.stop()


def run_aes(stub):
    done = stub.Run(pb.RunRequest())
    assert done.success, done.error


def started(aes, program):
    aes.boot()
    aes.poke(CODE, program)
    aes.start(f"GO {CODE:X}")
    return aes.bbc._connection.coprocessor_debugger_stub


def test_breakpoints():
    """4.4b (a), (b), (c), (e): set while running, it stops before the instruction; Run steps
    over it once and it stops on the next pass; deleted while paused, it stops nothing."""
    with Aes() as aes:
        stub = started(aes, LOOP)
        with host_running(aes):
            added = stub.AddBreakpoint(pb.AddBreakpointRequest(start_address=CODE + 4))
            assert added.success
            assert stopped_by_itself(stub)
        first = state(stub)
        with host_running(aes):
            run_aes(stub)
            assert stopped_by_itself(stub)
        second = state(stub)
        stub.RemoveBreakpoint(pb.RemoveBreakpointRequest(id=added.id))
        with host_running(aes):
            run_aes(stub)
            assert not stopped_by_itself(stub, seconds=0.5)
        stub.Stop(pb.Empty())
    assert first["R15"] == second["R15"] == CODE + 4
    assert second["R1"] == (first["R1"] + 1) & 0xFFFFFFFF


def test_watchpoint_above_64k():
    """4.4b (d): a write watchpoint at &12000 stops the AES after the STR, R15 at the next
    instruction; one at &2000, the address cut to 16 bits, never does."""
    with Aes() as aes:
        stub = started(aes, STORE_LOOP)
        stub.Stop(pb.Empty())
        stub.AddWatchpoint(pb.AddWatchpointRequest(start_address=0x2000, end_address=0x2004,
                                                   type=pb.WATCHPOINT_WRITE))
        with host_running(aes):
            run_aes(stub)
            assert not stopped_by_itself(stub, seconds=0.5)
        stub.ClearWatchpoints(pb.Empty())
        stub.AddWatchpoint(pb.AddWatchpointRequest(start_address=0x12000, end_address=0x12001,
                                                   type=pb.WATCHPOINT_WRITE))
        with host_running(aes):
            assert stopped_by_itself(stub)
        seen = state(stub)
    assert seen["R15"] == CODE + 8
