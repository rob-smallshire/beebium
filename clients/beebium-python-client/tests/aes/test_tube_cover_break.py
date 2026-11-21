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
# test_tube_cover_break.py, tests the cache and soft Break
# version 2.0.0, 20 November 2026.

"""The cache and soft Break, through the running Executive. Written as test_tube_cover_io.py
says, which gives the rules for them."""
from aes_host import Aes, conversation, copy_frozen, expected_banner, fact, frozen_unchanged, start_call
from cover_support import calls_of, words


# Cache and soft Break.
def test_cached_name_needs_no_host(tmp_path):
    """Spec 1.2; SRC, Kernel SCLI, Super2 CACHE: the program runs from high memory, so typing its
    name sends no OSCLI and no transfer. The limit falls (`Memory limit now &`)."""
    limit = fact("text.memory_limit")
    disc = copy_frozen("AESTESTS", tmp_path)
    with Aes(disc=disc) as aes:
        aes.boot()
        assert limit in aes.command("CACHE T12ENV TE")
        text = aes.command("TE")
        assert "T12 DONE" in text
        assert calls_of(aes, 0x02) == [] and aes.wire.host_wrote(4) == []
    assert frozen_unchanged("AESTESTS")


def soft_break(aes):
    """Break alone, no Ctrl. Ends at the new prompt."""
    aes.wire.clear()
    aes.bbc.debugger.ensure_running()
    aes.bbc.keyboard.press_break()

    def back():
        return aes.at_prompt() and bytes(start_call()) in bytes(aes.wire.parasite_wrote(2))

    assert aes.run(back, 20), aes.screen()
    aes.settle()
    screen = bytes(aes.bbc.memory.address.peek[0x7C00:0x8000])
    return screen


def test_soft_break_keeps_ram_and_copies_no_language():
    """Soft Break. INF, Host230.lst &048F: no language copy, the release and &80 only. SRC,
    Super1:252-257 (OSBYTE 253, X = 0): straight to the supervisor. Super1:193: with no cache the
    start is not `Soft start`, because the cache pointer is not the RAM limit. RAM above &E00
    stays: SRC that sizing puts back each word it tests, 1 KB apart from &1000, &2000 among them
    (Super1:168-184), and that only low memory is recopied (COPYLOOP); INF that DRAM holds."""
    soft_start = fact("text.soft_start")
    with Aes() as aes:
        aes.boot()
        aes.poke(0x2000, [0x12345678])    # data, to see it survive
        screen = soft_break(aes)
        assert expected_banner().encode() in screen
        assert soft_start.encode() not in screen
        assert aes.wire.host_wrote(3) == []
        assert aes.wire.host_wrote(2)[0] == 0x80
        (osbyte, reply), = [item for item in conversation(aes.wire) if item[0] == start_call()]
        assert reply[2] == 0, "X: the last Break was soft"
        aes.hook_oswrch()
        assert words(aes.memory(0x2000, 4)) == [0x12345678]


def test_soft_break_with_a_cache_is_a_soft_start(tmp_path):
    """SRC, Super1:193-215, the ROM's `Soft start ` text: with a cache the pointer equals the RAM
    limit, so the RAM limit is kept, the memory limit is worked out again from it (Super1:205-206)
    and `Soft start ` comes before the banner. The cached program
    still runs (Super1:233 onwards keeps CACHENAMES)."""
    soft_start = fact("text.soft_start")
    disc = copy_frozen("AESTESTS", tmp_path)
    with Aes(disc=disc) as aes:
        aes.boot()
        aes.command("CACHE T12ENV TE")
        screen = soft_break(aes)
        assert soft_start.encode() + b" " + expected_banner().encode() in screen
        aes.hook_oswrch()
        assert "T12 DONE" in aes.command("TE")
    assert frozen_unchanged("AESTESTS")
