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

"""What a REPL shows for the client's objects.

A snapshot prints as its value (``A=1B X=00 ...``); a live part of the client
prints as one line in angle brackets naming what it is and, where a cheap
read gives it, the state it is in. None prints as ``<... object at 0x...>``,
and none raises when the server has gone.
"""

from __future__ import annotations

import re

import pytest

from beebium.client import Beebium
from beebium.client._proto import debugger_pb2
from beebium.client.cpu import Registers, StatusRegister

DEFAULT_OBJECT_REPR = re.compile(r" object at 0x[0-9a-fA-F]+>")

SIX_502_FLAG_NAMES = ["C", "Z", "I", "D", "B", "", "V", "N"]


def six_502_descriptor() -> debugger_pb2.CpuDescriptor:
    descriptor = debugger_pb2.CpuDescriptor(family="6502")
    for name, width_bits in (("A", 8), ("X", 8), ("Y", 8), ("SP", 8), ("PC", 16)):
        descriptor.registers.add(name=name, width_bits=width_bits)
    descriptor.registers.add(
        name="P", width_bits=8, role=debugger_pb2.FLAGS, flag_names=SIX_502_FLAG_NAMES
    )
    return descriptor


def assert_one_line_facade(obj: object) -> str:
    text = repr(obj)
    assert "\n" not in text
    assert not DEFAULT_OBJECT_REPR.search(text), text
    assert text.startswith(f"<{type(obj).__name__}"), text
    assert text.endswith(">"), text
    return text


class TestSnapshots:
    """Value snapshots print as their value (pure -- no server)."""

    def test_registers_repr_is_the_register_line(self):
        regs = Registers(
            six_502_descriptor(),
            {"A": 0x1B, "X": 0x00, "Y": 0x0A, "SP": 0xF1, "PC": 0xEAFD, "P": 0x32},
        )
        assert repr(regs) == "A=1B X=00 Y=0A SP=F1 PC=EAFD P=32 [nv-BdiZc]"

    def test_status_register_repr_names_the_register(self):
        assert repr(StatusRegister(0x32)) == "P=32 [nv-BdiZc]"

    def test_status_register_from_registers_uses_the_descriptor_name(self):
        descriptor = debugger_pb2.CpuDescriptor(family="other")
        descriptor.registers.add(
            name="F", width_bits=8, role=debugger_pb2.FLAGS, flag_names=SIX_502_FLAG_NAMES
        )
        regs = Registers(descriptor, {"F": 0x01})
        assert repr(regs.status) == "F=01 [nv-bdizC]"

    def test_status_register_str_is_unchanged(self):
        assert str(StatusRegister(0x32)) == "nv-BdiZc"


FACADE_NAMES = [
    "debugger",
    "cpu",
    "keyboard",
    "controllers",
    "video",
    "audio",
    "extensions",
    "memory",
    "basic",
    "system_via",
    "user_via",
    "crtc",
    "real_time_clock",
    "video_ula",
    "addressable_latch",
    "sound",
    "tube_ula",
    "system",
    "cassette",
    "disc",
    "econet",
    "serial",
    "transport",
    "indicators",
    "sideways",
    "extension_ui",
    "tube",
]


class TestLiveObjects:
    """Live parts of a running client, against one server for the module."""

    def test_beebium_names_target_model_and_machine(self, bbc_shared: Beebium):
        text = assert_one_line_facade(bbc_shared)
        identity = bbc_shared.system.identity
        assert bbc_shared.target in text
        assert identity.model_name in text
        assert repr(identity.name) in text

    @pytest.mark.parametrize("name", FACADE_NAMES)
    def test_every_facade_is_one_line(self, bbc_shared: Beebium, name: str):
        assert_one_line_facade(getattr(bbc_shared, name))

    def test_sub_objects_are_one_line(self, bbc_shared: Beebium):
        memory = bbc_shared.memory
        region_name = memory.regions[0].name
        for obj in (
            memory.address,
            memory.region(region_name),
            bbc_shared.crtc.registers,
            bbc_shared.video_ula.palette,
            bbc_shared.disc.drive(0),
        ):
            assert_one_line_facade(obj)

    def test_debugger_shows_its_execution_state(self, bbc_shared: Beebium):
        debugger = bbc_shared.debugger
        debugger.stop()
        debugger.add_breakpoint(0xFFEE)
        try:
            state = debugger.get_state()
            text = repr(debugger)
            assert "stopped" in text
            assert f"cycles={state.cycle_count}" in text
            assert "breakpoints=1" in text
            assert "watchpoints=0" in text
        finally:
            debugger.clear_breakpoints()
            debugger.run()
        assert "running" in repr(debugger)

    def test_cpu_shows_family_and_registers(self, bbc_shared: Beebium):
        bbc_shared.debugger.stop()
        try:
            text = repr(bbc_shared.cpu)
            assert text == f"<CPU 6502 {bbc_shared.cpu.registers}>"
        finally:
            bbc_shared.debugger.run()

    def test_memory_shows_machine_type_and_regions(self, bbc_shared: Beebium):
        memory = bbc_shared.memory
        text = repr(memory)
        assert memory.machine_type in text
        for info in memory.regions:
            assert info.name in text

    def test_accessors_say_which_and_where(self, bbc_shared: Beebium):
        memory = bbc_shared.memory
        region_name = memory.regions[0].name
        region = memory.region(region_name)
        assert repr(memory.address.peek) == "<PeekMemoryAccessor address space, peek>"
        assert repr(memory.address.bus) == "<BusMemoryAccessor address space, bus>"
        assert repr(memory.address.peek.with_pc(0xC000)) == (
            "<PCContextReader address space, peek, as from PC=C000>"
        )
        assert repr(memory.address.bus.with_pc(0xC000)) == (
            "<PCContextAccessor address space, bus, as from PC=C000>"
        )
        assert repr(region.peek) == f"<RegionPeekAccessor region {region_name!r}, peek>"
        assert repr(region.bus) == f"<RegionBusAccessor region {region_name!r}, bus>"
        assert repr(memory.address.bus.cast("<H")) == (
            "<TypedMemoryAccessor '<H' over address space, bus>"
        )
        assert repr(memory.address.peek.cast("<H")) == (
            "<TypedMemoryReader '<H' over address space, peek>"
        )

    def test_system_names_model_and_machine(self, bbc_shared: Beebium):
        identity = bbc_shared.system.identity
        text = repr(bbc_shared.system)
        assert identity.model_name in text
        assert repr(identity.name) in text

    def test_basic_and_keyboard_name_the_target(self, bbc_shared: Beebium):
        assert bbc_shared.target in repr(bbc_shared.basic)
        assert bbc_shared.target in repr(bbc_shared.keyboard)


class TestAfterTheServerHasGone:
    def test_live_reprs_say_unavailable_rather_than_raise(self, bbc: Beebium):
        debugger, cpu, memory, system = bbc.debugger, bbc.cpu, bbc.memory, bbc.system
        bbc.close()
        for obj in (bbc, debugger, cpu, memory, system):
            text = assert_one_line_facade(obj)
            assert "unavailable" in text
