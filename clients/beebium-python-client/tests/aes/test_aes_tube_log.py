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
# test_aes_tube_log.py, tests the counting wrapper in the server
# version 2.0.0, 20 November 2026.

"""The counting wrapper in a real server. Its banner check restores what an earlier banner test had
to weaken, because Beebium's trace leaves out R1."""
import tempfile
from pathlib import Path

from aes_host import Aes, expected_banner
from tube_log import R1DATA, mapping_faults, read_rows, values


def test_banner_byte_by_byte_on_r1():
    """AN p.28; ROM file at &1938; Super1:192, 215-246: LF, CR, the words, LF, CR, LF, CR, one
    zero, in that order at the chip, with every access at the address that selects its register."""
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "tube.log"
        with Aes(tube_log=path) as aes:
            aes.boot()
        rows = read_rows(path)
    assert mapping_faults(rows) == []
    banner = expected_banner().encode()
    wanted = [10, 13, *banner, 10, 13, 10, 13, 0]
    assert values(rows, "W", R1DATA)[:len(wanted)] == wanted


def test_ordinary_plugin_writes_no_log():
    """The wrapper is in the test build only (USER, 2 Nov 2026), so asking the ordinary plugin
    for a log gets none."""
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "tube.log"
        with Aes(tube_log=path, log_build=False) as aes:
            aes.boot()
        assert not path.exists()


def test_a_prompt_command_is_counted():
    """AN p.22: a command at A* goes out as &02, the text, CR, and it is in the log in order."""
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "tube.log"
        with Aes(tube_log=path) as aes:
            aes.boot()
            aes.command("*FX 0")
        rows = read_rows(path)
    assert b"\x02FX 0\r" in bytes(values(rows, "W", 3))
