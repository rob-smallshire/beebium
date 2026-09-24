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

Everything here is a function of emulated time, never of host speed. The
machine runs unpaced and only between the steps of an `IntegraB` driver, each
bounded by an emulated-time budget, and the real-time clock runs on the
emulated clock (--integra-rtc clock=emulated) from a fixed start. A slow host
only makes the tests take longer.
"""

from __future__ import annotations

import datetime
import re
import shutil
from collections.abc import Callable
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError, ServerStartupError
from beebium.client.sideways import ProtectionKind, SlotStatusReport

VARIANT = "model-b-integra-b"
DISC_ARGS = ["--fdc", "acorn-1770",
             "--sideways", "slot=1:type=rom:image=acorn-dfs_2_26.rom"]
# The emulated clock starts here: Tuesday 15 September 2026, 10:20:30.
START = datetime.datetime(2026, 9, 15, 10, 20, 30)
EMULATED_CLOCK = ["--integra-rtc", f"clock=emulated:time={START:%Y-%m-%dT%H:%M:%S}"]


class IntegraB:
    """Drive a machine by emulated time.

    The machine is stopped between steps and advances only inside `run_until`,
    which runs unpaced until a condition holds or an emulated-time budget runs
    out. Keys pressed or released between steps take effect when it next runs.
    """

    def __init__(self, bbc: Beebium):
        self.bbc = bbc
        bbc.system.set_speed_multiplier(0.0)  # unpaced
        bbc.debugger.ensure_stopped()

    # --- Running -------------------------------------------------------------

    def screen(self) -> str:
        return self.bbc.video.screen_text().text

    def lines(self) -> list[str]:
        return [ln.rstrip() for ln in self.screen().splitlines() if ln.strip()]

    def run_until(self, condition: Callable[[], bool], emulated_seconds: float,
                  what: str, *, check_every: float = 0.05) -> None:
        """Run until `condition` holds, checking it every `check_every`
        emulated seconds; fail if `emulated_seconds` pass first."""
        if not self.bbc.run_until_or_timeout(condition, emulated_seconds,
                                             chunk_seconds=check_every):
            raise AssertionError(
                f"not {what} within {emulated_seconds} emulated seconds:\n{self.screen()}")

    def run_for(self, emulated_seconds: float) -> None:
        self.bbc.run_until_or_timeout(lambda: False, emulated_seconds,
                                      chunk_seconds=emulated_seconds)

    def typing_idle(self) -> bool:
        return self.bbc.keyboard.typing_status().idle

    def prompt_ready(self, prompt: str = ">", containing: str | None = None) -> bool:
        """The screen ends with an empty prompt line and all typing is done."""
        lines = self.lines()
        return (self.typing_idle() and bool(lines) and lines[-1] == prompt
                and (containing is None or containing in self.screen()))

    # --- Typing ----------------------------------------------------------------

    def type(self, *lines: str) -> None:
        """Type each line and RETURN, running until the keys have gone in."""
        for line in lines:
            self.bbc.keyboard.type(line + "\r")
            self.run_until(self.typing_idle, 30.0, f"typed {line!r}")

    def clear(self) -> None:
        """CLS at the BASIC prompt, leaving just an empty prompt."""
        self.bbc.keyboard.type("CLS\r")
        self.run_until(lambda: self.typing_idle() and self.lines() == [">"],
                       10.0, "cleared the screen")

    def command(self, command: str, emulated_seconds: float = 10.0) -> str:
        """Clear the screen, type a command and run until it has finished, i.e.
        a fresh empty prompt follows it. Returns the screen."""
        self.clear()
        self.bbc.keyboard.type(command + "\r")
        self.run_until(lambda: self.prompt_ready() and self.lines() != [">"],
                       emulated_seconds, f"finished {command!r}")
        return self.screen()

    # --- Resets ------------------------------------------------------------------

    def reset(self, *, ctrl: bool = False) -> str:
        """BREAK (or CTRL+BREAK), running until BASIC is ready again.

        The screen is cleared first, so the prompt from before the reset cannot
        be taken for the new one. CTRL is held for a stretch of emulated time
        after BREAK is released, while the MOS reads it."""
        self.clear()
        kb = self.bbc.keyboard
        if ctrl:
            kb.ctrl_down()
        kb.break_down()
        kb.break_up()
        self.run_for(0.2)
        if ctrl:
            kb.ctrl_up()
        self.run_until(lambda: self.prompt_ready(containing="BASIC"), 20.0,
                       "reset to BASIC")
        return self.screen()

    def reset_mode(self) -> str:
        """CTRL+@+BREAK: IBOS reset mode, run until it asks Go (Y/N)?"""
        kb = self.bbc.keyboard
        kb.ctrl_down()
        kb.key_down("@")
        kb.break_down()
        kb.break_up()
        self.run_for(0.5)
        kb.key_up("@")
        kb.ctrl_up()
        self.run_until(lambda: "Go (Y/N)" in self.screen(), 20.0, "in reset mode")
        return self.screen()

    # --- Hardware -------------------------------------------------------------------

    def rtc_register(self, register: int) -> int:
        """Read an RTC register through the board's &FE38/&FE3C ports."""
        self.bbc.memory.address.bus[0xFE38] = register
        return self.bbc.memory.address.peek[0xFE3C]


