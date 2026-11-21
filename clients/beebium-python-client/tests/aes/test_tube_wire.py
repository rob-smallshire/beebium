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
# test_tube_wire.py, tests the Tube conversation on the wire
# version 2.0.0, 20 November 2026.

"""What the Tube carries, through the real Executive and a real BBC host.

Expected values are notes/test-specs.md's, from AN004, UTIL and the Executive source, read
before any of these ran. Nothing comes from a run or from Beebium. The small ARM word lists are
typed at the A* prompt with the Executive's own MemoryA, and the 6502 bytes are poked into the
host: no disc, no file, nothing built for the AES. A run here is not evidence of what an AES
does. Run with run-server-tests.sh.
"""
import hashlib
import re
from pathlib import Path

import pytest

import dfs_image
from aes_host import (PROMPT, ROM, ROMS, Aes, calls_on_r2, conversation, copy_frozen, expected_banner, fact,
                      frozen_unchanged, need_rom, registers, start_call)

AB_SHA1 = "29daa03807a481b5ec0657ec3c95e2db1ae532c9"  # CLAUDE.md, fixed inputs.
BYTE, FILE, MULTIPLE, OPEN, EXIT = 6, 8, 12, 13, 0x11  # DOC, UTIL 2.4.
PROGRAM, TEXT, DATA_AT = 0x1000, 0x1100, 0x1200
EXIT_LINE = [(0xEF000011, "SWI &11")]  # Exit (DOC, UTIL 2.4.18).
LOAD_AT = 0x8000
HOST_LANGUAGE_COPY = 0x0491  # INF, Host230.lst, "Language entered with OSBYTE 142".
HOST_STUB, HOST_BLOCK, HOST_PAGE = 0x0900, 0x0A00, 0x0B00
USERV, FILEV = 0x0200, 0x0212  # DOC, the BBC's vectors; *CODE calls USERV (AN pp.4-5).


def pattern(length):
    return bytes((i * 7 + 3) & 255 for i in range(length))


def scratch_disc(folder, *sizes):
    files = [(f"D{size}", LOAD_AT, LOAD_AT, pattern(size)) for size in sizes]
    path = Path(folder) / "scratch.ssd"
    path.write_bytes(dfs_image.make(files))
    return path


def saved(disc, name):
    return dfs_image.catalogue(disc.read_bytes())[f"$.{name}"][2]


def rom_bytes():
    need_rom()
    return Path(ROM).read_bytes()


# The prompt, checked byte by byte.
def test_prompt_bytes():
    """SRC, ROM at &1C30, Super1:390: 132, 157, 135, "A*", 156, in Mode 7, in the file, in the
    screen's memory, and last in what the host is sent after a command."""
    at = int(fact("prompt_at"), 0)
    assert rom_bytes()[at:at + len(PROMPT)] == PROMPT
    with Aes() as aes:
        aes.boot()
        assert PROMPT in bytes(aes.bbc.memory.address.peek[0x7C00:0x8000])
        assert aes.command("HELP").endswith(PROMPT.decode("latin-1"))


def test_typing_at_the_prompt_gets_a_new_prompt():
    """SRC, Super1 HELP: `HELP` alone answers in the Executive and passes on to the host."""
    with Aes() as aes:
        aes.boot()
        text = aes.command("HELP")
        assert f"{fact('text.supervisor_version')}\n\r{fact('text.supervisor_name')}\n\r" in text
        assert text.endswith(PROMPT.decode("latin-1"))


# Section 1.4 and two rows of section 1.5.
def test_start_up_banner_then_zero():
    """AN p.28; ROM file at &1938; Super1:192, 215-246: the banner, then one zero byte."""
    with Aes(start_stopped=True) as aes:
        aes.bbc.debugger.add_breakpoint(HOST_LANGUAGE_COPY)
        aes.bbc.debugger.run_and_wait_for_stop(timeout=30)
        banner = expected_banner()
        assert banner.encode() in bytes(aes.bbc.memory.address.peek[0x7C00:0x8000])
        # LF, CR, the words, LF, CR, LF, CR, one zero. The count stands for the bytes, as
        # Beebium's trace leaves out R1.
        assert aes.bbc.tube_ula.state.counters.r1_p2h_writes == 2 + len(banner) + 4 + 1


