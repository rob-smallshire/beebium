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
# aes_host.py, drives a Beebium server with the plugin, for the Python tests
# version 2.0.0, 20 November 2026.

"""Drives the AES from the BBC's side, in a real Beebium server, for the server tests.

R2, R3 and R4 are read from Beebium's Tube trace, which records what each side put on the chip.
The trace leaves out R1 (seen 1 Nov 2026; notes/beebium-issues/). So text is taken at the host's
OSWRCH vector, and host-to-parasite R1 bytes at the host's own store. Nothing here is evidence
of what an AES does (CLAUDE.md, the clean-room rule).
"""
import hashlib
import os
import re
import shutil
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client._proto import debugger_pb2
from beebium.client.screen import read_mode7_cells

# Set by configure(), from conftest.py, before any test imports them.
SERVER = ""
# The Executive ROM the plugin ships, unless BEEBIUM_AES_ROM_FILEPATH names another image.
PLUGIN_FOLDER = Path(__file__).resolve().parents[4] / "src" / "extensions" / "acorn-aes-coprocessor"
BUNDLED_ROM = PLUGIN_FOLDER / "roms" / "acorn-aes-executive_1_00.rom"
ROM = os.environ.get("BEEBIUM_AES_ROM_FILEPATH") or str(BUNDLED_ROM)
TUBE_LOG_DIRPATH = ""
PROJECT = os.environ.get("AES_PROJECT", "")
ROMS = Path()
DFS = ROMS / "acorn-dfs_2_26.rom"
FACTS_FOLDER = PLUGIN_FOLDER / "tests" / "rom-facts"
# DOC, Beebium docs/cli.md: full speed. Emulated time is what the tests count (INF, no test
# here reads wall time). Given to each server these tests launch, not set in os.environ, where
# every server the rest of the pytest run launches would inherit it.
SERVER_ENV = {"BEEBIUM_NO_PACING": "1"}

# First 8 hex digits of each frozen disc's SHA-1, from notes/real-aes/real-aes-run-sheet.md. Under AES_PROJECT.
FROZEN = {
    "AESTESTS": ("tests/AESTESTS.ssd", "79612e37"),
    "AESTIER2": ("tests/AESTIER2.ssd", "447bfa4f"),
    "AESTIER3": ("tests/AESTIER3.ssd", "20cfd33f"),
    "AESTIER4": ("tests/AESTIER4.ssd", "446fb8c3"),
    "AESTIER6": ("tests/AESTIER6.ssd", "e83ef8a3"),
    "AESTIER7": ("tests/AESTIER7.ssd", "a80839ae"),
    "arm_tak_1": ("own-work/tak/test-discs/arm_tak_1.ssd", "9ba5529f"),
    "arm_tak_2": ("own-work/tak/test-discs/arm_tak_2.ssd", "77d733f2"),
    "arm_lisp_3a": ("own-work/tak/test-discs/arm_lisp_3a.ssd", "135223ac"),
    "arm_lisp_3b": ("own-work/tak/test-discs/arm_lisp_3b.ssd", "d7ed7c93"),
}


def configure(server_filepath, rom_dirpath):
    """The server under test, which may be None for none, and the host's ROM folder."""
    global SERVER, TUBE_LOG_DIRPATH, ROMS, DFS
    ROMS = Path(rom_dirpath)
    DFS = ROMS / "acorn-dfs_2_26.rom"
    if server_filepath is None:
        return
    SERVER = str(server_filepath)
    # The server is <build>/src/server/beebium-model-b; the plugin's tools are built beside it.
    tools_dirpath = Path(server_filepath).parent.parent / "extensions" / "acorn-aes-coprocessor"
    TUBE_LOG_DIRPATH = str(tools_dirpath / "tube-log")


def launch(extra_args, extra_env=None, **options):
    """A server for these tests: the one under test, unpaced, given time to start."""
    return Beebium.launch(server=SERVER, extra_args=extra_args, startup_timeout=20.0,
                          extra_env={**SERVER_ENV, **(extra_env or {})}, **options)


def missing(why):
    """A test that cannot run says why. AES_REQUIRE_ALL makes that a failure, on the model of
    Beebium's BEEBIUM_REQUIRE_MDNS (SRC). Skips rather than errors, as Beebium's fixtures do."""
    if "AES_REQUIRE_ALL" in os.environ:
        pytest.fail(f"skipped under AES_REQUIRE_ALL: {why}")
    pytest.skip(why)