def _launch(beebium_server_filepath: Path | None, extra_args: list[str]):
    return Beebium.launch(
        server=beebium_server_filepath,
        variant=VARIANT,
        extra_args=DISC_ARGS + extra_args,
    )


def _booted(bbc: Beebium) -> IntegraB:
    machine = IntegraB(bbc)
    machine.run_until(lambda: machine.prompt_ready(containing="BASIC"), 20.0,
                      "booted to BASIC")
    return machine


def _group(status: SlotStatusReport, group_id: str):
    for g in status.protection_groups:
        if g.id == group_id:
            return g
    return None


def _clock(screen: str) -> datetime.datetime:
    """The date and time IBOS printed, e.g. "Tue,15 Sep 2026.10:20:31"."""
    match = re.search(r"\w{3},(\d{1,2}) (\w{3}) (\d{4})\.(\d\d):(\d\d):(\d\d)", screen)
    assert match, screen
    day, month, year, hh, mm, ss = match.groups()
    return datetime.datetime.strptime(f"{day} {month} {year} {hh}:{mm}:{ss}",
                                      "%d %b %Y %H:%M:%S")


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
def launch(beebium_server_filepath: Path | None):
    """Launch an Integra-B (emulated clock, plus any extra arguments) booted
    to BASIC."""
    stack = []

    def _start(*extra_args: str, clock: list[str] = EMULATED_CLOCK) -> IntegraB:
        try:
            context = _launch(beebium_server_filepath, clock + list(extra_args))
            bbc = context.__enter__()
        except ServerNotFoundError as e:
            pytest.skip(str(e))
        stack.append(context)
        return _booted(bbc)

    yield _start
    for context in reversed(stack):
        context.__exit__(None, None, None)


@pytest.fixture
def integra_b(launch) -> IntegraB:
    return launch()


# ---------------------------------------------------------------------------
# The board
# ---------------------------------------------------------------------------


def test_boots_as_a_set_up_board(integra_b):
    """The battery-backed state is that of a board that has been set up, so
    IBOS starts BASIC (configured LANG 3) rather than stopping at Language?."""
    screen = integra_b.screen()
    assert "INTEGRA-B 128K" in screen
    assert "DFS" in screen
    assert "BASIC" in screen


def test_roms_lists_the_board_ram_banks(integra_b):
    screen = integra_b.command("*ROMS")
    assert re.search(r"15 \(\s*SL\) IBOS", screen), screen
    for bank in (4, 5, 6, 7):
        # 'E': write-enabled RAM, detected by IBOS writing to the bank.
        assert re.search(rf"\b{bank} \(E", screen), screen


def test_ibos_sees_a_write_protected_ram_chip(integra_b):
    integra_b.bbc.sideways.set_protection("slots-4-5", ProtectionKind.WRITE_PROTECT, True)
    screen = integra_b.command("*ROMS")
    for bank in (4, 5):
        assert re.search(rf"\b{bank} \(P", screen), screen   # write-protected
    for bank in (6, 7):
        assert re.search(rf"\b{bank} \(E", screen), screen   # other chip unaffected


def test_protection_groups_are_per_ram_chip(integra_b):
    sideways = integra_b.bbc.sideways
    status = sideways.get_slot_status()
    assert [g.id for g in status.protection_groups] == ["slots-4-5", "slots-6-7"]
    g = _group(status, "slots-4-5")
    assert list(g.slots) == [4, 5]
    assert g.supports_write_protect
    assert not g.supports_hide
    assert not g.write_protected

    sideways.set_protection("slots-6-7", ProtectionKind.WRITE_PROTECT, True)
    assert _group(sideways.get_slot_status(), "slots-6-7").write_protected