def test_start_up_byte_80_is_ignored():
    """Section 1.5 (source over AN p.28, ROM read at &19D4): the host's type 4 and &80 start
    nothing, and OSBYTE 253 follows (Super1:250-257). AN's reading would run BASIC's bytes as
    ARM code and no prompt could appear. The trace is read every emulated millisecond, because the
    language copy overruns the ring at the usual rate, so "first" here means first from the boot."""
    with Aes() as aes:
        aes.chunk = 0.001
        aes.boot()
        r4 = aes.wire.host_wrote(4)
        assert r4[-9] == 4 and r4[-2] == 5, "type 4, then the release"
        assert aes.wire.host_wrote(2)[0] == 0x80
        assert calls_on_r2(aes.wire.parasite_wrote(2))[0] == start_call()


def test_language_copy_arrives_whole(tmp_path):
    """Type 7 from the real host (AN p.27; Kernel:1018-1024): BASIC at &8000 on a hard Break
    (INF, Host230.lst), read back by *SAVE, which is types 0 or 2. One image moved both ways."""
    disc = tmp_path / "blank.ssd"
    disc.write_bytes(dfs_image.make([]))
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.command("*SAVE LANG 8000 +4000", seconds=60)
    assert saved(disc, "LANG") == (ROMS / "bbc-basic_2.rom").read_bytes()


# Section 1.1, the calls the Executive makes for itself.
def test_prompt_conversation():
    """Kernel:885-891 for OSCLI, stars stripped by CLIA (Super1); Super1:372-393, Middle:155-164:
    OSBYTE &87, &EC, 3, &D9, then OSRDCH. Lengths and reply counts as section 1.1."""
    with Aes() as aes:
        aes.boot()
        aes.command("*FX 0")
        calls = calls_on_r2(aes.wire.parasite_wrote(2))
        calls = calls[next(i for i, call in enumerate(calls) if call[0] == 0x02):]
        assert calls[0] == [0x02] + list(b"FX 0\r")
        assert [c[0] for c in calls[1:5]] == [0x06, 0x06, 0x04, 0x06]
        assert calls[1][3] == 0x87
        assert calls[2][1:] == [0x14, 0xEB, 0xEC]
        assert calls[3][2] == 3
        assert calls[4] == [0x06, 0x00, 0x00, 0xD9]
        assert aes.wire.parasite_wrote(2)[-1] == 0x00, "OSRDCH waits last"


def test_osrdch_and_echo():
    """AN p.22; Kernel:268-277; Middle:164-199: C byte, character; OSBYTE &DA; echo on R1."""
    with Aes() as aes:
        aes.boot()
        aes.wire.clear()
        aes.forget_text()
        aes.type("X")
        assert aes.run(lambda: "X" in aes.text(), 2)
        reply = aes.wire.host_wrote(2)
        assert reply[0] & 0x80 == 0 and reply[1] == ord("X")
        assert calls_on_r2(aes.wire.parasite_wrote(2))[0] == [0x06, 0x00, 0xFF, 0xDA]


def test_exit_reads_the_time():
    """AN p.23, Kernel:336-373; Super1:283-287: OSWORD 1 is &08, &01, 0 out, 5 back; 5 bytes
    come back; `Finished after` and the prompt follow (notes/test-specs.md 3.2). Only once a
    program has run: START is -1 until GO sets it (SRC, Kernel:799, Middle:120-122, Super1:289-293),
    so a QUIT before any GO reads the time and prints nothing."""
    with Aes() as aes:
        aes.boot()
        assert fact("text.finished") not in aes.command("QUIT")
        aes.program(PROGRAM, EXIT_LINE)
        text = aes.command("GO")
        times = [reply for call, reply in conversation(aes.wire) if call == [0x08, 0x01, 0x00, 0x05]]
        assert [len(reply) for reply in times] == [5, 5], "GO reads the start, the exit reads the end"
        assert re.search(r"\n\r" + re.escape(fact("text.finished")) + r" [0-9., a-z]* sec\.\n\r", text), repr(text)


