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
import shutil
import time
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


def _type(bbc: Beebium, *lines: str) -> None:
    """Type each line followed by RETURN, waiting for the typing to finish."""
    for line in lines:
        bbc.keyboard.type(line + "\r")
        bbc.keyboard.wait_until_typing_complete()


def _run(bbc: Beebium, command: str, timeout: float = 30.0) -> None:
    """Type a command at an empty prompt and wait until it has finished, i.e.
    a fresh empty prompt follows it. Use for commands that do disc I/O, where
    typing ahead is unreliable. Clears the screen first so prompts are counted
    reliably."""
    bbc.keyboard.type("CLS\r")
    bbc.keyboard.wait_until_typing_complete()
    bbc.expect(">", timeout=timeout, sample_interval_seconds=0.2)
    bbc.keyboard.type(command + "\r")
    bbc.keyboard.wait_until_typing_complete()
    deadline = time.monotonic() + timeout
    while True:
        lines = [ln.rstrip() for ln in bbc.video.screen_text().text.splitlines() if ln.strip()]
        if lines and lines[-1] == ">" and any(ln.startswith(">" + command[:8]) for ln in lines):
            return
        if time.monotonic() > deadline:
            raise TimeoutError(f"{command!r} did not finish:\n" + "\n".join(lines))
        time.sleep(0.2)


def _rtc_register(bbc: Beebium, register: int) -> int:
    """Read an RTC register through the board's &FE38/&FE3C ports."""
    bbc.memory.address.bus[0xFE38] = register
    return bbc.memory.address.peek[0xFE3C]


def _region_active(bbc: Beebium, name: str) -> bool:
    return next(r for r in bbc.memory.regions if r.name == name).active


def _command(bbc: Beebium, command: str, expect: str) -> str:
    """Type a command at the BASIC prompt and wait for `expect` on screen."""
    bbc.keyboard.type(command + "\r")
    bbc.keyboard.wait_until_typing_complete()
    bbc.expect(expect, timeout=10.0, sample_interval_seconds=0.2)
    return bbc.video.screen_text().text


@pytest.fixture
def scratch_disc_filepath(tmp_path: Path) -> Path:
    """A writable copy of a committed disc with plenty of free space."""
    repo_root = Path(__file__).parent.parent.parent.parent
    source = repo_root / "tests" / "assets" / "discs" / "6502timing.ssd"
    if not source.exists():
        pytest.skip(f"disc image not found: {source}")
    target = tmp_path / "scratch.ssd"
    shutil.copyfile(source, target)
    return target


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


# ---------------------------------------------------------------------------
# IBOS commands (IBOS guide sections 2, 3, 5, 6 and 1-5)
# ---------------------------------------------------------------------------


def test_time_and_date_can_be_set_and_run_on(integra_b):
    _type(integra_b, "*TIME=10:20:30", "*DATE=15/9/26")
    screen = _command(integra_b, "*DATE", "Sep 2026.")
    assert "Tue,15 Sep 2026." in screen
    screen = _command(integra_b, "*TIME", "10:20:")
    assert re.search(r"Tue,15 Sep 2026\.10:20:3\d", screen), screen

    # The clock keeps running, and survives Break (the RTC is battery backed).
    time.sleep(2.0)
    integra_b.keyboard.press_break()
    integra_b.expect(">", timeout=10.0, sample_interval_seconds=0.2)
    _type(integra_b, "CLS")
    screen = _command(integra_b, "*TIME", "Sep 2026")
    match = re.search(r"Tue,15 Sep 2026\.10:20:(\d\d)", screen)
    assert match, screen
    assert int(match.group(1)) >= 32


def test_calendar(integra_b):
    _type(integra_b, "*TIME=12:00:00", "*DATE=15/9/26", "CLS")
    screen = _command(integra_b, "*CALENDAR", "Sat")
    assert "September 2026" in screen
    # September 2026 starts on a Tuesday.
    assert re.search(r"Tue\s+1\s+8\s+15\s+22\s+29", screen), screen


def test_osword_14_reads_the_clock(integra_b):
    _type(integra_b, "*TIME=10:20:30", "*DATE=15/9/26", "CLS",
          "DIM B% 40",
          "?B%=0:A%=14:X%=B% MOD 256:Y%=B% DIV 256:CALL &FFF1:PRINT $B%")
    integra_b.expect("Tue,15 Sep 2026.10:2", timeout=10.0, sample_interval_seconds=0.2)

    # Function 1: BCD year, month, date, day of week, hours, minutes, seconds.
    _type(integra_b, "CLS",
          "?B%=1:CALL &FFF1:PRINT \"BCD\";:FOR I%=0 TO 5:PRINT \" \";~B%?I%;:NEXT")
    screen = _command(integra_b, "", "BCD")
    assert "BCD 26 9 15 3 10 2" in screen, screen