def test_write_protect_launch_flag(launch):
    status = launch("--write-protect", "slots-6-7").bbc.sideways.get_slot_status()
    assert _group(status, "slots-6-7").write_protected
    assert not _group(status, "slots-4-5").write_protected


def test_socket_pair_fitted_with_ram(launch):
    machine = launch("--sideways", "slot=8:type=ram", "--sideways", "slot=9:type=ram")
    assert _group(machine.bbc.sideways.get_slot_status(), "slots-8-9") is not None
    # IBOS must be told about RAM fitted in the sockets (IBOS guide 1-5):
    # 15, plus 16 for a chip in socket 9 (banks 8/9).
    machine.type("*FX162,127,31")
    banner = machine.reset(ctrl=True)
    assert "INTEGRA-B 160K" in banner, banner
    screen = machine.command("*ROMS")
    for bank in (8, 9):
        assert re.search(rf"\b{bank} \(E", screen), screen


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
# The real-time clock
# ---------------------------------------------------------------------------


def test_clock_starts_at_the_configured_emulated_time(integra_b):
    now = _clock(integra_b.command("*TIME"))
    # Booting and typing take a few emulated seconds.
    assert START <= now < START + datetime.timedelta(seconds=15), now


def test_clock_advances_with_emulated_time(integra_b):
    before = _clock(integra_b.command("*TIME"))
    integra_b.run_for(30.0)
    after = _clock(integra_b.command("*TIME"))
    # 30 emulated seconds, plus the time the second *TIME took to type.
    assert datetime.timedelta(seconds=30) <= after - before < datetime.timedelta(seconds=40)


def test_time_and_date_can_be_set_and_run_on(integra_b):
    integra_b.type("*TIME=23:59:50", "*DATE=31/12/26")
    set_at = _clock(integra_b.command("*TIME"))
    assert datetime.datetime(2026, 12, 31, 23, 59, 50) <= set_at, set_at

    # Across midnight into the new year, and through BREAK (the RTC keeps
    # time; only its interrupt state follows the reset line).
    integra_b.run_for(20.0)
    integra_b.reset()
    screen = integra_b.command("*TIME")
    after = _clock(screen)
    assert after.date() == datetime.date(2027, 1, 1), screen
    assert "Fri,01 Jan 2027" in screen
    assert after - set_at >= datetime.timedelta(seconds=20)


def test_date(integra_b):
    screen = integra_b.command("*DATE")
    assert "Tue,15 Sep 2026." in screen


def test_calendar(integra_b):
    screen = integra_b.command("*CALENDAR")
    assert "September 2026" in screen
    # September 2026 starts on a Tuesday.
    assert re.search(r"Tue\s+1\s+8\s+15\s+22\s+29", screen), screen


def test_osword_14_reads_the_clock(integra_b):
    integra_b.type("DIM B% 40")
    # Function 0: the time and date as a string.
    screen = integra_b.command(
        "?B%=0:A%=14:X%=B% MOD 256:Y%=B% DIV 256:CALL &FFF1:PRINT $B%")
    assert "Tue,15 Sep 2026.10:2" in screen
    # Function 1: BCD year, month, date, day of week, hours, minutes, seconds.
    screen = integra_b.command(
        "?B%=1:CALL &FFF1:PRINT \"BCD\";:FOR I%=0 TO 5:PRINT \" \";~B%?I%;:NEXT:PRINT")
    assert "BCD 26 9 15 3 10 2" in screen, screen


def test_alarm_flashes_the_lock_leds_until_acknowledged(integra_b):
    """An alarm-due flashes CAPS LOCK and SHIFT LOCK alternately (driven by
    the RTC alarm and periodic interrupts) until CTRL+SHIFT is held."""
    screen = integra_b.command("*ALARM=10:21:30")
    screen = integra_b.command("*ALARM ?")
    assert "10:21:30 / ON" in screen

    latch = integra_b.bbc.addressable_latch
    assert not latch.state.shift_lock_led
    integra_b.run_until(lambda: latch.state.shift_lock_led, 90.0,
                        "alarm flashing SHIFT LOCK", check_every=0.25)

    kb = integra_b.bbc.keyboard
    kb.ctrl_down()
    kb.shift_down()
    integra_b.run_for(1.5)
    kb.shift_up()
    kb.ctrl_up()
    integra_b.run_for(1.0)
    assert not latch.state.shift_lock_led
    assert latch.state.caps_lock_led


