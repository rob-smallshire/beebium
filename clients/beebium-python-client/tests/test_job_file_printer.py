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

"""Integration test for the printer port with the job-file-printer extension (#217).

A real Model B launched with ``--job-file-printer`` prints two jobs through
the MOS printer driver, separated by a wait in the guest longer than the
idle period, and the server writes two job files. Every wait polls the guest
or the server's own accounting, never the wall clock alone.
"""

from __future__ import annotations

import re
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.ext.peripheral.job_file_printer import JobFilePrinter

TIMEOUT = 30.0

JOB_FILENAME = re.compile(r"^job-\d{8}T\d{6}Z(-\d+)?\.prn$")


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


def _wait_for_jobs(printer: JobFilePrinter, count: int, timeout: float = TIMEOUT) -> None:
    """Poll GetStatus until the printer has completed `count` jobs."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if printer.get_status().jobs_completed >= count:
            return
        time.sleep(0.05)
    pytest.fail(f"job file printer completed {printer.get_status().jobs_completed} jobs, "
                f"expected {count}")


def _type_line(bbc: Beebium, line: str) -> None:
    bbc.keyboard.type(line + "\r")
    bbc.keyboard.wait_until_typing_complete()


def test_two_prints_separated_by_more_than_idle_are_two_job_files(
    launch_bbc, tmp_path: Path
) -> None:
    jobs_dirpath = tmp_path / "jobs"
    bbc = launch_bbc(extra_args=["--job-file-printer", f"directory={jobs_dirpath}:idle=1"])
    bbc.wait_until_ready()
    printer = bbc.extensions[JobFilePrinter]
    assert jobs_dirpath.is_dir(), "the directory is created at attach"
    assert list(jobs_dirpath.iterdir()) == [], "no job file before the first byte"

    _type_line(bbc, "*FX5,1")
    # Each wait is 2.5 seconds of the guest's own clock (TIME counts
    # centiseconds), longer than idle=1, so each PRINT is a job of its own.
    wait = "T%=TIME:REPEAT UNTIL TIME>T%+250"
    _type_line(bbc, f'VDU 2:PRINT "ONE":VDU 3:{wait}:VDU 2:PRINT "TWO":VDU 3:{wait}:'
                    f'PRINT "DO"+"NE"')
    _wait_for_screen(bbc, "DONE")
    _wait_for_jobs(printer, 2)

    status = printer.get_status()
    assert status.directory == str(jobs_dirpath)
    assert status.jobs_completed == 2
    assert status.current_job_file == ""
    assert status.bytes_in_job == 0
    assert status.total_bytes == 8

    job_filepaths = sorted(jobs_dirpath.iterdir())
    assert len(job_filepaths) == 2
    assert all(JOB_FILENAME.match(p.name) for p in job_filepaths), job_filepaths
    assert sorted(p.read_bytes() for p in job_filepaths) == [b"ONE\r", b"TWO\r"]
