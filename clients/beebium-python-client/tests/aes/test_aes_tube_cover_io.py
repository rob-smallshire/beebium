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
# test_aes_tube_cover_io.py, tests the OSWORD and file Tube calls
# version 2.0.0, 20 November 2026.

"""The rest of the SWI index and Tube tables that typed words can reach.

The owner agreed this way (USER, 1 Nov 2026): up to 14 words of code, typed at once at A* with the
Executive's own MemoryA. The stop word and data words do not count. Each word is written out with its meaning, and aes_host.decode checks the two agree,
from the manual's bit fields. Expected values come from notes/test-specs.md, UTIL, AN004, the
Executive source or the ROM's bytes, never from a run. A run here is not evidence of an AES.
"""
import pytest

import dfs_image
from aes_host import Aes, conversation, registers
from cover_support import (BLOCK, CLOSE_R7, C_FLAG, OPEN_INPUT, OPEN_UPDATE, PROGRAM, TEXT, UNDEFINED, calls_of,
                           disc_with, on_disc, pattern)


# OSWORD: every count in Kernel:374-379 (notes/test-specs.md 1.1).
COUNTS = {2: (5, 0), 3: (0, 5), 4: (5, 0), 7: (8, 0), 8: (14, 0), 9: (4, 5), 10: (1, 9), 11: (1, 5),
          12: (5, 0), 13: (0, 8), 14: (1, 24), 15: (32, 0), 16: (16, 1), 17: (13, 13), 18: (0, 128),
          19: (4, 4), 20: (128, 128)}


@pytest.mark.parametrize("first, last, lines", [
    (2, 4, [(0xE3A04002, "MOV R4,#&2"), (0xE3540005, "CMP R4,#&5")]),
    (7, 20, [(0xE3A04007, "MOV R4,#&7"), (0xE3540015, "CMP R4,#&15")]),
])
def test_osword_counts(first, last, lines):
    """SRC, Kernel:374-379: out and back for each call, on the wire, and that many bytes back.
    OSWORD 5 and 6 are left out here because they touch host memory: next test."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            lines[0],                       # the first call
            (0xE1A00004, "MOV R0,R4"),      # the call number
            (0xE3A01A02, "MOV R1,#&2000"),  # the block
            (0xEF000007, "SWI &7"),         # Word
            (0xE2844001, "ADD R4,R4,#&1"),  # next
            lines[1],                       # one past the last
            (0x1AFFFFF9, "BNE &1004"),      # round again
            UNDEFINED,
        ])
        aes.chunk = 0.001  # Calls 18 and 20 are 128 bytes each way: the trace must keep up.
        aes.command("GO")
        seen = {call[1]: (call, reply) for call, reply in calls_of(aes, 0x08) if first <= call[1] <= last}
        assert sorted(seen) == list(range(first, last + 1))
        for number, (call, reply) in seen.items():
            out, back = COUNTS[number]
            assert call[2] == out and len(call) == 4 + out and call[-1] == back, number
            assert len(reply) == back, number


def test_osword_5_and_6_reach_host_memory():
    """SRC, Kernel:374-379: 5 is 4 out, 5 back; 6 is 5 out, 0 back. Block last byte first (AN
    p.23). The byte lands in the host, and comes back at offset 4 of the block (INF, the BBC's
    OSWORD 5 and 6; AN p.4 lists them)."""
    with Aes() as aes:
        aes.boot()
        aes.poke(BLOCK, [0xFFFF0A00, 0x0000005A])  # data: host address, the byte to put there
        aes.program(PROGRAM, [
            (0xE3A00006, "MOV R0,#&6"),     # write host memory
            (0xE3A01A02, "MOV R1,#&2000"),  # the block
            (0xEF000007, "SWI &7"),         # Word
            (0xE3A02000, "MOV R2,#&0"),     # zero
            (0xE5C12004, "STRB R2,[R1,#4]"),       # clear the block's byte, so the read must fill it
            (0xE3A00005, "MOV R0,#&5"),     # read host memory
            (0xEF000007, "SWI &7"),         # Word
            UNDEFINED,
        ])
        aes.command("GO")
        calls = dict((call[1], (call, reply)) for call, reply in calls_of(aes, 0x08))
        assert calls[6][0] == [0x08, 6, 5, 0x5A, 0xFF, 0xFF, 0x0A, 0x00, 0]
        assert calls[5][0] == [0x08, 5, 4, 0xFF, 0xFF, 0x0A, 0x00, 5]
        assert aes.bbc.memory.address.peek[0x0A00] == 0x5A
        assert aes.memory(BLOCK + 4, 1)[0] == 0x5A


def test_osword_0_never_crosses_and_16_each_way_and_callers_counts():
    """DOC, UTIL 2.4.8; Kernel:332-333: Word 0 does nothing. Kernel: &15 to &7F are 16 and 16;
    &80 up take their counts from the block's first two bytes (notes/test-specs.md 1.1)."""
    with Aes() as aes:
        aes.boot()
        aes.poke(0x2100, [0x00000000])  # data: counts 0 and 0
        aes.poke(0x2200, [0x00000305])  # data: 5 out, 3 back
        aes.poke(0x2300, [0x00008080])  # data: 128 and 128, the limit (DOC, AN p.5)
        aes.program(PROGRAM, [
            (0xE3A00000, "MOV R0,#&0"),     # OSWORD 0
            (0xE3A01A02, "MOV R1,#&2000"),  # a block
            (0xEF000007, "SWI &7"),         # Word: nothing on the wire
            (0xE3A00021, "MOV R0,#&21"),    # a call from &15 to &7F
            (0xEF000007, "SWI &7"),         # Word
            (0xE3A00080, "MOV R0,#&80"),    # a call from &80
            (0xE3A01C21, "MOV R1,#&2100"),  # counts 0, 0
            (0xEF000007, "SWI &7"),         # Word
            (0xE3A00080, "MOV R0,#&80"),    # again
            (0xE3A01C22, "MOV R1,#&2200"),  # counts 5, 3
            (0xEF000007, "SWI &7"),         # Word
            (0xE3A00080, "MOV R0,#&80"),    # again
            (0xE3A01C23, "MOV R1,#&2300"),  # counts 128, 128
            (0xEF000007, "SWI &7"),         # Word
            UNDEFINED,
        ])
        aes.command("GO")
        calls = [item for item in calls_of(aes, 0x08) if item[0][1] != 1]
        assert [call[1] for call, _ in calls] == [0x21, 0x80, 0x80, 0x80]
        assert calls[0][0][2] == 16 and calls[0][0][-1] == 16 and len(calls[0][1]) == 16
        assert calls[1][0] == [0x08, 0x80, 0, 0] and calls[1][1] == []
        assert calls[2][0][2] == 5 and calls[2][0][-1] == 3 and len(calls[2][1]) == 3
        assert calls[3][0][2] == 128 and calls[3][0][-1] == 128 and len(calls[3][1]) == 128