def test_host_clock_is_the_default(launch):
    """Without --integra-rtc the calendar follows the host's local date."""
    machine = launch(clock=[])
    today = datetime.date.today()
    shown = _clock(machine.command("*TIME")).date()
    assert shown in (today, today + datetime.timedelta(days=1)), shown


def test_host_clock_can_be_shifted_to_a_given_time(launch):
    machine = launch(clock=["--integra-rtc", "time=2030-06-01T08:00"])
    shown = _clock(machine.command("*TIME"))
    # The host clock runs on from 08:00:00 while the machine boots.
    assert datetime.datetime(2030, 6, 1, 8, 0) <= shown < datetime.datetime(2030, 6, 1, 8, 10)
    assert "Sat,01 Jun 2030" in machine.screen()


# ---------------------------------------------------------------------------
# IBOS configuration and sideways RAM (IBOS guide sections 2, 3, 5)
# ---------------------------------------------------------------------------


def test_configuration_survives_ctrl_break(integra_b):
    integra_b.type("*CONFIGURE MODE 3")
    integra_b.reset(ctrl=True)
    screen = integra_b.command("*STATUS MODE")
    assert re.search(r"MODE\s+3", screen), screen
    screen = integra_b.command("PRINT ~HIMEM")  # MODE 3 screen memory starts at &4000
    assert re.search(r"^\s*4000$", screen, re.M), screen


def test_srwrite_and_srread_absolute(integra_b):
    integra_b.type("FOR I%=0 TO 255:I%?&3000=I%:NEXT",
                   "*SRWRITE 3000+100 8000 4",
                   "FOR I%=0 TO 255:I%?&3000=0:NEXT",
                   "*SRREAD 3000+100 8000 4")
    screen = integra_b.command("PRINT ?&3001;\" \";?&30FF")
    assert re.search(r"^\s*1 255$", screen, re.M), screen
    bank_4 = integra_b.bbc.memory.region("bank_4").peek[0x8000:0x8004]
    assert bytes(bank_4) == b"\x00\x01\x02\x03"


def test_srdata_pseudo_addressing_and_srwipe(integra_b):
    integra_b.type("*SRDATA 7")
    assert re.search(r"\b7 \(E\s*\) RAM", integra_b.command("*ROMS"))

    integra_b.type("FOR I%=0 TO 15:I%?&3000=&40+I%:NEXT",
                   "*SRWRITE 3000+10 0",
                   "*SRREAD 3100+10 0")
    screen = integra_b.command("PRINT ?&3100;\" \";?&310F")
    assert re.search(r"^\s*64 79$", screen, re.M), screen
    # Pseudo address 0 is just past the 16-byte header of the first bank.
    assert integra_b.bbc.memory.region("bank_7").peek[0x8010] == 0x40

    integra_b.type("*SRWIPE 7")
    assert not re.search(r"\b7 \(E\s*\) RAM", integra_b.command("*ROMS"))


def test_srsave_and_srload_install_a_rom_image(launch, scratch_disc_filepath: Path):
    machine = launch("--floppy", f"0:{scratch_disc_filepath}")
    # Save the BASIC ROM from motherboard slot 3 and install it in bank 6.
    machine.command("*SRSAVE BASICIM 8000+4000 3 Q", emulated_seconds=30.0)
    machine.command("*SRLOAD BASICIM 8000 6 QI", emulated_seconds=30.0)
    screen = machine.command("*ROMS")
    assert re.search(r"\b6 \(E\s*L\) BASIC", screen), screen
    assert re.search(r"\b3 \(\s*L\) BASIC", screen), screen


def test_unplug_and_insert(launch, scratch_disc_filepath: Path):
    machine = launch("--floppy", f"0:{scratch_disc_filepath}")
    machine.type("*UNPLUG 1 I")
    assert re.search(r"\b1 \(\s*US\s*\) DFS", machine.command("*ROMS"))
    # With DFS unplugged nothing claims *DISC; it falls through to the current
    # filing system as *RUN DISC, which is not on the disc.
    assert "Bad command" in machine.command("*DISC", emulated_seconds=30.0)

    machine.type("*INSERT 1 I")
    assert re.search(r"\b1 \(\s*S\s*\) DFS", machine.command("*ROMS"))
    assert "Bad command" not in machine.command("*DISC", emulated_seconds=30.0)


# ---------------------------------------------------------------------------
# Shadow RAM (IBOS guide section 2-3)
# ---------------------------------------------------------------------------