def test_alarm_flashes_the_lock_leds_until_acknowledged(integra_b):
    """An alarm-due flashes CAPS LOCK and SHIFT LOCK alternately (driven by
    the RTC alarm and periodic interrupts) until CTRL+SHIFT is held."""
    _type(integra_b, "*TIME=10:20:30", "*ALARM=10:20:33")
    screen = _command(integra_b, "*ALARM ?", "/ ON")
    assert "10:20:33 / ON" in screen

    def shift_lock_lit() -> bool:
        return integra_b.indicators.get("shift-lock-led") > 0

    deadline = time.monotonic() + 15.0
    while not shift_lock_lit():
        assert time.monotonic() < deadline, "alarm never flashed SHIFT LOCK"
        time.sleep(0.1)

    kb = integra_b.keyboard
    kb.ctrl_down()
    kb.shift_down()
    time.sleep(1.5)
    kb.shift_up()
    kb.ctrl_up()
    time.sleep(1.0)
    assert integra_b.indicators.get("shift-lock-led") == 0
    assert integra_b.indicators.get("caps-lock-led") == 255


def test_configuration_survives_ctrl_break(integra_b):
    _type(integra_b, "*CONFIGURE MODE 3")
    integra_b.keyboard.ctrl_break()
    integra_b.expect(">", timeout=10.0, sample_interval_seconds=0.2)
    screen = _command(integra_b, "*STATUS MODE", "MODE")
    assert re.search(r"MODE\s+3", screen), screen
    screen = _command(integra_b, "PRINT ~HIMEM", "4000")  # MODE 3 screen at &4000
    assert "4000" in screen


def test_srwrite_and_srread_absolute(integra_b):
    _type(integra_b,
          "FOR I%=0 TO 255:I%?&3000=I%:NEXT",
          "*SRWRITE 3000+100 8000 4",
          "FOR I%=0 TO 255:I%?&3000=0:NEXT",
          "*SRREAD 3000+100 8000 4")
    screen = _command(integra_b, "PRINT \"R\";?&3001;\" \";?&30FF", "R1")
    assert "R1 255" in screen
    assert bytes(integra_b.memory.region("bank_4").peek[0x8000:0x8004]) == b"\x00\x01\x02\x03"


def test_srdata_pseudo_addressing_and_srwipe(integra_b):
    _type(integra_b, "*SRDATA 7")
    screen = _command(integra_b, "*ROMS", "  0 (")
    assert re.search(r"\b7 \(E\s*\) RAM", screen), screen

    _type(integra_b, "CLS",
          "FOR I%=0 TO 15:I%?&3000=&40+I%:NEXT",
          "*SRWRITE 3000+10 0",
          "*SRREAD 3100+10 0")
    screen = _command(integra_b, "PRINT \"P\";?&3100;\" \";?&310F", "P64")
    assert "P64 79" in screen
    # Pseudo address 0 is just past the 16-byte header of the first bank.
    assert integra_b.memory.region("bank_7").peek[0x8010] == 0x40

    _type(integra_b, "CLS", "*SRWIPE 7")
    screen = _command(integra_b, "*ROMS", "  0 (")
    assert not re.search(r"\b7 \(E\s*\) RAM", screen), screen


def test_srsave_and_srload_install_a_rom_image(beebium_server_filepath: Path | None,
                                               scratch_disc_filepath: Path):
    try:
        with _launch(beebium_server_filepath,
                     ["--floppy", f"0:{scratch_disc_filepath}"]) as bbc:
            bbc.expect(">", timeout=20.0, sample_interval_seconds=0.2)
            # Save the BASIC ROM from motherboard slot 3 and install it in bank 6.
            _run(bbc, "*SRSAVE BASICIM 8000+4000 3 Q")
            _run(bbc, "*SRLOAD BASICIM 8000 6 QI")
            screen = _command(bbc, "*ROMS", "  0 (")
            assert re.search(r"\b6 \(E\s*L\) BASIC", screen), screen
            assert re.search(r"\b3 \(\s*L\) BASIC", screen), screen
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_unplug_and_insert(beebium_server_filepath: Path | None,
                           scratch_disc_filepath: Path):
    try:
        with _launch(beebium_server_filepath,
                     ["--floppy", f"0:{scratch_disc_filepath}"]) as bbc:
            bbc.expect(">", timeout=20.0, sample_interval_seconds=0.2)
            _type(bbc, "*UNPLUG 1 I")
            screen = _command(bbc, "*ROMS", "  0 (")
            assert re.search(r"\b1 \(\s*US\s*\) DFS", screen), screen
            # With DFS unplugged nothing claims *DISC; it falls through to the
            # current filing system as *RUN DISC, which is not on the disc.
            _type(bbc, "CLS")
            screen = _command(bbc, "*DISC", "Bad command")
            assert "Bad command" in screen

            _type(bbc, "CLS", "*INSERT 1 I")
            screen = _command(bbc, "*ROMS", "  0 (")
            assert re.search(r"\b1 \(\s*S\s*\) DFS", screen), screen
            _run(bbc, "*DISC")
            assert "Bad command" not in bbc.video.screen_text().text
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_shadow_command_selects_how_modes_are_interpreted(integra_b):
    # *SHADOW (0): MODE 0-7 also select shadow modes.
    _type(integra_b, "*SHADOW")
    screen = _command(integra_b, "MODE 7:PRINT ~HIMEM", "8000")
    assert "8000" in screen
    # *SHADOW 1: only MODE 128-135 select shadow modes.
    _type(integra_b, "*SHADOW 1")
    screen = _command(integra_b, "MODE 7:PRINT ~HIMEM", "7C00")
    assert "7C00" in screen