# Section 1.3.
def test_escape_through_r1():
    """AN p.25; Kernel:901-908: R1 byte with bit 7 set enters the handler; ReadLine returns C
    set and the supervisor acknowledges with OSBYTE &7E, then prints Escape (Middle:574-578)."""
    with Aes() as aes:
        aes.boot()
        aes.wire.clear()
        aes.forget_text()
        aes.bbc.keyboard.press_escape()
        assert aes.run(lambda: aes.at_prompt() and fact("text.escape") in aes.text(), 5)
        aes.settle()
        assert aes.host_r1_bytes()[0] & 0x80
        ack = [call for call in calls_on_r2(aes.wire.parasite_wrote(2)) if call[0] == 0x04 and call[2] == 0x7E]
        assert len(ack) == 1
        assert f"\n\r{fact('text.escape')}\n\r" in aes.text()


EVENT_HANDLER = 0x1040
EVENT_STORE = 0x1050
EVENT_PROGRAM = [
    (0xE3A00000, "MOV R0,#&0"),     # Control: error handler, no change
    (0xE3A01000, "MOV R1,#&0"),     # error buffer, no change
    (0xE3A02000, "MOV R2,#&0"),     # escape handler, no change
    (0xE3A03D41, "MOV R3,#&1040"),  # event handler, ours
    (0xEF00000F, "SWI &F"),         # Control
    (0xEF000004, "SWI &4"),         # ReadC: waits for a key, with events coming in
    (0xEE000000, "undefined"),      # ends the program
]
# At &1040. PC reads 8 ahead (DOC, SW p.1), so each offset of 8 lands 16 past its own word.
EVENT_HANDLER_LINES = [
    (0xE58F0008, "STR R0,[PC,#8]"),  # A, to &1050
    (0xE58F1008, "STR R1,[PC,#8]"),  # X, to &1054
    (0xE58F2008, "STR R2,[PC,#8]"),  # Y, to &1058
    (0xE1A0F00E, "MOV R15,R14"),     # return (DOC, UTIL 2.4.16)
]


