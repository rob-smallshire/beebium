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

"""Integration tests for the printer port with the file-printer extension (#99).

Each launches a real Model B with ``--file-printer`` and drives the port the
way software does: the MOS printer driver via *FX5,1 and VDU 2, and a BASIC
program bit-banging the User VIA. Every wait polls the guest or the server's
own accounting, never the wall clock alone.
"""

from __future__ import annotations

import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.ext.peripheral.file_printer import FilePrinter

TIMEOUT = 20.0


def _screen(bbc: Beebium) -> str:
    text = bbc.video.screen_text().text
    return "\n".join(text) if isinstance(text, list) else text


def _wait_for_screen(bbc: Beebium, needle: str, timeout: float = TIMEOUT) -> None:
    """Poll the screen until `needle` appears, rather than sleeping blind."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if needle in _screen(bbc):
            return
        time.sleep(0.05)
    pytest.fail(f"{needle!r} did not appear on screen. Screen was:\n{_screen(bbc)}")


def _wait_for_written(printer: FilePrinter, count: int, timeout: float = TIMEOUT) -> None:
    """Poll GetStatus until the file printer has written `count` bytes."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = printer.get_status()
        if status.bytes_written >= count:
            return
        time.sleep(0.05)
    pytest.fail(f"file printer wrote {printer.get_status().bytes_written} bytes, expected {count}")


def _type_line(bbc: Beebium, line: str) -> None:
    bbc.keyboard.type(line + "\r")
    bbc.keyboard.wait_until_typing_complete()


def _launch(launch_bbc, out_filepath: Path, *params: str) -> tuple[Beebium, FilePrinter]:
    spec = ":".join([f"path={out_filepath}", *params])
    bbc = launch_bbc(extra_args=["--file-printer", spec])
    bbc.wait_until_ready()
    return bbc, bbc.extensions[FilePrinter]


# The marker is assembled at run time so it is on the screen only once the
# statement after the print has executed, not as part of the echoed line.
MARKER_EXPR = '"DO"+"NE"'
MARKER = "DONE"


def _print_hello(bbc: Beebium) -> None:
    _type_line(bbc, "*FX5,1")
    _type_line(bbc, f'VDU 2:PRINT "HELLO":VDU 3:PRINT {MARKER_EXPR}')
    _wait_for_screen(bbc, MARKER)


def test_mos_printer_driver_spools_hello_with_cr(launch_bbc, tmp_path: Path) -> None:
    """*FX5,1 then VDU 2 / PRINT / VDU 3 yields HELLO CR: *FX6,10 suppresses the LF."""
    out_filepath = tmp_path / "printout.txt"
    bbc, printer = _launch(launch_bbc, out_filepath)
    assert out_filepath.exists(), "the file is created at attach"

    _print_hello(bbc)
    _wait_for_written(printer, 6)

    status = printer.get_status()
    assert status.path == str(out_filepath)
    assert status.bytes_received == 6
    assert status.online
    assert out_filepath.read_bytes() == b"HELLO\r"


def test_fx6_0_lets_the_lf_through(launch_bbc, tmp_path: Path) -> None:
    """BASIC's newline is LF then CR; *FX6,0 stops the MOS swallowing the LF."""
    out_filepath = tmp_path / "printout.txt"
    bbc, printer = _launch(launch_bbc, out_filepath)

    _type_line(bbc, "*FX6,0")
    _print_hello(bbc)
    _wait_for_written(printer, 7)

    assert out_filepath.read_bytes() == b"HELLO\n\r"


def test_line_ending_lf_translates_the_cr(launch_bbc, tmp_path: Path) -> None:
    out_filepath = tmp_path / "printout.txt"
    bbc, printer = _launch(launch_bbc, out_filepath, "line_ending=lf")

    _print_hello(bbc)
    _wait_for_written(printer, 6)

    assert out_filepath.read_bytes() == b"HELLO\n"


def test_bit_banged_strobe_sees_ack_on_ca1(launch_bbc, tmp_path: Path) -> None:
    """A program that sets DDRA, selects CA2 pulse mode, writes ORA and polls IFR
    sees the CA1 flag the file printer's /ACK raises.

    ORA is written with STA abs, as the MOS and real software do. BASIC's
    ``?&FE61=65`` stores through STA (zp),Y, whose dummy read of &FE61 is
    itself a strobe in pulse mode, taken with the data lines still at zero.
    """
    out_filepath = tmp_path / "printout.txt"
    bbc, printer = _launch(launch_bbc, out_filepath)

    _type_line(bbc, "10 DIM C 10:P%=C")
    _type_line(bbc, "20 [OPT 0:LDA &80:STA &FE61:RTS:]")
    _type_line(bbc, "30 ?&FE63=&FF:?&FE6C=&0A:?&80=65")
    _type_line(bbc, "40 CALL C")
    _type_line(bbc, "50 IF (?&FE6D AND 2)=0 THEN GOTO 50")
    _type_line(bbc, '60 PRINT "ACKED"')
    _type_line(bbc, "RUN")

    _wait_for_screen(bbc, "ACKED")
    _wait_for_written(printer, 1)
    assert out_filepath.read_bytes() == b"A"


def test_offline_printer_stalls_the_print_until_set_online(launch_bbc, tmp_path: Path) -> None:
    """With online=false the print stalls: the MOS waits for an /ACK that never
    comes. SetOnline(true) lets it finish and the file has every byte."""
    out_filepath = tmp_path / "printout.txt"
    bbc, printer = _launch(launch_bbc, out_filepath, "online=false")
    assert not printer.get_status().online

    _type_line(bbc, "*FX5,1")
    # More than the MOS printer buffer holds, so the PRINT itself blocks.
    _type_line(bbc, f'VDU 2:PRINT STRING$(200,"X"):VDU 3:PRINT {MARKER_EXPR}')

    # Stalled: a good stretch of emulated time passes with nothing accepted
    # and the statement after the print never runs.
    start_cycles = bbc.debugger.cycle_count
    deadline = time.monotonic() + TIMEOUT
    while bbc.debugger.cycle_count - start_cycles < 2_000_000:
        assert time.monotonic() < deadline, "the machine did not advance"
        time.sleep(0.05)
    assert printer.get_status().bytes_received == 0
    assert MARKER not in _screen(bbc)

    status = printer.set_online(True)
    assert status.online

    _wait_for_screen(bbc, MARKER)
    _wait_for_written(printer, 201)
    assert out_filepath.read_bytes() == b"X" * 200 + b"\r"