def test_shadow_memory_is_not_exchanged_by_default(integra_b):
    _type(integra_b, "MODE 7:?&5000=&AA")
    screen = _command(integra_b, "MODE 135:PRINT \"V\";~?&5000", "V")
    assert "VAA" not in screen


def test_shx_exchanges_main_and_shadow_memory_on_mode_change(integra_b):
    _type(integra_b, "MODE 7:?&5000=&AA", "*SHX ON")
    screen = _command(integra_b, "MODE 135:PRINT \"V\";~?&5000", "VAA")
    assert "VAA" in screen


def test_osbyte_108_switches_the_cpu_between_shadow_and_screen(integra_b):
    """In a shadow mode the CPU normally sees shadow RAM at &7C00; OSBYTE 108
    with X=1 switches it to the screen memory until X=0 switches back."""
    # 'Q' (81) is printed at the top left of the MODE 7 screen, &7C00.
    probe = ("MODE 135:PRINT \"Q\";:A%=108:X%={x}:CALL &FFF4:"
             "V%=?&7C00:X%=0:CALL &FFF4:PRINT '\"V\";V%+1000")
    # The +1000 keeps the result ("V1nnn") distinct from the typed command.
    screen = _command(integra_b, probe.format(x=1), "V1")
    assert "V1081" in screen
    screen = _command(integra_b, probe.format(x=0), "V1")
    assert re.search(r"V1\d\d\d", screen) and "V1081" not in screen


def test_x_prefix_gives_commands_the_screen_memory(integra_b):
    """*X* runs a command with shadow memory switched out, so *SRWRITE copies
    the screen itself rather than shadow RAM."""
    _type(integra_b, "MODE 135", "PRINT \"MARKER\"", "*X*SRWRITE 7C00+400 8000 4")
    integra_b.expect(">", timeout=10.0, sample_interval_seconds=0.2)
    time.sleep(0.5)
    copy = bytes(integra_b.memory.region("bank_4").peek[0x8000:0x8400])
    assert b"MARKER" in copy


def test_reset_mode_full_system_reset(integra_b):
    """CTRL+@+BREAK enters IBOS reset mode; Y performs a full system reset,
    after which IBOS's No Language Environment is the language and the clock
    is halted until set (IBOS guide 1-5)."""
    kb = integra_b.keyboard
    kb.ctrl_down()
    kb.key_down("@")
    time.sleep(0.05)
    kb.break_down()
    time.sleep(0.05)
    kb.break_up()
    time.sleep(1.0)
    kb.key_up("@")
    kb.ctrl_up()
    screen = integra_b.expect("Go (Y/N)", timeout=10.0, sample_interval_seconds=0.2)
    assert "System Reset" in integra_b.video.screen_text().text

    _type(integra_b, "Y")
    integra_b.expect("INTEGRA-B", timeout=20.0, sample_interval_seconds=0.2)
    screen = _command(integra_b, "*STATUS LANG", "LANG")
    assert re.search(r"LANG\s+15", screen), screen
    assert _rtc_register(integra_b, 0x0B) & 0x80  # SET: clock halted


def test_reset_mode_without_reset_unplugs_other_roms(integra_b):
    """Any key but Y leaves the configuration alone but temporarily unplugs
    every ROM except IBOS, leaving the No Language Environment."""
    kb = integra_b.keyboard
    kb.ctrl_down()
    kb.key_down("@")
    time.sleep(0.05)
    kb.break_down()
    time.sleep(0.05)
    kb.break_up()
    time.sleep(1.0)
    kb.key_up("@")
    kb.ctrl_up()
    integra_b.expect("Go (Y/N)", timeout=10.0, sample_interval_seconds=0.2)
    _type(integra_b, "N")
    integra_b.expect("*", timeout=10.0, sample_interval_seconds=0.2)
    screen = _command(integra_b, "*ROMS", "  0 (")
    assert re.search(r"\b3 \(\s*U\s*L\) BASIC", screen), screen
    assert re.search(r"\b1 \(\s*US\s*\) DFS", screen), screen
