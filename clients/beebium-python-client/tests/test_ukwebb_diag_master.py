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

"""The Master 128 against the UKWEBB.NET diagnostic ROM, when it is at hand.

The ROM is a version 1 diagnostic and may itself be at fault: where it and the
emulator disagree, the failure says so, and the finding may be the ROM's.

The ROM is private and is never committed. These tests run only when the
environment variable BEEBIUM_UKWEBB_DIAG_DIRPATH names a directory holding its
two images: the 16K MOS half (fitted as the MOS) and the bank 9 image. They
assert only what the emulation defends on its own grounds: the RAM regions
and hardware lines pass, the summary has no fails or warnings, and the CMOS
bytes it shows are this server's Welcome Guide seed.

The ROM is its own OS and scans the keyboard matrix itself, so a key is held
down for a while (in emulated time) rather than typed.
"""

from __future__ import annotations

import os
import re
from pathlib import Path

import pytest

from beebium.client import Beebium

DIRPATH_VARIABLE = "BEEBIUM_UKWEBB_DIAG_DIRPATH"
MOS_FILENAME = "ukwebb_diag_master_16k.rom"
BANK_9_FILENAME = "ukwebb_diag_master_bank9.rom"

ROM_MAY_BE_AT_FAULT = (
    "The diagnostic ROM is a version 1 and may itself be at fault, so check it "
    "against a real Master before changing the emulation."
)

# Emulated seconds: the RAM tests take about 8, the hardware tests are done
# by about 14.
RAM_SCREEN_SECONDS = 30.0
SUMMARY_SECONDS = 45.0
SUBMENU_SECONDS = 10.0
KEY_HOLD_SECONDS = 0.4
# The menu's text is on the screen a moment before its loop reads keys.
MENU_SETTLE_SECONDS = 1.0

RAM_REGIONS = ["ANDY", "HAZEL", "SHADOW", "SWRAM 4", "SWRAM 5", "SWRAM 6", "SWRAM 7"]

HARDWARE_LINES = [
    "RAM main + extra",
    "Sys VIA regs+timer",
    "User VIA regs+timer",
    "IRQ to CPU (via T1)",
    "CRTC VSYNC frame",
    "CRTC R14/R15 r/w",
    "Keys stuck down",
    "ADC 4 ch + EOC",
    "RTC clock ticking",
    "CMOS RAM 50 bytes",
    "Serial 6850 + ULA",
    "1770 FDC registers",
    "Drive 0 trk0 / speed",
]

# The server's Welcome Guide CMOS seed (Master128CmosSeed.hpp), CMOS RAM
# bytes 14-63; the ROM shows these as "CMOS RAM 14-63", ten to a row.
WELCOME_GUIDE_CMOS = [0x00] * 50
for index, value in {
    5: (12 << 4) | 13,  # LANG 12, FILE 13
    6: 0xFF,  # every ROM inserted, 0-7
    7: 0xFF,  # and 8-15
    10: 0x17,  # TV 0,1; MODE 7
    11: 0xE3,  # FLOPPY, NODIR, CAPS, FDRIVE 3
    12: 50,  # DELAY 50
    13: 10,  # REPEAT 10
    14: 10,  # IGNORE 10
    15: 0x2D,  # PRINT 1, BAUD 4, TUBE
    16: 0x82,  # DATA 4, LOUD; INTUBE, SCROLL, NOBOOT
}.items():
    WELCOME_GUIDE_CMOS[index] = value


@pytest.fixture(scope="module")
def diag_rom_dirpath() -> Path:
    """The directory holding the diagnostic ROM's images, or a skip."""
    value = os.environ.get(DIRPATH_VARIABLE)
    if not value:
        pytest.skip(
            f"Set {DIRPATH_VARIABLE} to a directory holding {MOS_FILENAME} and "
            f"{BANK_9_FILENAME} to run the diagnostic ROM tests."
        )
    dirpath = Path(value)
    missing = [name for name in (MOS_FILENAME, BANK_9_FILENAME) if not (dirpath / name).is_file()]
    if missing:
        pytest.skip(
            f"{DIRPATH_VARIABLE} names {dirpath}, which does not hold {', '.join(missing)}."
        )
    return dirpath


