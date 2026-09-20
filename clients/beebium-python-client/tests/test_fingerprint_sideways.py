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

"""Dabs Press Fingerprint installs into slot-15 sideways RAM on model-b-atpl-sidewise.

This is the end-to-end proof for the ATPL Sidewise board's defining feature and
the close-out for issue #72. Fingerprint's Model B install path (menu option 1)
loads its ROM image with a bare ``*LOAD SMON FFFF8000`` -- a write straight to
&8000-&BFFF while BASIC, not the target bank, is the paged ROM. It never pages
the target bank in first, so it only works on a board whose writes to the
sideways region reach RAM regardless of ROMSEL. The ATPL Sidewise board is
exactly that: slot 15 (its only RAM slot) is write-through -- a write to
&8000-&BFFF lands in slot-15 RAM whatever ROM is paged, while reads stay
ROMSEL-gated.

The test boots Fingerprint on ``model-b-atpl-sidewise``, drives its menu to
install the full sideways-RAM build into bank 15, and then proves the install
took by asking the MOS for help: ``*HELP FI.`` only lists Fingerprint's commands
if slot-15 RAM now holds a ROM the MOS scans as a service ROM -- i.e. only if the
write-through actually delivered the image. That second step is the load-bearing
assertion; the "Installed" banner alone is just the game's own optimism.

The menu wants the bank as a single hex nibble, so bank 15 is entered as ``F``
(typing "15" yields "Invalid hex"). Navigation polls the screen-text API rather
than blind-timing keypresses, following the Firetrack auto-boot tests.
"""

from __future__ import annotations

import hashlib
import shutil
import threading
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ScreenExpectTimeout, ServerNotFoundError

# Dabs Press Fingerprint (David Spencer / Dabs Press, 1987), from the issue #72
# report (stardot download file id 121282). Committed space-free, consistent
# with the other game discs here and sidestepping the DiscUrl spaced-path bug.
FINGERPRINT_DISC_FILENAME = "DabsPressFingerprint.ssd"
FINGERPRINT_DISC_SHA256 = "baeccaf6b0cf6135818c7f6aeddd0448bd27eb1534412f828d870090f802271d"

# Menu landmarks (Fingerprint Menu V2.0, drawn in MODE 1).
MENU_OPTION_1 = "Install full Sideways RAM"
BANK_PROMPT = "Which sideways RAM bank"
INSTALLED_BANNER = "Installed. *HELP FI. for commands"

# Bank 15 is entered as the hex nibble 'F'.
BANK_15_KEY = "F"

# Fingerprint's own *HELP FI. output -- proof the service ROM in slot-15 RAM ran.
HELP_BANNER = "Fingerprint 3.22"
HELP_COMMAND = "Set breakpoint"


@pytest.fixture(scope="module")
def fingerprint_disc_filepath() -> Path:
    """The committed Fingerprint disc, its integrity pinned by SHA-256."""
    repo_root = Path(__file__).parents[3]
    path = repo_root / "tests" / "assets" / "discs" / FINGERPRINT_DISC_FILENAME
    if not path.exists():
        pytest.skip(f"Fingerprint disc image not found: {path}")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert digest == FINGERPRINT_DISC_SHA256, (
        f"Fingerprint disc {path} has SHA-256 {digest}, expected {FINGERPRINT_DISC_SHA256}"
    )
    return path