def sha1(path):
    return hashlib.sha1(Path(path).read_bytes()).hexdigest()


def read_facts(path):
    """Everything after the first = is the value, spaces included: as RomCheck.hpp reads them."""
    lines = path.read_text(encoding="latin-1").split("\n")
    return dict(line.split("=", 1) for line in lines if "=" in line and not line.startswith("#"))


def rom_is_readable():
    return bool(ROM) and Path(ROM).is_file()


def load_facts():
    path = FACTS_FOLDER / f"{sha1(ROM)}.txt" if rom_is_readable() else None
    return read_facts(path) if path and path.is_file() else {}


FACTS = load_facts()


def rom_problem():
    """Why tests tied to a ROM cannot run on this one; empty if they can. As RomCheck.hpp."""
    if not rom_is_readable():
        return "no ROM: BEEBIUM_AES_ROM_FILEPATH is not set, or names no file"
    return "" if FACTS else f"no facts file for this ROM, SHA-1 {sha1(ROM)}"


def need_rom():
    if rom_problem():
        missing(rom_problem())


def fact(key):
    """A value from the ROM's facts file. A test reaching this has its ROM, or skips here; it skips
    too when the file has no such key (the ROM lacks it, or it is not established for that ROM)."""
    need_rom()
    if key not in FACTS:
        missing(f"this ROM's facts file has no {key}")
    return FACTS[key]


def start_call():
    """The R2 call the ARM makes at the end of every start: the facts file's start_call."""
    return [int(byte) for byte in fact("start_call").split()]


# SRC, ROM at prompt_at, Super1:390. The screen's cells keep 7 bits (DOC, Beebium client screen.py).
PROMPT = bytes(int(byte) for byte in FACTS.get("prompt_bytes", "").split())
PROMPT_CELLS = bytes(b & 0x7F for b in PROMPT)


def frozen_path(name):
    path = Path(PROJECT) / FROZEN[name][0]
    if not PROJECT or not path.is_file():
        missing(f"frozen test disc {name} is not shipped")
    return path


def copy_frozen(name, folder):
    """A frozen disc only ever runs as a copy, with its hash checked first (CLAUDE.md)."""
    original, prefix = frozen_path(name), FROZEN[name][1]
    assert sha1(original).startswith(prefix), f"{original} is not the frozen image"
    copy = Path(folder) / f"{name}.ssd"
    shutil.copyfile(original, copy)
    copy.chmod(0o644)
    return copy


def frozen_unchanged(name):
    return sha1(frozen_path(name)).startswith(FROZEN[name][1])


def expected_banner(ram_mb=4):
    """The banner's words for this ROM and RAM size, as RomCheck.hpp's expected_banner_words
    makes them: the ROM's bytes from the facts file's banner_at up to the NUL, then the size in
    KB (SRC, ROM bytes at banner_at; the size is the setting ram)."""
    need_rom()
    image = Path(ROM).read_bytes()
    at = int(FACTS["banner_at"], 0)
    return image[at:image.index(0, at)].decode("latin-1") + f"{ram_mb * 1024}K"


class Wire:
    """What each side has written to the Tube. DOC, TubeInspection.hpp: tag bits 7-4 register,
    bit 3 direction (0 host to parasite, 1 parasite to host), bit 2 side (0 host, 1 coprocessor).
    The trace is a ring of 1024, so it is read often, and `lost` says if any were missed. Asking
    what a side wrote refuses a trace with a gap: a gap reads as a short reply, or hides a call
    (seen 1 Nov 2026, OSWORD 14). The language copy overruns the ring at the usual reads."""

    def __init__(self, bbc):
        self.bbc = bbc
        self.entries = []
        self.lost = 0
        self.seen = 0

    def _state(self):
        # The client's TubeUlaState leaves the trace out; the RPC carries it.
        return self.bbc.tube_ula._stub.GetTubeState(debugger_pb2.GetTubeStateRequest())

    def update(self):
        state = self._state()
        fresh = state.trace_total_count - self.seen
        self.seen = state.trace_total_count
        held = list(state.trace)
        self.lost += max(0, fresh - len(held))
        for entry in held[-fresh:] if fresh else []:
            self.entries.append((entry.tag >> 4, bool(entry.tag & 8), bool(entry.tag & 4), entry.value))

    def clear(self):
        self.update()
        self.entries = []
        self.lost = 0

    def parasite_wrote(self, register):
        assert self.lost == 0, f"{self.lost} trace entries lost"
        return [v for reg, p2h, copro, v in self.entries if reg == register and p2h and copro]

    def host_wrote(self, register):
        assert self.lost == 0, f"{self.lost} trace entries lost"
        return [v for reg, p2h, copro, v in self.entries if reg == register and not p2h and not copro]

    def after(self, register):
        """A new Wire view of everything from the first host write on `register` onwards."""
        rest = Wire.__new__(Wire)
        first = next(i for i, e in enumerate(self.entries) if e[0] == register and not e[1] and not e[2])
        rest.entries = self.entries[first:]
        rest.lost = self.lost
        return rest


