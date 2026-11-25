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
# test_aes_tube_cover_swis.py, tests the screen, keyboard and other SWIs
# version 2.0.0, 20 November 2026.

"""The screen and keyboard SWIs, and the SWIs with no Tube traffic of their own, through the
running Executive. Written as test_aes_tube_cover_io.py says, which gives the rules for them."""
import pytest

from aes_host import Aes, fact, registers, start_call
from cover_support import (BLOCK, C_FLAG, I_FLAG, OPEN_INPUT, PROGRAM, ROUTINE, TEXT, UNDEFINED, V_FLAG,
                           disc_with, on_disc, pattern, words)


# Screen and keyboard.
def test_write_swis():
    """DOC, UTIL 2.4.1 to 2.4.4, 2.4.28: exact bytes through the host's OSWRCH, R0 past the zero
    after Write0, WriteS running on at the next word. R1 itself is not seen (Beebium's trace
    leaves it out), so this is the byte as the host took it."""
    with Aes() as aes:
        aes.boot()
        aes.poke_text(TEXT, b"Hi\0")
        aes.program(PROGRAM, [
            (0xE3A00C11, "MOV R0,#&1100"),  # the string
            (0xEF000002, "SWI &2"),         # Write0
            (0xE1A07000, "MOV R7,R0"),      # keep R0
            (0xEF000003, "SWI &3"),         # NewLine
            (0xEF000151, "SWI &151"),       # WriteI "Q"
            (0xE3A0005A, "MOV R0,#&5A"),    # "Z"
            (0xEF000000, "SWI &0"),         # WriteC
            (0xEF000001, "SWI &1"),         # WriteS
            (0x00006B6F, "data ok, 0, 0"),  # its string, then the next word runs
            UNDEFINED,
        ])
        text = aes.command("GO")
        assert "Hi\n\rQZok" in text
        assert registers(text)[7] == TEXT + 3


@pytest.mark.parametrize("escape", [False, True])
def test_readc_key_and_escape(escape):
    """DOC, UTIL 2.4.5; AN p.22; Kernel:268-277: R0 = character, C clear; on Escape C is set."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            (0xEF000004, "SWI &4"),      # ReadC
            (0xE1A05000, "MOV R5,R0"),   # keep the character
            (0xE1A0600F, "MOV R6,R15"),  # keep the flags
            UNDEFINED,
        ])
        aes.start("GO")
        if escape:
            aes.bbc.keyboard.press_escape()
        else:
            aes.bbc.keyboard.type("K")
        dump = registers(aes.finish())
        assert bool(dump[6] & C_FLAG) == escape
        if not escape:
            assert dump[5] == ord("K")


def test_readline_delete_and_ctrl_u():
    """Middle:164-178: &7F takes one character back, Ctrl-U all of them, each echoed as &7F. So
    the line reaches the Executive clean, and HELP answers."""
    with Aes() as aes:
        aes.boot()
        assert fact("text.supervisor_version") in aes.command("HEX\x7fLP")
        aes.wire.clear()
        aes.forget_text()
        aes.type("ABC")
        assert aes.run(aes.idle, 5)
        aes.settle()
        aes.bbc.keyboard.ctrl_down()
        aes.bbc.keyboard.key_down("U")
        aes.settle(0.2)
        aes.bbc.keyboard.key_up("U")
        aes.bbc.keyboard.ctrl_up()
        aes.settle(0.2)
        assert aes.text().count("\x7f") == 3
        assert fact("text.supervisor_version") in aes.command("HELP")


def test_readline_length_limit():
    """Super1 CLIPT2, 238 characters; Middle:179-183: past the limit, BEL and no echo."""
    with Aes() as aes:
        aes.boot()
        aes.wire.clear()
        aes.forget_text()
        aes.type("X" * 240)
        assert aes.run(aes.idle, 40)
        aes.settle()
        text = aes.text()
        assert text.count("X") == 238 and text.count("\x07") == 2


# SWIs with no Tube traffic of their own, through the running Executive.
def test_control_returns_and_sets_handlers():
    """DOC, UTIL 2.4.16: 0 for no change, previous values back. The default event handler is the
    Executive's own, in the ROM (SRC, Middle EVENTH)."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            (0xE3A00000, "MOV R0,#&0"),     # no change
            (0xE3A01000, "MOV R1,#&0"),     # no change
            (0xE3A02000, "MOV R2,#&0"),     # no change
            (0xE3A03D41, "MOV R3,#&1040"),  # a new event handler
            (0xEF00000F, "SWI &F"),         # Control
            (0xE1A08003, "MOV R8,R3"),      # the old one
            (0xE3A00000, "MOV R0,#&0"),     # no change
            (0xE3A01000, "MOV R1,#&0"),     # no change
            (0xE3A02000, "MOV R2,#&0"),     # no change
            (0xE3A03000, "MOV R3,#&0"),     # no change
            (0xEF00000F, "SWI &F"),         # Control
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        assert dump[3] == ROUTINE
        assert dump[8] & 0x3FFFFFC >= 0x3000000