@pytest.fixture
def bbc_fingerprint(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    fingerprint_disc_filepath: Path,
) -> Beebium:
    """model-b-atpl-sidewise, DFS in slot 13, slot 15 as RAM, auto-booting Fingerprint.

    The board reserves slot 15 for RAM and defaults its language slot to 14, so
    BASIC lands at 14 and DFS is placed at 13 -- exactly the shipped
    model-b-atpl-sidewise-disc topology.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            variant="model-b-atpl-sidewise",
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"slot=13:type=rom:image={dfs_1770_rom_filepath}",
                "--sideways",
                "slot=15:type=ram",
                "--floppy",
                f"0:{fingerprint_disc_filepath}",
                "--auto-boot",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.debugger.ensure_running()
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _drive(bbc: Beebium, keys: str, landmark: str, *, timeout: float = 20.0, attempts: int = 4) -> None:
    """Type `keys`, wait for `landmark`; re-drive if the press was dropped.

    A press dropped on a slow host leaves the screen on the prior landmark, so
    the wait times out and the keys are re-sent. The wait returns the instant
    the landmark appears, so a press that did register is never doubled.
    """
    deadline = time.monotonic() + timeout
    last_error: ScreenExpectTimeout | None = None
    for _ in range(attempts):
        bbc.keyboard.type(keys)
        remaining = max(3.0, deadline - time.monotonic())
        try:
            bbc.expect(landmark, timeout=remaining)
            return
        except ScreenExpectTimeout as error:
            last_error = error
            if time.monotonic() >= deadline:
                break
    assert last_error is not None
    raise last_error


def test_fingerprint_installs_into_slot15_sideways_ram(bbc_fingerprint: Beebium) -> None:
    """Install Fingerprint into bank 15 and prove the service ROM answers *HELP."""
    bbc = bbc_fingerprint

    # !BOOT chains INTRO, which draws its menu (self-correcting PAGE first).
    bbc.expect(MENU_OPTION_1, timeout=30.0)

    # Option 1: install the full sideways-RAM build.
    _drive(bbc, "1\r", BANK_PROMPT, timeout=15.0)

    # Bank 15 == hex 'F'. The install performs *LOAD SMON FFFF8000 with BASIC
    # paged in; on the ATPL board the write-through routes it to slot-15 RAM.
    _drive(bbc, f"{BANK_15_KEY}\r", INSTALLED_BANNER, timeout=20.0)

    # Criterion 1: the game reports the install completed.
    assert INSTALLED_BANNER in bbc.video.screen_text().text

    # Criterion 2 (load-bearing): the MOS scans slot 15 as a service ROM and
    # Fingerprint answers *HELP FI. with its command listing -- only possible
    # if the write-through actually delivered the ROM image into slot-15 RAM.
    _drive(bbc, "*HELP FI.\r", HELP_BANNER, timeout=15.0)
    help_text = bbc.video.screen_text().text
    assert HELP_BANNER in help_text, help_text
    assert HELP_COMMAND in help_text, help_text


# ---------------------------------------------------------------------------
# A wedged guest must never take the server down (issue #72, crash path)
#
# Installing a "service ROM" into a bank with no real RAM behind it is the #72
# crash: the write goes nowhere, but the menu still pokes the MOS ROM-type table
# to advertise a ROM in that bank. The empty bank reads 0xFF, so the next
# paged-ROM service scan (any * command) makes the MOS jump into a bogus service
# entry and the 6502 wanders into a tight loop in RAM. That is faithful guest
# behaviour -- a real BBC does the same -- and it is unrecoverable from the
# guest's side without a Break. What must NOT happen is the emulator SERVER
# process dying or its gRPC surface hanging: a guest executing garbage is the
# emulator working, not failing. This test drives the guest into that wedge on a
# plain Model B and asserts the server stays alive and responsive.
#
# It deliberately does not assert Break recovery or the exact wander target
# (&2551 in practice): those are faithful MOS behaviour and would make the test
# brittle. The only guarded invariant is server survival.
# ---------------------------------------------------------------------------

# A guest executing below the sideways-ROM window is in RAM -- machine code that
# the MOS/BASIC idle loops never run. Stable PC here is the wedge landmark.
_SIDEWAYS_ROM_BASE = 0x8000
# The MOS ROM-type table; entry N is 0x02A1 + N. The empty bank 0 install writes
# 0xFF here (the byte it read back from the empty bank), the poison marker.
_ROM_TYPE_TABLE = 0x02A1


@pytest.fixture
def bbc_fingerprint_manual_boot(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    fingerprint_disc_filepath: Path,
    tmp_path: Path,
) -> Beebium:
    """A plain Model B with the Fingerprint disc mounted but NOT auto-booted.

    Auto-boot is left off deliberately: with it on, a stray Break would re-run
    the disc's !BOOT and re-enter the menu, muddying the state. The test boots
    once itself with a Shift-Break. The disc is copied into the test's own tmp
    dir so the run is hermetic (unique port comes from ``port=0``).
    """
    disc_copy_filepath = tmp_path / FINGERPRINT_DISC_FILENAME
    shutil.copyfile(fingerprint_disc_filepath, disc_copy_filepath)
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            variant="model-b",
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"slot=13:type=rom:image={dfs_1770_rom_filepath}",
                "--floppy",
                f"0:{disc_copy_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.debugger.ensure_running()
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _rpc_responds_within(call, timeout: float) -> tuple[bool, object]:
    """Run ``call()`` on a helper thread; report whether it returned in time.

    A dead server makes the RPC raise quickly; a hung one blocks. Bounding it on
    a thread lets the test distinguish "responded" from "still hanging" without
    the whole test just timing out.
    """
    box: dict[str, object] = {}

    def run() -> None:
        try:
            box["value"] = call()
        except Exception as error:  # noqa: BLE001 - reported, not raised, on the thread
            box["error"] = error

    thread = threading.Thread(target=run, daemon=True)
    thread.start()
    thread.join(timeout)
    if thread.is_alive():
        return False, "timed out"
    if "error" in box:
        return False, box["error"]
    return True, box.get("value")


def _peek_byte(bbc: Beebium, address: int) -> int:
    return bytes(bbc.memory.address.peek.read(address, 1))[0]


def test_service_rom_install_into_empty_bank_wedges_guest_not_server(
    bbc_fingerprint_manual_boot: Beebium,
) -> None:
    """Driving the guest into the #72 empty-bank wedge must not down the server."""
    bbc = bbc_fingerprint_manual_boot
    server = bbc._server

    # Boot the disc (Shift-Break) and install the full build into bank 0, which
    # has no RAM behind it on a plain Model B.
    bbc.keyboard.shift_break()
    bbc.expect(MENU_OPTION_1, timeout=30.0)
    _drive(bbc, "1\r", BANK_PROMPT, timeout=15.0)
    _drive(bbc, "0\r", INSTALLED_BANNER, timeout=25.0)

    # The install advertised a ROM in the empty bank: type entry 0 == 0xFF.
    assert _peek_byte(bbc, _ROM_TYPE_TABLE) == 0xFF

    # A * command triggers the paged-ROM service scan that jumps into the bogus
    # bank. Poll until the guest is wandering in RAM (PC below the sideways-ROM
    # window on several consecutive reads) -- the faithful wedge.
    bbc.keyboard.type("*HELP\r")
    deadline = time.monotonic() + 20.0
    consecutive_in_ram = 0
    while time.monotonic() < deadline:
        if 0 <= bbc.cpu.pc < _SIDEWAYS_ROM_BASE:
            consecutive_in_ram += 1
            if consecutive_in_ram >= 5:
                break
        else:
            consecutive_in_ram = 0
        time.sleep(0.2)
    assert consecutive_in_ram >= 5, (
        f"guest did not reach the RAM-wander wedge; last PC=&{bbc.cpu.pc:04X}"
    )

    # THE INVARIANT: the wedged guest has not taken the server down. The process
    # is still up and every gRPC surface still answers promptly.
    assert server.is_running, f"server process exited (code={server.last_exit_code})"

    ok, value = _rpc_responds_within(lambda: bbc.system.protocol_fingerprint, timeout=5.0)
    assert ok, f"SystemService did not respond after the wedge: {value!r}"

    ok, value = _rpc_responds_within(lambda: bbc.debugger.get_state(), timeout=5.0)
    assert ok, f"DebuggerControl did not respond after the wedge: {value!r}"

    ok, value = _rpc_responds_within(lambda: _peek_byte(bbc, _ROM_TYPE_TABLE), timeout=5.0)
    assert ok, f"memory peek did not respond after the wedge: {value!r}"

    # And it is still alive after all that probing.
    assert server.is_running, f"server process exited (code={server.last_exit_code})"
