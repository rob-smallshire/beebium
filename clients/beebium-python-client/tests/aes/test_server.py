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
# test_server.py, tests the plugin in the built server
# version 2.0.0, 20 November 2026.

"""The AES in a real Beebium server, started the way a user starts it.

Always names the built server (notes/beebium-baseline.md): nothing is discovered. Tied to the
ROM through aes_host.expected_banner, RomCheck.hpp's check. Says what Beebium's server does with
the plugin; a result here
is not evidence of what an AES does.
"""
import json
import os
import subprocess

import pytest

from aes_host import DFS, PLUGIN_FOLDER, ROM, SERVER, Aes, expected_banner, fact, launch, need_rom

from beebium.client.screen import read_mode7_screen, screen_contains

MANIFEST = PLUGIN_FOLDER / "manifest.json"
# DOC, profile host.model: BBC B with the 1770. The host's Tube code is in the filing system
# ROM, so one must be fitted (DOC, Beebium's docs/tube-subsystem.md). DFS 2.26 for now: the profile's
# ADFS 1.30, and ADFS with DFS, are tried outside these tests, on the owner's own discs.
HOST = ["--fdc", "acorn-1770", "--sideways", f"slot=14:type=rom:image={DFS}"]
BOOT_SECONDS = 10.0


def tube_args(*settings):
    need_rom()
    return ["--tube-aes", ":".join([f"rom={ROM}", *settings])]


def server(*arguments):
    return subprocess.run([SERVER, *arguments], capture_output=True, text=True, timeout=30)


def banner_shows(extra_args, text):
    with launch(extra_args) as bbc:
        seen = bbc.run_until_or_timeout(lambda: screen_contains(bbc, text), emulated_seconds=BOOT_SECONDS)
        if not seen:
            print("\n".join(read_mode7_screen(bbc)))
        return seen


def test_host_has_a_filing_system():
    """Without this the banner test would fail on any plugin, and say nothing about ours."""
    with launch(HOST) as bbc:
        assert bbc.run_until_or_timeout(lambda: screen_contains(bbc, "Acorn 1770 DFS"), emulated_seconds=BOOT_SECONDS)


def test_listed():
    """DOC, Beebium guide section 8: list-extensions shows our flag."""
    assert "tube-aes" in server("list-extensions").stdout


def test_described():
    """DOC, Beebium guide section 8: describe-extension shows the settings. Changed 1 Nov 2026
    (USER): it was every setting, 45 of them; now the three, and no behaviour switch."""
    text = server("describe-extension", "tube-aes").stdout
    keys = [row["key"] for row in json.loads(MANIFEST.read_text())["parameters"]]
    assert keys == ["rom", "ram", "abort"]
    for key in keys:
        assert f"\n{key}\t" in text
    assert "time.rom_wait" not in text


@pytest.mark.parametrize("ram_mb", [4, 2])
def test_banner(ram_mb):
    """USER, 1 Nov 2026: the banner on the BBC's screen, from the ROM bytes and ram."""
    assert banner_shows(HOST + tube_args(f"ram={ram_mb}"), expected_banner(ram_mb))


@pytest.mark.parametrize("ram_mb", [4, 2])
def test_banner_strict(ram_mb):
    """USER, 4 Nov 2026: with abort strict the banner still shows, because checking waits for user mode."""
    assert banner_shows(HOST + tube_args(f"ram={ram_mb}", "abort=strict"), expected_banner(ram_mb))


@pytest.mark.parametrize("setting, shown, not_shown", [("abort=strict", "go_unmapped_report", "text.branch_zero"),
                                                     ("abort=off", "text.branch_zero", "go_unmapped_report")])
def test_strict_reaches_the_plugin(setting, shown, not_shown):
    """USER, 4 Nov 2026: abort=strict on Beebium's command line reaches the plugin; the banner alone
    cannot show it. GO 400000, 4 MB: GO reads byte 7 of the target in user mode (SRC, ROM &3F8C;
    Kernel SCLIGO), outside the map, so the data abort report. With abort off, &400000 repeats
    address 0 (INF, ram.repeat), which branches to RESET1 (SRC, Kernel). Which report GO gives is
    the ROM's: its facts file's go_unmapped_report names the text. key (data abort on 1.01)."""
    shown, not_shown = (fact(fact(key)) if key == "go_unmapped_report" else fact(key) for key in (shown, not_shown))
    with Aes(settings=("ram=4", setting)) as aes:
        out = aes.boot().command("GO 400000")
    assert shown in out and not_shown not in out, out


def test_bundled_rom_is_the_default():
    """The Executive ROM ships with the plugin, so --tube-aes alone boots it."""
    if os.environ.get("BEEBIUM_AES_ROM_FILEPATH"):
        pytest.skip("BEEBIUM_AES_ROM_FILEPATH names another image than the bundled one")
    need_rom()
    assert banner_shows(HOST + ["--tube-aes"], expected_banner(4))


def test_short_rom_refused(tmp_path):
    """USER, Plugin option 4 and 1 Nov 2026: exactly 16 KB, and the message names the path, the
    size found and the size wanted. Replaces a check for "16 KB": these words say more."""
    short = tmp_path / "short.rom"
    short.write_bytes(bytes(100))
    done = server(*HOST, "--tube-aes", f"rom={short}")
    assert done.returncode != 0
    assert f"{short} is 100 bytes; a ROM must be exactly 16,384" in done.stdout + done.stderr


def test_missing_rom_refused(tmp_path):
    """USER, 1 Nov 2026: Beebium does not check that the file exists, so the plugin says so."""
    missing = tmp_path / "none.rom"
    done = server(*HOST, "--tube-aes", f"rom={missing}")
    assert done.returncode != 0
    assert f"no file at {missing}" in done.stdout + done.stderr


def test_unreadable_rom_refused(tmp_path):
    """USER, 1 Nov 2026: something there that cannot be read as a file. A folder stands for
    every such case; the message does not tell one from another."""
    done = server(*HOST, "--tube-aes", f"rom={tmp_path}")
    assert done.returncode != 0
    assert f"could not read {tmp_path} as a file" in done.stdout + done.stderr


def test_bad_choice_refused():
    """DOC, Beebium framework: the manifest's choices are enforced before a machine is built."""
    done = server(*HOST, *tube_args("ram=3"))
    assert done.returncode != 0
    assert "parameter 'ram' must be one of" in done.stdout + done.stderr


@pytest.mark.parametrize("setting", ["ram=1", "abort=empty"])
def test_removed_choice_refused(setting):
    """USER, 5 Nov 2026: ram 1 and abort empty are no longer choices, so the server refuses them."""
    done = server(*HOST, *tube_args(setting))
    assert done.returncode != 0
    assert f"parameter '{setting.split('=')[0]}' must be one of" in done.stdout + done.stderr


def test_behaviour_switch_is_not_a_plugin_setting():
    """USER, 1 Nov 2026: the switches are changed in the code, not on the command line."""
    done = server(*HOST, *tube_args("time.rom_wait=5"))
    assert done.returncode != 0
    assert "unknown parameter 'time.rom_wait'" in done.stdout + done.stderr


def test_saved_machine_reopens_to_the_banner(tmp_path):
    """A machine created and opened for the first time reaches the banner with its settings
    kept. It has never run, so this says nothing of one saved after running (INF, guide)."""
    machine = tmp_path / "AES.beebium-machine"
    made = server("create-machine", "--output", str(machine), *HOST, *tube_args("ram=2"))
    assert made.returncode == 0, made.stdout + made.stderr
    assert banner_shows(["--machine", str(machine)], expected_banner(2))