def calls_on_r2(stream):
    """Splits what the parasite wrote on R2 into calls, by the lengths in notes/test-specs.md
    1.1 (DOC, AN pp.22-25; SRC). A wrong length shows as an unknown command or a short call."""
    calls, at = [], 0

    def until_cr(start):
        return stream.index(0x0D, start) + 1

    while at < len(stream):
        command = stream[at]
        if command == 0x00:
            end = at + 1
        elif command == 0x02:
            end = until_cr(at + 1)
        elif command == 0x04:
            end = at + 3
        elif command == 0x06:
            end = at + 4
        elif command == 0x08:
            count_out = stream[at + 2]
            end = at + 3 + count_out + 1
        elif command == 0x0C:
            end = at + 7
        elif command == 0x0E:
            end = at + 2
        elif command == 0x10:
            end = at + 3
        elif command == 0x12:
            end = at + 3 if stream[at + 1] == 0 else until_cr(at + 2)
        elif command == 0x14:
            end = until_cr(at + 17) + 1
        elif command == 0x16:
            end = at + 15
        else:
            raise AssertionError(f"unknown R2 command {command:02X} at {at} in {bytes(stream).hex()}")
        calls.append(list(stream[at:end]))
        at = end
    return calls


def conversation(wire):
    """The R2 traffic as (call, what the host wrote back after it), in order."""
    stream = wire.parasite_wrote(2)
    calls = calls_on_r2(stream)
    ends = []
    total = 0
    for call in calls:
        total += len(call)
        ends.append(total)
    replies = [[] for _ in calls]
    sent = 0
    for reg, p2h, copro, value in wire.entries:
        if reg == 2 and p2h and copro:
            sent += 1
        elif reg == 2 and not p2h and not copro and sent:
            done = sum(1 for end in ends if end <= sent) - 1
            replies[done].append(value)
    return list(zip(calls, replies))


def decode(word, at):
    """An ARM word as text, written from the bit fields in DOC, SW pp.8-11 (data processing),
    p.19 (single transfer), p.13 (branch), p.26 (SWI), p.36 (undefined). It knows only the forms
    the tests type, and fails on any other, so a wrong hex word cannot pass as its meaning."""
    conditions = {0xE: "", 0x1: "NE"}
    condition = conditions[word >> 28]
    kind = word >> 24 & 15
    if word == UNDEFINED:
        return "undefined"
    if kind == 15:
        return f"SWI{condition} &{word & 0xFFFFFF:X}"
    if kind == 10:
        offset = word & 0xFFFFFF
        offset = offset - (1 << 24) if offset >> 23 else offset
        return f"B{condition} &{at + 8 + offset * 4:X}"
    if word >> 20 & 0xFF == 0x58 and word >> 16 & 15 == 15:  # U set, a word, PC as base.
        return f"STR R{word >> 12 & 15},[PC,#{word & 0xFFF}]"
    if word >> 20 & 0xFF == 0x5C:  # U set, a byte.
        return f"STRB R{word >> 12 & 15},[R{word >> 16 & 15},#{word & 0xFFF}]"
    assert word >> 26 & 3 == 0, f"{word:08X}"
    immediate, operation, set_flags = word >> 25 & 1, word >> 21 & 15, word >> 20 & 1
    rn, rd, operand = word >> 16 & 15, word >> 12 & 15, word & 0xFFF
    if immediate:
        rotate = 2 * (operand >> 8)
        value = ((operand & 255) >> rotate | (operand & 255) << (32 - rotate)) & 0xFFFFFFFF
        second = f"#&{value:X}"
    else:
        assert operand >> 4 == 0, f"{word:08X}"
        second = f"R{operand}"
    if operation == 13 and not set_flags:
        return f"MOV R{rd},{second}"
    if operation == 4 and not set_flags:
        return f"ADD R{rd},R{rn},{second}"
    if operation == 10 and set_flags:
        return f"CMP R{rn},{second}"
    if operation == 9 and set_flags and rd == 15 and rn == 15:
        return f"TEQP PC,{second}"
    raise AssertionError(f"{word:08X}")