# Files, unbuffered: handles below 256.
def test_bget_bput_close(tmp_path):
    """AN p.24; Kernel:447-450, 490-497, 676-685; UTIL 2.4.11, 2.4.12, 2.4.14. &C0 gives a handle
    below 256, so each byte crosses: OSBGET &0E, handle; reply C, byte. OSBPUT &10, handle, byte;
    one byte back. Close &12, 0, handle; one byte back."""
    disc = disc_with(tmp_path, "D16", 16)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"D16\r")
        aes.program(PROGRAM, [
            *OPEN_UPDATE,
            (0xEF00000A, "SWI &A"),      # BGet: the first byte
            (0xE1A05000, "MOV R5,R0"),   # keep it
            (0xE1A01007, "MOV R1,R7"),   # the handle
            (0xE3A00055, "MOV R0,#&55"), # the byte to put at offset 1
            (0xEF00000B, "SWI &B"),      # BPut
            *CLOSE_R7,
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        handle = dump[7]
        assert 0 < handle < 256
        assert dump[5] == pattern(16)[0]
        assert [call for call, _ in calls_of(aes, 0x12)] == [[0x12, 0xC0, *b"D16\r"], [0x12, 0x00, handle]]
        (bget, got), = calls_of(aes, 0x0E)
        assert bget == [0x0E, handle] and got[0] & 0x80 == 0 and got[1] == pattern(16)[0]
        (bput, back), = calls_of(aes, 0x10)
        assert bput == [0x10, handle, 0x55] and len(back) == 1
    assert on_disc(disc, "D16")[:2] == bytes([pattern(16)[0], 0x55])


def test_bget_reply_bytes(tmp_path):
    """SRC, our DFS 2.26 disassembly (notes/evidence/dfs226-tube-host.md, &052D, &053A): OSBGET's
    first reply byte has C, end of file, in bit 7 and the data byte shifted right in bits 0-6; the
    second is the data byte. AN p.24 (DOC) agrees on bit 7 and the second byte. Bytes with bit 0
    and bit 7 set, then end of file. The ARM gets R0 = byte, C = end of file (UTIL 2.4.11)."""
    data = bytes([0x01, 0x80, 0xFF])
    disc = tmp_path / "scratch.ssd"
    disc.write_bytes(dfs_image.make([("B3", 0x8000, 0x8000, data)]))
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"B3\r")
        aes.program(PROGRAM, [
            *OPEN_UPDATE,
            (0xEF00000A, "SWI &A"),      # BGet: &01
            (0xE1A04000, "MOV R4,R0"),
            (0xEF00000A, "SWI &A"),      # BGet: &80
            (0xE1A05000, "MOV R5,R0"),
            (0xEF00000A, "SWI &A"),      # BGet: &FF
            (0xE1A06000, "MOV R6,R0"),
            (0xE1A0900F, "MOV R9,R15"),  # flags after a byte that was there
            (0xEF00000A, "SWI &A"),      # BGet: past the end
            (0xE1A0800F, "MOV R8,R15"),  # flags at the end
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        handle = dump[7]
        assert [dump[4], dump[5], dump[6]] == list(data)
        assert not dump[9] & C_FLAG and dump[8] & C_FLAG
        bgets = calls_of(aes, 0x0E)
        assert [call for call, _ in bgets] == [[0x0E, handle]] * 4
        assert [reply for _, reply in bgets[:3]] == [[byte >> 1, byte] for byte in data]
        first, second = bgets[3][1]
        assert first & 0x80 and first & 0x7F == second >> 1


def test_bput_reply_is_7f(tmp_path):
    """SRC, our DFS 2.26 disassembly (&0520, &059C): OSBPUT sends the handle, then the byte, and
    gets exactly one byte back, always &7F. AN p.24 (DOC): handle, byte, one byte back, discarded.
    Bytes with bit 7 set and clear."""
    disc = disc_with(tmp_path, "P2", 2)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"P2\r")
        aes.program(PROGRAM, [
            *OPEN_UPDATE,
            (0xE3A00081, "MOV R0,#&81"),
            (0xEF00000B, "SWI &B"),      # BPut &81
            (0xE1A01007, "MOV R1,R7"),
            (0xE3A0007E, "MOV R0,#&7E"),
            (0xEF00000B, "SWI &B"),      # BPut &7E
            *CLOSE_R7,
            UNDEFINED,
        ])
        handle = registers(aes.command("GO"))[7]
        assert calls_of(aes, 0x10) == [([0x10, handle, 0x81], [0x7F]), ([0x10, handle, 0x7E], [0x7F])]
    assert on_disc(disc, "P2") == bytes([0x81, 0x7E])


def test_end_of_file_args_and_eof_call(tmp_path):
    """DOC, UTIL 2.4.10, 2.4.11; AN p.24; Middle:331-349: BGet past the end sets C (bit 7 of the
    first byte). OSARGS 2 on a handle below 256 is &0C, handle, R2 high first, A; reply type, 4
    bytes, high first. OSBYTE &7F below 256 goes to the host (Kernel:293-308) and gives non-zero
    at the end (INF, the BBC's OSBYTE &7F)."""
    disc = disc_with(tmp_path, "D1", 1)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"D1\r")
        aes.program(PROGRAM, [
            *OPEN_UPDATE,
            (0xEF00000A, "SWI &A"),      # BGet: the only byte
            (0xEF00000A, "SWI &A"),      # BGet: past the end
            (0xE1A0600F, "MOV R6,R15"),  # keep the flags
            (0xE3A02000, "MOV R2,#&0"),  # data value for OSARGS
            (0xE3A00002, "MOV R0,#&2"),  # OSARGS 2: extent
            (0xEF000009, "SWI &9"),      # Args
            (0xE3A0007F, "MOV R0,#&7F"), # OSBYTE &7F: at end of file?
            (0xEF000006, "SWI &6"),      # Byte
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        handle = dump[7]
        assert dump[6] & C_FLAG
        assert dump[2] == 1
        assert dump[1] != 0
        (args, back), = calls_of(aes, 0x0C)
        assert args == [0x0C, handle, 0, 0, 0, 0, 2]
        assert len(back) == 5 and back[-4:] == [0, 0, 0, 1]
        assert any(call[0] == 0x04 and call[2] == 0x7F for call, _ in conversation(aes.wire))


def test_open_missing_file_gives_zero(tmp_path):
    """AN p.24; DOC, UTIL 2.4.14; Kernel:691-692: R0 = 0 if not opened."""
    disc = disc_with(tmp_path, "D1", 1)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"NONE\r")
        aes.program(PROGRAM, [*OPEN_INPUT[:3], UNDEFINED])
        assert registers(aes.command("GO"))[0] == 0


def test_osbyte_9d_has_no_reply(tmp_path):
    """AN p.22; Kernel:317-318: &06, X, Y, &9D and nothing back. The byte reaches the file."""
    disc = disc_with(tmp_path, "D16", 16)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"D16\r")
        aes.program(PROGRAM, [
            *OPEN_UPDATE[:4],
            (0xE3A0009D, "MOV R0,#&9D"),  # OSBYTE &9D
            (0xE3A01066, "MOV R1,#&66"),  # X: the byte
            (0xE1A02007, "MOV R2,R7"),    # Y: the handle
            (0xEF000006, "SWI &6"),       # Byte
            *CLOSE_R7,
            UNDEFINED,
        ])
        handle = registers(aes.command("GO"))[7]
        (call, back), = [item for item in calls_of(aes, 0x06) if item[0][3] == 0x9D]
        assert call == [0x06, 0x66, handle, 0x9D] and back == []
    assert on_disc(disc, "D16")[0] == 0x66


# Files, buffered: handles 256 up, worked in ARM memory.
def test_buffered_bget_args_and_eof_stay_local(tmp_path):
    """Spec 1.2; DOC, UTIL 2.4.11; Kernel:293-308, 401-417, BGet above: no &0E, no OSBYTE &7F on the
    wire, and of OSARGS only Open's own. PTR reads 2; at the end, OSBYTE &7F gives -1 in R1."""
    disc = disc_with(tmp_path, "D2", 2)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"D2\r")
        aes.program(PROGRAM, [
            *OPEN_INPUT,
            (0xEF00000A, "SWI &A"),      # BGet: first byte
            (0xEF00000A, "SWI &A"),      # BGet: second byte
            (0xE1A05000, "MOV R5,R0"),   # keep it
            (0xE3A00000, "MOV R0,#&0"),  # OSARGS 0: PTR
            (0xEF000009, "SWI &9"),      # Args
            (0xE3A0007F, "MOV R0,#&7F"), # OSBYTE &7F
            (0xEF000006, "SWI &6"),      # Byte
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        assert 256 < dump[7] < 512
        assert dump[5] == pattern(2)[1] and dump[2] == 2 and dump[1] == 0xFFFFFFFF
        assert calls_of(aes, 0x0E) == []
        assert len(calls_of(aes, 0x0C)) == 1
        assert not any(call[0] == 0x04 and call[2] == 0x7F for call, _ in conversation(aes.wire))


def test_buffered_bput_writes_on_close(tmp_path):
    """Spec 1.2; DOC, UTIL 2.4.12, 2.4.14; Kernel, SBPUTB, SOPEN: no &10 on the wire, the handle
    is 257 up, and closing puts the two bytes in the file."""
    disc = disc_with(tmp_path, "D1", 1)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"NEW\r")
        aes.program(PROGRAM, [
            (0xE3A00080, "MOV R0,#&80"),  # Open for output
            *OPEN_UPDATE[1:],
            (0xE3A00041, "MOV R0,#&41"),  # "A"
            (0xEF00000B, "SWI &B"),       # BPut
            (0xE3A00042, "MOV R0,#&42"),  # "B"
            (0xEF00000B, "SWI &B"),       # BPut
            *CLOSE_R7,
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        assert 256 < dump[7] < 512
        assert calls_of(aes, 0x10) == []
    assert on_disc(disc, "NEW") == b"AB"


def test_close_all_and_close_command(tmp_path):
    """Kernel:676-685, Middle CloseHash0: Open with R0 = R1 = 0 is &12, 0, 0. `CLOSE` empties the
    Executive's table, then goes to the host as a command (Middle SCLICLOSE, SCLIT). That the
    table empties is not observed here; only the wire is."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            (0xE3A00000, "MOV R0,#&0"),  # close
            (0xE3A01000, "MOV R1,#&0"),  # all
            (0xEF00000D, "SWI &D"),      # Open
            UNDEFINED,
        ])
        aes.command("GO")
        (close, back), = calls_of(aes, 0x12)
        assert close == [0x12, 0, 0] and len(back) == 1
        aes.command("CLOSE")
        assert calls_of(aes, 0x02)[0][0] == [0x02, *b"CLOSE\r"]