def diag_args(dirpath: Path, *extra: str) -> list[str]:
    return [
        "--mos",
        str(dirpath / MOS_FILENAME),
        "--sideways",
        f"slot=9:type=rom:image={dirpath / BANK_9_FILENAME}",
        *extra,
    ]


@pytest.fixture
def diag_master(launch_bbc, diag_rom_dirpath: Path) -> Beebium:
    """A Master 128 running the diagnostic ROM from power-on."""
    return launch_bbc(
        preset="master-128", variant="master-128", extra_args=diag_args(diag_rom_dirpath)
    )


def screen(bbc: Beebium) -> str:
    return bbc.basic.read_screen_text()


def run_until_screen_shows(bbc: Beebium, text: str, emulated_seconds: float) -> str:
    """Run until the screen shows `text`; return the screen, or fail saying what it showed."""
    if not bbc.run_until_or_timeout(lambda: text in screen(bbc), emulated_seconds):
        pytest.fail(
            f"The screen did not show {text!r} within {emulated_seconds} emulated "
            f"seconds; it showed:\n{screen(bbc)}\n{ROM_MAY_BE_AT_FAULT}"
        )
    return screen(bbc)


def wait_for_menu(bbc: Beebium) -> str:
    """Run until the menu is up and reading keys; return its screen."""
    text = run_until_screen_shows(bbc, "SPACE=Re-test", SUMMARY_SECONDS)
    bbc.run_for_emulated_seconds(MENU_SETTLE_SECONDS)
    return text


def hold_key(bbc: Beebium, key: str) -> None:
    """Hold `key` down long enough for the ROM's own matrix scan to see it."""
    bbc.keyboard.key_down(key)
    try:
        bbc.run_for_emulated_seconds(KEY_HOLD_SECONDS)
    finally:
        bbc.keyboard.key_up(key)


def line_with(text: str, label: str) -> str:
    for line in text.splitlines():
        if line.strip().startswith(label):
            return line
    pytest.fail(f"No line starts with {label!r} on the screen:\n{text}\n{ROM_MAY_BE_AT_FAULT}")


def test_ram_tests_pass_every_region(diag_master: Beebium):
    text = run_until_screen_shows(diag_master, "Hardware tests next", RAM_SCREEN_SECONDS)

    main = line_with(text, "Main RAM")
    assert main.rstrip().endswith("OK"), f"{main}\n{text}\n{ROM_MAY_BE_AT_FAULT}"
    for region in RAM_REGIONS:
        line = line_with(text, region)
        assert line.rstrip().endswith("OK"), f"{line}\n{text}\n{ROM_MAY_BE_AT_FAULT}"


def test_hardware_summary_passes_every_line(diag_master: Beebium):
    text = run_until_screen_shows(diag_master, "SPACE=Re-test", SUMMARY_SECONDS)

    for label in HARDWARE_LINES:
        line = line_with(text, label)
        verdict = line.strip()[len(label) :].split()
        assert verdict[:1] == ["OK"], f"{line}\n{text}\n{ROM_MAY_BE_AT_FAULT}"
    assert re.search(r"Fail\s+00\s+Warn\s+00", text), f"{text}\n{ROM_MAY_BE_AT_FAULT}"


def test_cmos_submenu_shows_the_welcome_guide_seed(diag_master: Beebium):
    wait_for_menu(diag_master)
    hold_key(diag_master, "c")
    text = run_until_screen_shows(diag_master, "CMOS RAM 14-63", SUBMENU_SECONDS)

    lines = text.splitlines()
    heading = next(i for i, line in enumerate(lines) if "CMOS RAM 14-63" in line)
    shown = [int(byte, 16) for line in lines[heading + 1 : heading + 6] for byte in line.split()]
    assert shown == WELCOME_GUIDE_CMOS, (
        f"The CMOS bytes shown are not the Welcome Guide seed:\n{text}\n{ROM_MAY_BE_AT_FAULT}"
    )