def test_int_on_and_off():
    """DOC, UTIL 2.4.20, 2.4.21; notes/test-specs.md part 2: I set after IntOff, clear after IntOn."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            (0xEF000014, "SWI &14"),     # IntOff
            (0xE1A0800F, "MOV R8,R15"),  # keep the flags
            (0xEF000013, "SWI &13"),     # IntOn
            (0xE1A0900F, "MOV R9,R15"),  # keep the flags
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        assert dump[8] & I_FLAG and not dump[9] & I_FLAG


def test_callback_at_the_next_exit():
    """DOC, UTIL 2.4.22, 2.4.26, 2.4.27: CallBack sets block and routine; SetCallBack fires at the
    next kernel exit, not its own. So R9 = 1 is set between the two, the routine sees it, and the
    block holds R9 at slot 9 and the return PC at slot 15."""
    with Aes() as aes:
        aes.boot()
        aes.poke(ROUTINE, [0xEE000000])       # data: the routine is the undefined word
        aes.program(PROGRAM, [
            (0xE3A00A02, "MOV R0,#&2000"),    # the save block
            (0xE3A01D41, "MOV R1,#&1040"),    # the routine
            (0xEF000015, "SWI &15"),          # CallBack
            (0xEF00001B, "SWI &1B"),          # SetCallBack
            (0xE3A09001, "MOV R9,#&1"),       # after SetCallBack has returned
            (0xEF00001D, "SWI &1D"),          # an unused SWI: the next kernel exit
            (0xE3A09002, "MOV R9,#&2"),       # must not run
            UNDEFINED,
        ])
        text = aes.command("GO")
        dump = registers(text)
        assert dump[9] == 1
        assert dump[15] & 0x3FFFFFC == ROUTINE + 4
        block = words(aes.memory(BLOCK, 64))
        assert block[9] == 1
        assert block[15] & 0x3FFFFFC == PROGRAM + 24


def test_breakpt_default():
    """DOC, UTIL 2.4.24; Middle DEFBRK: `Stopped at break point at ` and the dump (spec part 2).
    R15 in the dump is the SWI's own address (SRC, Middle SBRKPT: R14 less 4)."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [(0xEF000017, "SWI &17")])
        text = aes.command("GO")
        assert fact("text.break_point") in text
        assert registers(text)[15] & 0x3FFFFFC == PROGRAM


def test_breakctrl_own_block_and_routine():
    """DOC, UTIL 2.4.25: previous values back, then the user registers land in our block and our
    routine runs, not the default one that prints `Stopped at break point at `. The default block
    is &E40 (SRC, Middle DEFHN2)."""
    with Aes() as aes:
        aes.boot()
        aes.poke(ROUTINE, [0xEE000000])       # data: the routine is the undefined word
        aes.program(PROGRAM, [
            (0xE3A00A02, "MOV R0,#&2000"),    # the block
            (0xE3A01D41, "MOV R1,#&1040"),    # the routine
            (0xEF000018, "SWI &18"),          # BreakCtrl
            (0xE1A08000, "MOV R8,R0"),        # the old block
            (0xE1A09001, "MOV R9,R1"),        # the old routine
            (0xEF000017, "SWI &17"),          # BreakPt
        ])
        text = aes.command("GO")
        dump = registers(text)
        assert dump[8] == 0xE40 and dump[9] >= 0x3000000
        assert fact("text.break_point") not in text
        assert dump[15] & 0x3FFFFFC == ROUTINE + 4, "the undefined word at our routine"
        block = words(aes.memory(BLOCK, 64))
        assert block[8] == 0xE40