@pytest.mark.parametrize("event", [4, 2])
def test_event_through_r1(event):
    """AN pp.25-26; Kernel:935-961: type below &80, then Y, X, A. Entered with R0 = A, R1 = X,
    R2 = Y (DOC, UTIL 2.4.16). Event 4 is vertical sync, 50 a second (INF, the BBC's event list;
    no document here gives it). Event 2 is a character entering the input buffer, with the
    character in Y (INF, the same). The default handler does nothing (Middle EVENTH), and no `IRQ,
    while at ` appears (Middle:670-674). The prompt returns afterwards. No R4 byte arrives during
    an event here, so Kernel:949-956, R4 served while waiting, is not exercised."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, EVENT_PROGRAM)
        aes.program(EVENT_HANDLER, EVENT_HANDLER_LINES)
        aes.command(f"*FX 14,{event}")
        aes.start("GO")
        aes.settle(0.5)
        aes.bbc.keyboard.type("K")
        text = aes.finish()
        stored = aes.memory(EVENT_STORE, 12)
        assert int.from_bytes(stored[:4], "little") == event
        if event == 2:
            assert int.from_bytes(stored[8:12], "little") == ord("K")
        r1 = aes.host_r1_bytes()
        whole = len(r1) - len(r1) % 4
        assert all(r1[i] < 0x80 for i in range(0, whole, 4)), "the type"
        assert all(r1[i] == event for i in range(3, whole, 4)), "A last: the event number"
        if event == 4:
            assert whole >= 80, "4 bytes, 50 a second for at least half a second (INF, 50 Hz)"
        off = aes.command(f"*FX 13,{event}")
        assert fact("text.irq_while") not in text + off


def test_host_error_through_r4_and_r2():
    """AN pp.21, 26; Kernel:911-934: R4 bit 7 set; R2 one byte, the number, the string, &00.
    Then Middle ERRORH: newline, text, " (", number, ")", newline."""
    with Aes() as aes:
        aes.boot()
        text = aes.command("*NOSUCHCOMMAND")
        assert aes.wire.host_wrote(4)[0] & 0x80
        reply = aes.wire.after(4).host_wrote(2)
        end = reply.index(0, 2)
        number, message = reply[1], bytes(reply[2:end]).decode("latin-1")
        assert f"\n\r{message} ({number})\n\r" in text, repr(text)


@pytest.mark.parametrize("size", [1, 255, 256, 257, 1000])
def test_file_in_and_out(tmp_path, size):
    """AN pp.26-27; Kernel:962-1003: R4 type, claimer, 4 address bytes high first, sync; data on
    R3 under FIQ; type 5 releases. Load then save back, at the edges in notes/tube-coverage.md."""
    disc = scratch_disc(tmp_path, size)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.command(f"*LOAD D{size} {LOAD_AT:X}")
        r4 = aes.wire.host_wrote(4)
        assert r4[0] in (1, 3), "host to parasite"
        assert r4[2:6] == [0, 0, 0x80, 0]
        assert r4[-2] == 5
        assert aes.memory(LOAD_AT, 16)[:size] == pattern(size)[:16]
        aes.command(f"*SAVE B{size} {LOAD_AT:X} +{size:X}")
        r4 = aes.wire.host_wrote(4)
        assert r4[0] in (0, 2), "parasite to host"
    assert saved(disc, f"B{size}") == pattern(size)


def test_low_address_load_changes_nothing(tmp_path):
    """Kernel:977-982: on types 1, 3, 7 an address below &F00 has &3000000 added, so the data goes
    at the ROM and changes nothing."""
    disc = scratch_disc(tmp_path, 16)
    with Aes(disc=disc) as aes:
        aes.boot()
        before = aes.memory(0xC00, 16)
        assert len(before) == 16
        aes.command("*LOAD D16 C00")
        assert aes.memory(0xC00, 16) == before


def test_type_6_real_loop():
    """Item 6 of the brief. A host that only reads, as AN p.9, against FIQP6 (Kernel:1006-1017),
    which waits on bit 7 of R3STAT before sending. Expected: the ROM's first 256 bytes (AN p.27)
    and, section 1.5, 257 writes to R3 and none to R4 (source, over AN's R4DATA at the end)."""
    code = bytes([0xA9, 0xD0, 0x20, 0x06, 0x04, 0x90, 0xF9,
                  0xA2, 0x00, 0xA0, HOST_BLOCK >> 8, 0xA9, 0x06, 0x20, 0x06, 0x04])
    wait_at = HOST_STUB + len(code) + 9 + 2 + 11 + 5 + 1
    code += bytes([0x20, wait_at & 255, wait_at >> 8] * 3)
    code += bytes([0xA0, 0x00])
    code += bytes([0xAD, 0xE5, 0xFE, 0x91, 0x80, 0xEA, 0xEA, 0xEA, 0xC8, 0xD0, 0xF5])
    code += bytes([0xA9, 0x90, 0x20, 0x06, 0x04])
    code += bytes([0x60])
    assert HOST_STUB + len(code) == wait_at
    code += bytes([0x60])
    with Aes() as aes:
        aes.boot()
        aes.bbc.debugger.ensure_stopped()
        host = aes.bbc.memory.address.bus
        host.write(HOST_STUB, code)
        host.write(HOST_BLOCK, (0x3000000).to_bytes(4, "little"))
        host.write(0x80, bytes([HOST_PAGE & 255, HOST_PAGE >> 8]))
        host.write(USERV, HOST_STUB.to_bytes(2, "little"))
        aes.command("*CODE")
        r4 = aes.wire.host_wrote(4)
        assert r4[0] == 6 and r4[2:6] == [3, 0, 0, 0], "type 6, the ROM's address high byte first"
        assert r4[-2] == 5, "the release"
        assert len(aes.wire.parasite_wrote(3)) == 257
        assert aes.wire.parasite_wrote(4) == []
        assert bytes(aes.bbc.memory.address.peek[HOST_PAGE:HOST_PAGE + 256]) == rom_bytes()[:256]


# Section 1.5, the rows that need the ARM to make a call. Typed with MemoryA at the prompt.
def test_osbyte_82_83_84_go_to_the_host():
    """Section 1.5, source over AN p.22 (Kernel:278-331): each is &06, X, Y, A on the wire. The
    undefined word at the end gives the Executive's register dump, and the prompt."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            (0xE3A01000, "MOV R1,#&0"),   # X = 0
            (0xE3A02000, "MOV R2,#&0"),   # Y = 0
            (0xE3A00082, "MOV R0,#&82"),  # OSBYTE &82
            (0xEF000006, "SWI &6"),       # Byte
            (0xE3A00083, "MOV R0,#&83"),  # OSBYTE &83
            (0xEF000006, "SWI &6"),       # Byte
            (0xE3A00084, "MOV R0,#&84"),  # OSBYTE &84
            (0xEF000006, "SWI &6"),       # Byte
            (0xEE000000, "undefined"),    # ends the program with the Executive's register dump
        ])
        aes.command("GO")
        calls = calls_on_r2(aes.wire.parasite_wrote(2))
        high = [call for call in calls if call[0] == 0x06 and call[3] in (0x82, 0x83, 0x84)]
        assert [call[3] for call in high] == [0x82, 0x83, 0x84]


def test_osfile_reply_is_not_masked(tmp_path):
    """Section 1.5, source over AN p.24 (Kernel:394-396): a host reply &81 reaches R0 whole. The
    host's FILEV is pointed at LDA #&81, RTS, because no filing system sends bit 7."""
    disc = scratch_disc(tmp_path, 16)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"D16\r")
        aes.program(PROGRAM, [
            (0xE3A00005, "MOV R0,#&5"),     # OSFILE 5, read information
            (0xE3A01C11, "MOV R1,#&1100"),  # the name
            (0xEF000008, "SWI &8"),         # File
            (0xEE000000, "undefined"),      # the register dump shows R0
        ])
        aes.bbc.debugger.ensure_stopped()
        aes.bbc.memory.address.bus.write(HOST_STUB, bytes([0xA9, 0x81, 0x60]))
        aes.bbc.memory.address.bus.write(FILEV, HOST_STUB.to_bytes(2, "little"))
        text = aes.command("GO")
        assert registers(text)[0] & 255 == 0x81, text


