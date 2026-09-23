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

"""Integration tests for the Model B with the Computech Integra-B board.

These drive the board's own IBOS ROM, so they check the emulated hardware the
way IBOS sees it: its *ROMS listing detects write-protected RAM by trying to
write, *TIME reads the real-time clock, and shadow modes rely on the SHEN and
MEMSEL latches.
"""

from __future__ import annotations

import datetime
import re
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError, ServerStartupError
from beebium.client.sideways import ProtectionKind, SlotStatusReport

VARIANT = "model-b-integra-b"
DISC_ARGS = ["--fdc", "acorn-1770",
             "--sideways", "slot=1:type=rom:image=acorn-dfs_2_26.rom"]


def _launch(beebium_server_filepath: Path | None, extra_args: list[str]):
    return Beebium.launch(
        server=beebium_server_filepath,
        variant=VARIANT,
        extra_args=DISC_ARGS + extra_args,
    )


def _group(status: SlotStatusReport, group_id: str):
    for g in status.protection_groups:
        if g.id == group_id:
            return g
    return None


def _command(bbc: Beebium, command: str, expect: str) -> str:
    """Type a command at the BASIC prompt and wait for `expect` on screen."""
    bbc.keyboard.type(command + "\r")
    bbc.keyboard.wait_until_typing_complete()
    bbc.expect(expect, timeout=10.0, sample_interval_seconds=0.2)
    return bbc.video.screen_text().text


@pytest.fixture
def integra_b(beebium_server_filepath: Path | None):
    try:
        with _launch(beebium_server_filepath, []) as instance:
            instance.expect(">", timeout=20.0, sample_interval_seconds=0.2)
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_boots_as_a_set_up_board(integra_b):
    """The battery-backed state is that of a board that has been set up, so
    IBOS starts BASIC (configured LANG 3) rather than stopping at Language?."""
    screen = integra_b.video.screen_text().text
    assert "INTEGRA-B 128K" in screen
    assert "DFS" in screen
    assert "BASIC" in screen


def test_roms_lists_the_board_ram_banks(integra_b):
    screen = _command(integra_b, "*ROMS", "  0 (")
    assert re.search(r"15 \(\s*SL\) IBOS", screen)
    for bank in (4, 5, 6, 7):
        # 'E': write-enabled RAM, detected by IBOS writing to the bank.
        assert re.search(rf"\b{bank} \(E", screen), screen


def test_ibos_sees_a_write_protected_ram_chip(integra_b):
    integra_b.sideways.set_protection("slots-4-5", ProtectionKind.WRITE_PROTECT, True)
    screen = _command(integra_b, "*ROMS", "  0 (")
    for bank in (4, 5):
        assert re.search(rf"\b{bank} \(P", screen), screen   # write-protected
    for bank in (6, 7):
        assert re.search(rf"\b{bank} \(E", screen), screen   # other chip unaffected


def test_shadow_mode_frees_main_memory_and_keeps_the_screen(integra_b):
    """In a shadow mode the CPU sees shadow RAM at &3000-&7FFF, so BASIC gets
    HIMEM=&8000, while the display still shows main (screen) memory."""
    screen = _command(integra_b, "MODE 135:PRINT ~HIMEM", "8000")
    assert "8000" in screen


def test_time_follows_the_host_clock(integra_b):
    today = datetime.datetime.now()
    screen = _command(integra_b, "*TIME", str(today.year))
    # e.g. "Wed,23 Sep 2026.18:20:54"
    assert f"{today:%b} {today.year}" in screen


def test_protection_groups_are_per_ram_chip(integra_b):
    status = integra_b.sideways.get_slot_status()
    ids = [g.id for g in status.protection_groups]
    assert ids == ["slots-4-5", "slots-6-7"]
    g = _group(status, "slots-4-5")
    assert list(g.slots) == [4, 5]
    assert g.supports_write_protect
    assert not g.supports_hide
    assert not g.write_protected

    integra_b.sideways.set_protection("slots-6-7", ProtectionKind.WRITE_PROTECT, True)
    assert _group(integra_b.sideways.get_slot_status(), "slots-6-7").write_protected


def test_write_protect_launch_flag(beebium_server_filepath: Path | None):
    try:
        with _launch(beebium_server_filepath, ["--write-protect", "slots-6-7"]) as bbc:
            status = bbc.sideways.get_slot_status()
            assert _group(status, "slots-6-7").write_protected
            assert not _group(status, "slots-4-5").write_protected
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_socket_pair_fitted_with_ram(beebium_server_filepath: Path | None):
    try:
        with _launch(beebium_server_filepath,
                     ["--sideways", "slot=8:type=ram",
                      "--sideways", "slot=9:type=ram"]) as bbc:
            bbc.expect(">", timeout=20.0, sample_interval_seconds=0.2)
            assert _group(bbc.sideways.get_slot_status(), "slots-8-9") is not None
            # IBOS must be told about RAM fitted in the sockets (IBOS guide 1-5):
            # 15, plus 16 for a chip in socket 9 (banks 8/9).
            bbc.keyboard.type("*FX162,127,31\r")
            bbc.keyboard.wait_until_typing_complete()
            bbc.keyboard.ctrl_break()
            bbc.expect("INTEGRA-B 160K", timeout=20.0, sample_interval_seconds=0.2)
            screen = _command(bbc, "*ROMS", "  0 (")
            for bank in (8, 9):
                assert re.search(rf"\b{bank} \(E", screen), screen
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_ram_in_half_a_socket_pair_is_rejected(beebium_server_filepath: Path | None):
    try:
        with _launch(beebium_server_filepath, ["--sideways", "slot=9:type=ram"]):
            pass
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    except ServerStartupError as e:
        assert "share one RAM chip" in str(e)
        return
    pytest.fail("expected ServerStartupError for RAM in only one slot of a pair")