def test_shadow_mode_frees_main_memory_and_keeps_the_screen(integra_b):
    """In a shadow mode the CPU sees shadow RAM at &3000-&7FFF, so BASIC gets
    HIMEM=&8000, while the display still shows main (screen) memory."""
    screen = integra_b.command("MODE 135:PRINT ~HIMEM")
    assert re.search(r"^\s*8000$", screen, re.M), screen


def test_shadow_command_selects_how_modes_are_interpreted(integra_b):
    # *SHADOW (0): MODE 0-7 also select shadow modes.
    integra_b.type("*SHADOW")
    assert re.search(r"^\s*8000$", integra_b.command("MODE 7:PRINT ~HIMEM"), re.M)
    # *SHADOW 1: only MODE 128-135 select shadow modes.
    integra_b.type("*SHADOW 1")
    assert re.search(r"^\s*7C00$", integra_b.command("MODE 7:PRINT ~HIMEM"), re.M)


def test_shadow_memory_is_not_exchanged_by_default(integra_b):
    integra_b.type("MODE 7:?&5000=&AA")
    screen = integra_b.command("MODE 135:PRINT ~?&5000")
    assert not re.search(r"^\s*AA$", screen, re.M), screen


def test_shx_exchanges_main_and_shadow_memory_on_mode_change(integra_b):
    integra_b.type("MODE 7:?&5000=&AA", "*SHX ON")
    screen = integra_b.command("MODE 135:PRINT ~?&5000")
    assert re.search(r"^\s*AA$", screen, re.M), screen


def test_osbyte_108_switches_the_cpu_between_shadow_and_screen(integra_b):
    """In a shadow mode the CPU normally sees shadow RAM at &7C00; OSBYTE 108
    with X=1 switches it to the screen memory until X=0 switches back."""
    # 'Q' (81) is printed at the top left of the MODE 7 screen, &7C00.
    probe = ("MODE 135:PRINT \"Q\";:A%=108:X%={x}:CALL &FFF4:"
             "V%=?&7C00:X%=0:CALL &FFF4:PRINT 'V%")
    assert re.search(r"^\s*81$", integra_b.command(probe.format(x=1)), re.M)
    assert not re.search(r"^\s*81$", integra_b.command(probe.format(x=0)), re.M)


def test_x_prefix_gives_commands_the_screen_memory(integra_b):
    """*X* runs a command with shadow memory switched out, so *SRWRITE copies
    the screen itself rather than shadow RAM."""
    integra_b.command("MODE 135:PRINT \"MARKER\"")
    integra_b.type("*X*SRWRITE 7C00+400 8000 4")
    bank_4 = integra_b.bbc.memory.region("bank_4")
    integra_b.run_until(lambda: b"MARKER" in bytes(bank_4.peek[0x8000:0x8400]), 10.0,
                        "copied the screen into bank 4")


# ---------------------------------------------------------------------------
# IBOS reset mode (IBOS guide section 1-5)
# ---------------------------------------------------------------------------


def test_reset_mode_full_system_reset(integra_b):
    """CTRL+@+BREAK enters IBOS reset mode; Y performs a full system reset,
    after which IBOS's No Language Environment is the language and the clock
    is halted until set."""
    assert "System Reset" in integra_b.reset_mode()
    integra_b.type("Y")
    # IBOS's No Language Environment has a '*' prompt.
    integra_b.run_until(lambda: integra_b.prompt_ready("*", containing="INTEGRA-B"), 30.0,
                        "reset to the No Language Environment")
    integra_b.type("*STATUS LANG")
    integra_b.run_until(lambda: integra_b.prompt_ready("*", containing="LANG "), 10.0,
                        "shown *STATUS LANG")
    assert re.search(r"LANG\s+15", integra_b.screen()), integra_b.screen()
    assert integra_b.rtc_register(0x0B) & 0x80  # SET: clock halted


def test_reset_mode_without_reset_unplugs_other_roms(integra_b):
    """Any key but Y leaves the configuration alone but temporarily unplugs
    every ROM except IBOS, leaving the No Language Environment."""
    integra_b.reset_mode()
    integra_b.type("N")
    integra_b.run_until(lambda: integra_b.prompt_ready("*"), 30.0,
                        "entered the No Language Environment")
    integra_b.type("*ROMS")
    integra_b.run_until(lambda: integra_b.prompt_ready("*", containing="  0 ("), 10.0,
                        "listed the ROMs")
    screen = integra_b.screen()
    assert re.search(r"\b3 \(\s*U\s*L\) BASIC", screen), screen
    assert re.search(r"\b1 \(\s*US\s*\) DFS", screen), screen