def test_buffered_handle_and_local_multiple(tmp_path):
    """Sections 1.2 and 1.5. Open with &40, buffering on: handle + 256, 257 up (DOC, UTIL 2.4.7,
    2.4.14; Kernel:705-765). Multiple 4 on it twice (Kernel:529-539, source over UTIL 2.4.13):
    the second is served from the buffer, so only one OSGBPB crosses the Tube, and R2, R3, R4
    come back as UTIL 2.4.13 says."""
    disc = scratch_disc(tmp_path, 2000)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"D2000\r")
        one = [
            (0xE1A01007, "MOV R1,R7"),    # the handle
            (0xE3A00004, "MOV R0,#&4"),   # Multiple 4, read at the file's pointer
            (0xE3A03010, "MOV R3,#&10"),  # 16 bytes
            (0xEF00000C, "SWI &C"),       # Multiple
        ]
        aes.program(PROGRAM, [
            (0xE3A00040, "MOV R0,#&40"),    # Open for input
            (0xE3A01C11, "MOV R1,#&1100"),  # the name
            (0xEF00000D, "SWI &D"),         # Open
            (0xE1A07000, "MOV R7,R0"),      # keep the handle
            (0xE3A02C12, "MOV R2,#&1200"),  # where the data goes
            *one, *one,
            (0xEE000000, "undefined"),      # the register dump shows R2, R3, R4, R7
        ])
        text = aes.command("GO")
        calls = calls_on_r2(aes.wire.parasite_wrote(2))
        dump = registers(text)
        assert dump[7] > 256 and dump[7] < 512
        assert dump[2] == DATA_AT + 32 and dump[3] == 0 and dump[4] == 32
        assert sum(1 for call in calls if call[0] == 0x16) == 1
        assert calls[[c[0] for c in calls].index(0x12)] == [0x12, 0x40] + list(b"D2000\r")
        assert aes.memory(DATA_AT, 32) == pattern(2000)[:32]