def test_unused_numbers_return_with_v_clear():
    """SRC, Kernel:192-193, 199-200, 233, 238-242; DOC, UTIL 2.4: &1D to &FF and &200 up do nothing
    and return with V clear, V having been set first. UnusedSWI with 0 gives the handler back
    (DOC, UTIL 2.4.26): by default the kernel's own return, SLVK, in the low copy below &E00 and
    not at ROM (SRC, Kernel:233, no +ROM; Super1 COPYLOOP)."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            (0xE33FF201, "TEQP PC,#&10000000"),  # set V
            (0xE1A0700F, "MOV R7,R15"),          # V is set here
            (0xEF00001D, "SWI &1D"),             # unused
            (0xE1A0800F, "MOV R8,R15"),          # V is clear here
            (0xE33FF201, "TEQP PC,#&10000000"),  # set V
            (0xEF000200, "SWI &200"),            # high number
            (0xE1A0900F, "MOV R9,R15"),          # V is clear here
            (0xE3A00000, "MOV R0,#&0"),          # no change
            (0xEF000019, "SWI &19"),             # UnusedSWI
            UNDEFINED,
        ])
        dump = registers(aes.command("GO"))
        assert dump[7] & V_FLAG
        assert not dump[8] & V_FLAG and not dump[9] & V_FLAG
        assert 0 < dump[0] < int(fact("workspace_limit"), 0)


def test_getenv():
    """DOC, UTIL 2.3, 2.4.17, Super1 HELP: R1 = 4 MB less 20 KB; R0 points at the command string,
    with the text after ';'. Printed by the program, as UTIL's example does: once it ends the
    Executive reads the time over that buffer (SRC, Super1:283-287)."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [
            (0xEF000010, "SWI &10"),  # GetEnv
            (0xEF000002, "SWI &2"),   # Write0: the string R0 points at
            UNDEFINED,
        ])
        text = aes.command("GO ;HELLO")
        assert "HELLO" in text.split("GO ;HELLO\n\r", 1)[1]
        assert registers(text)[1] == 0x400000 - 20 * 1024


def test_entersvc():
    """DOC, UTIL 2.4.23; spec part 2: returns in SVC mode. The dump's mode line says so."""
    with Aes() as aes:
        aes.boot()
        aes.program(PROGRAM, [(0xEF000016, "SWI &16"), UNDEFINED])
        assert fact("said.mode_svc") in aes.command("GO")


def test_buff_and_nobuff():
    """SRC, Super1 BUFF, NOBUFF; DOC, UTIL 2.3: SetEnv through the Executive's own commands. The
    limit is all of RAM off, and 20 KB less on."""
    with Aes() as aes:
        aes.boot()
        assert fact("said.nobuff") + "\n\r" in aes.command("NOBUFF")
        assert fact("said.buff") + "\n\r" in aes.command("BUFF")


def test_nobuff_gives_a_low_handle(tmp_path):
    """DOC, UTIL 2.4.14; Kernel:705-765: with buffering off, input gets the host's own handle."""
    disc = disc_with(tmp_path, "D1", 1)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.poke_text(TEXT, b"D1\r")
        aes.command("NOBUFF")
        aes.program(PROGRAM, [*OPEN_INPUT[:3], UNDEFINED])
        assert 0 < registers(aes.command("GO"))[0] < 256


def test_two_saves_in_a_row(tmp_path):
    """The ARM sends one byte past the end of a type 0 save, and it stays in R3 (FIQP0 has no count:
    SRC, Middle:485-487, ROM &1424). The host reads R3 twice before its sync (INF, Host230.lst
    &045D), so the next save should start clean: both files whole, not one byte off (DOC, AN p.26).
    A different byte follows each saved block, so a leak would show."""
    disc = disc_with(tmp_path, "D16", 16)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.command("*LOAD D16 8000")
        aes.command("*SAVE A1 8000 +8")
        aes.command("*SAVE A2 8008 +8")
        aes.command("*LOAD D16 9000")
        assert aes.memory(0x9000, 16) == pattern(16)
    assert on_disc(disc, "A1") == pattern(16)[:8]
    assert on_disc(disc, "A2") == pattern(16)[8:]


def test_hard_break_copies_the_language_again():
    """Ctrl-Break after the boot. INF, Host230.lst &0491: type 7 from the host again, 16 KB,
    polled by FIQP7 on bit 7 of R3STAT (Kernel:1018-1024). Super1:252-257: X is not 0, and the
    prompt returns. The R3 count stands for the data here: test_language_copy_arrives_whole
    checks the bytes. The trace is read every emulated millisecond, because the copy overruns the
    ring at the usual rate, so the wait below sees every byte the ARM sends from the Break on."""
    with Aes() as aes:
        aes.boot()
        aes.settle(2)
        aes.chunk = 0.001
        aes.wire.clear()
        aes.bbc.debugger.ensure_running()
        aes.bbc.keyboard.ctrl_break()

        def back():
            return aes.at_prompt() and bytes(start_call()) in bytes(aes.wire.parasite_wrote(2))

        assert aes.run(back, 20), aes.screen()
        # Beebium zeroes its counters at a Break (DOC, TubeInspection.hpp; soft Break too, seen 1 Nov
        # 2026), so this is the new copy.
        assert aes.bbc.tube_ula.state.counters.r3_h2p_writes == 16384