# 6502, poked into the host after boot: stores each OSWRCH byte, then carries on through the old
# vector. Free RAM below the Mode 7 screen, with no language running in the host (INF).
WRCHV = 0x020E
HOOK, CAPTURE = 0x0C00, 0x3000
STORE_LOW, STORE_HIGH = HOOK + 1, HOOK + 2
# The same for the host's "send byte in A via R1" store, a STA &FEE1 at &06C1 in the host's RAM
# (INF, the host's Tube code listing Host230.lst, from mdfs.net), turned into a JSR.
R1_STORE, R1_HOOK, R1_CAPTURE = 0x06C1, 0x0C20, 0x5000
R1_LOW, R1_HIGH = R1_HOOK + 4, R1_HOOK + 5

UNDEFINED = 0xEE000000  # DOC, SW p.36; SRC, Super1 HELP: the Executive's own "illegal instruction".
LIMIT_OF_WORDS = 14  # USER, 1 Nov 2026: of code, typed at once. The stop word and data do not count.


class Aes:
    """A fresh machine: a new server for each fresh boot (Break is not one, acceptance suite)."""

    def __init__(self, disc=None, disc1=None, settings=(), start_stopped=False, tube_log=None,
                 log_build=True, adfs=False, hard_disc=None):
        """tube_log names a file for the counting wrapper, which is in the test build only:
        log_build=False asks the ordinary plugin, to show that it writes none."""
        need_rom()
        self.args = ["--fdc", "acorn-1770", "--sideways", f"slot=14:type=rom:image={DFS}"]
        if adfs:  # Below DFS, so DFS is still the filing system at boot (DOC, profile host.model).
            self.args += ["--sideways", f"slot=13:type=rom:image={ROMS / 'acorn-adfs_1_30.rom'}"]
        if hard_disc:  # DOC, Beebium cli.md.
            self.args += ["--acorn-scsi", "--scsi-hdd", f"scsi-id=0:image={hard_disc}"]
        self.tube_log = tube_log
        if tube_log and log_build:
            self.args += ["--extension-dir", TUBE_LOG_DIRPATH]
        if disc:
            self.args += ["--floppy", f"0:{disc}"]
        if disc1:
            self.args += ["--floppy", f"1:{disc1}"]
        self.args += ["--tube-aes", ":".join([f"rom={ROM}", *settings])]
        self.start_stopped = start_stopped
        self.chunk = 0.02  # Emulated seconds between trace reads. The ring holds 1024 entries.

    def __enter__(self):
        extra_env = {"AES_TUBE_LOG": str(self.tube_log)} if self.tube_log else {}
        self.launch = launch(self.args, extra_env, start_stopped=self.start_stopped)
        self.bbc = self.launch.__enter__()
        self.wire = Wire(self.bbc)
        return self

    def __exit__(self, *args):
        return self.launch.__exit__(*args)

    def run(self, done, seconds, chunk=None):
        def check():
            self.wire.update()
            return done()
        return self.bbc.run_until_or_timeout(check, emulated_seconds=seconds, chunk_seconds=chunk or self.chunk)

    def rows(self):
        return [row.rstrip(b" ") for row in read_mode7_cells(self.bbc)]

    def at_prompt(self):
        rows = [row for row in self.rows() if row]
        return bool(rows) and rows[-1] == PROMPT_CELLS

    def screen(self):
        return "\n".join(row.decode("latin-1") for row in self.rows())

    def boot(self, seconds=20, hooks=True):
        """hooks=False leaves the host untouched, for long runs: the capture stores have no end
        check, and they add host time to every OSWRCH."""
        assert self.run(self.at_prompt, seconds), self.screen()
        self.settle()
        if hooks:
            self.hook_oswrch()
        return self

    def hook_oswrch(self):
        host = self.bbc.memory.address
        old = bytes(host.peek[WRCHV:WRCHV + 2])
        host.bus.write(HOOK, bytes([0x8D, CAPTURE & 255, CAPTURE >> 8,
                                    0xEE, STORE_LOW & 255, STORE_LOW >> 8, 0xD0, 0x03,
                                    0xEE, STORE_HIGH & 255, STORE_HIGH >> 8, 0x4C, *old]))
        host.bus.write(WRCHV, HOOK.to_bytes(2, "little"))
        assert bytes(host.peek[R1_STORE:R1_STORE + 3]) == bytes([0x8D, 0xE1, 0xFE])
        host.bus.write(R1_HOOK, bytes([0x8D, 0xE1, 0xFE, 0x8D, R1_CAPTURE & 255, R1_CAPTURE >> 8,
                                       0xEE, R1_LOW & 255, R1_LOW >> 8, 0xD0, 0x03,
                                       0xEE, R1_HIGH & 255, R1_HIGH >> 8, 0x60]))
        host.bus.write(R1_STORE, bytes([0x20, R1_HOOK & 255, R1_HOOK >> 8]))

    def forget_text(self):
        self.bbc.debugger.ensure_stopped()
        self.bbc.memory.address.bus.write(STORE_LOW, CAPTURE.to_bytes(2, "little"))

    def text(self):
        """Everything OSWRCH has seen since the last forget_text, the ARM's bytes and the host's
        own alike. Read while stopped."""
        host = self.bbc.memory.address.peek
        end = host[STORE_LOW] | host[STORE_HIGH] << 8
        return bytes(host[CAPTURE:end]).decode("latin-1")

    def host_r1_bytes(self):
        """Every byte the host has put in R1 since the boot. Read while stopped."""
        host = self.bbc.memory.address.peek
        end = host[R1_LOW] | host[R1_HIGH] << 8
        return list(bytes(host[R1_CAPTURE:end]))

    def settle(self, seconds=0.2):
        self.run(lambda: False, seconds)

    def idle(self):
        return self.bbc.keyboard.typing_status().idle

    def type(self, text):
        self.bbc.keyboard.type(text)

    def command(self, line, seconds=30):
        """Types a line at A* and runs to the next A*. Returns what the ARM sent to the screen."""
        self.wire.clear()
        self.forget_text()
        self.type(line + "\r")
        assert self.run(lambda: self.idle() and self.at_prompt(), seconds), f"{line}:\n{self.screen()}"
        self.settle()
        return self.text()

    def poke(self, address, words):
        """Through the Executive's MemoryA, as a user would type it (SRC, Super1 ALTER)."""
        for word in words:
            self.command(f"MEMORYA {address:X} {word:08X}")
            address += 4

    def program(self, address, lines):
        """Types (word, meaning) pairs. Each meaning is checked against the word's own fields;
        a line whose meaning starts "data" is data, not an instruction."""
        code = [word for word, meaning in lines if meaning != "undefined" and not meaning.startswith("data")]
        assert len(code) <= LIMIT_OF_WORDS, f"{len(code)} words of code"
        for offset, (word, meaning) in enumerate(lines):
            assert meaning.startswith("data") or decode(word, address + 4 * offset) == meaning, f"{word:08X}"
        self.poke(address, [word for word, _ in lines])

    def start(self, line):
        """Types a line and runs until the keyboard queue is empty, for a program that waits."""
        self.wire.clear()
        self.forget_text()
        self.type(line + "\r")
        assert self.run(self.idle, 10), self.screen()
        self.settle()

    def finish(self, seconds=10):
        assert self.run(self.at_prompt, seconds), self.screen()
        self.settle()
        return self.text()

    def poke_text(self, address, text):
        padded = text + bytes(-len(text) % 4)
        self.poke(address, [int.from_bytes(padded[i:i + 4], "little") for i in range(0, len(padded), 4)])

    def memory(self, address, length):
        """Bytes through the Executive's Memory command (SRC, Super1 MEMORY: word lines)."""
        text = self.command(f"MEMORY {address:X} +{length:X}")
        data = bytearray()
        for row in re.findall(r"^[0-9A-F]{8} :((?: +[0-9A-F]{8}){4})", text.replace("\r", ""), re.M):
            for word in row.split():
                data += int(word, 16).to_bytes(4, "little")
        return bytes(data[:length])


def registers(text):
    """The Executive's register dump (notes/test-specs.md 3.2), as {number: value}."""
    return {int(n): int(v, 16) for n, v in re.findall(r"R(\d+) *= ([0-9A-F]{8})", text)}