def test_transfer_reaches_file_and_args(tmp_path):
    """SRC, Super1 TRANSFER, MOVFIL: Args 0, 0; File &FF then File 0, with 16-byte blocks last
    byte first (AN pp.24-25). A ROM command, so no program is needed."""
    disc = scratch_disc(tmp_path, 100)
    with Aes(disc=disc) as aes:
        aes.boot()
        text = aes.command("TRANSFER DISC DISC D100 C100")
        assert fact("said.transfer") + "\n\r" in text, repr(text)
        calls = calls_on_r2(aes.wire.parasite_wrote(2))
        files = [call for call in calls if call[0] == 0x14]
        assert [call[-1] for call in files] == [0xFF, 0x00]
        assert any(call[0] == 0x0C for call in calls)
    assert saved(disc, "C100") == pattern(100)


# Item 4: the exception tests that need the Tube.
def report_of(text, message):
    match = re.search(message + r"([0-9A-F]{8})\n\r" + re.escape(fact("text.dump")), text)
    assert match, repr(text)
    return int(match.group(1), 16)


def test_undefined_to_the_screen():
    """SW 3.6; notes/test-specs.md 3.2: InitStore's default is &EE000000 (SRC, Super1 HELP), GO
    runs it at &1000 (DOC, UTIL), the report gives R14 = &1004 in its low bits, USR, and the way
    out is OSWORD 1, `Finished after` and the prompt."""
    with Aes() as aes:
        aes.boot()
        assert fact("said.store") + "\n\r" in aes.command("INITSTORE", seconds=60)
        text = aes.command("GO")
        r14 = report_of(text, fact("text.undefined"))
        assert r14 & 0x3FFFFFC == 0x1004 and r14 & 3 == 0
        assert fact("said.mode_usr") in text
        assert registers(text)[15] == r14
        assert fact("text.finished") + " " in text


def test_address_exception_to_the_screen():
    """SW 3.2; notes/test-specs.md 3.2: Memory at &4000000 makes the ROM's first load of the word
    fault (SRC, Super1 MEMP1; found in the ROM file, where the first is E499A000, the post-indexed
    form, offset 0). R14 = that word + 8, USR, R9 the address. With offset 0 writeback would leave
    R9 the same, so this shows the address, not adx.base_writeback."""
    rom = rom_bytes()
    load = int(fact("memory_load_word"), 0).to_bytes(4, "little")
    at = 0x3000000 + rom.index(load, rom.index(fact("text.memory_anchor").encode()))
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, EXIT_LINE)
        aes.command("GO")
        text = aes.command("MEMORY 4000000")
        r14 = report_of(text, fact("text.address_exception"))
        assert r14 & 0x3FFFFFC == at + 8 and r14 & 3 == 0
        assert registers(text)[9] == 0x4000000
        assert fact("text.finished") + " " in text


# Item 3.
def test_ab_loads_as_a_file(tmp_path):
    """CLAUDE.md, fixed inputs: AB's SHA-1. Loaded from a copy of the Tak disc at its own load
    address and saved back to a blank disc: both directions, whole. AB is not started."""
    disc = copy_frozen("arm_tak_1", tmp_path)
    load, _, data = dfs_image.catalogue(disc.read_bytes())["$.AB"]
    assert hashlib.sha1(data).hexdigest() == AB_SHA1
    blank = tmp_path / "blank.ssd"
    blank.write_bytes(dfs_image.make([]))
    with Aes(disc=disc, disc1=blank) as aes:
        aes.boot()
        aes.command("*LOAD AB", seconds=60)
        aes.command(f"*SAVE :1.ABCOPY {load:X} +{len(data):X}", seconds=60)
    assert hashlib.sha1(saved(blank, "ABCOPY")).hexdigest() == AB_SHA1
    assert frozen_unchanged("arm_tak_1")
