#!/usr/bin/env python3
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

"""Measure a headless server's CPU cost and speed ceiling in fixed guest states.

For each scenario this starts its own server, drives the guest into the
scenario's state, and at each requested speed measures, over a fixed window:

- every server thread's CPU use, as a percentage of one core, from
  /proc/<pid>/task (Linux only);
- the server's own pacing statistics: the speed achieved and the estimated
  ceiling. At a speed the host cannot reach (--speed 10 on a Pi), the
  achieved speed *is* the ceiling;
- optionally `perf stat` over the same window, and a separate `perf record`
  window whose report (top symbols, and the callers of chosen symbols) is
  written beside it.

Run it from the repository root with the Python client's environment:

    uv run --project clients/beebium-python-client \\
        python tools/perf/profile_server.py \\
        --server build-prof/src/server/beebium-model-b \\
        --scenario idle --scenario mode2 --speed 1 --speed 10 \\
        --perf-stat --perf-record --output-dirpath perf-out

It kills only the servers it started. See docs/performance.md for the method
and the baseline numbers.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

from beebium.client import Beebium

REPO_DIRPATH = Path(__file__).resolve().parents[2]
BLANK_DFS_FILEPATH = REPO_DIRPATH / "discs" / "blank" / "Acorn DFS 80T SS 200K.ssd"
DFS_ROM_FILENAME = "acorn-dfs_2_26.rom"

# A MODE 2 drawing loop: every frame of a bitmap mode changes, so video
# output and the render thread work as hard as they ever do.
MODE2_PROGRAM = [
    "10 MODE 2",
    "20 REPEAT",
    "30 GCOL 0,RND(7)",
    "40 DRAW RND(1280),RND(1024)",
    "50 UNTIL FALSE",
]

# Writes a 4 KiB file and reads it back, for ever: the disc controller,
# its NMIs and the DFS run continuously.
DISC_PROGRAM = [
    "10 REPEAT",
    "20 F=OPENOUT\"DATA\"",
    "30 FOR I=1 TO 4096:BPUT#F,I AND 255:NEXT",
    "40 CLOSE#F",
    "50 F=OPENIN\"DATA\"",
    "60 REPEAT:A=BGET#F:UNTIL EOF#F",
    "70 CLOSE#F",
    "80 UNTIL FALSE",
]

# Runs on the second processor: arithmetic there, with a line printed
# across the Tube every 100 iterations.
TUBE_PROGRAM = [
    "10 N=0",
    "20 REPEAT",
    "30 N=N+1:X=SQR(N)*1.5",
    "40 IF N MOD 100=0 PRINT N",
    "50 UNTIL FALSE",
]


@dataclasses.dataclass(frozen=True)
class Scenario:
    name: str
    description: str
    server_args: tuple[str, ...] = ()
    needs_disc: bool = False
    program: tuple[str, ...] = ()


SCENARIOS = {
    s.name: s
    for s in (
        Scenario("idle", "idle MODE 7 BASIC prompt"),
        Scenario("mode2", "MODE 2 BASIC drawing loop", program=tuple(MODE2_PROGRAM)),
        Scenario(
            "disc",
            "BASIC writing and reading a file on a 1770 DFS disc",
            server_args=(
                "--fdc", "acorn-1770",
                "--sideways", f"slot=14:type=rom:image={DFS_ROM_FILENAME}",
            ),
            needs_disc=True,
            program=tuple(DISC_PROGRAM),
        ),
        Scenario(
            "tube",
            "65C02 second processor running a BASIC loop",
            server_args=(
                "--fdc", "acorn-1770",
                "--sideways", f"slot=14:type=rom:image={DFS_ROM_FILENAME}",
                "--tube-65c02",
            ),
            program=tuple(TUBE_PROGRAM),
        ),
    )
}


def clock_ticks_per_second() -> int:
    return os.sysconf("SC_CLK_TCK")


def thread_cpu_ticks(pid: int) -> dict[int, tuple[str, int]]:
    """Each thread's name and its user+system CPU time in clock ticks."""
    result = {}
    task_dirpath = Path(f"/proc/{pid}/task")
    for entry in task_dirpath.iterdir():
        try:
            stat = (entry / "stat").read_text()
            name = (entry / "comm").read_text().strip()
        except (FileNotFoundError, ProcessLookupError):
            continue  # The thread ended between listing and reading.
        # The second field is "(comm)" and may contain spaces; the fields
        # after the closing parenthesis are fixed.
        fields = stat[stat.rindex(")") + 2:].split()
        utime, stime = int(fields[11]), int(fields[12])
        result[int(entry.name)] = (name, utime + stime)
    return result


def thread_cpu_percent(pid: int, before, after, seconds: float) -> list[tuple[str, int, float]]:
    """Per-thread CPU over a window, as a percentage of one core, busiest first."""
    ticks = clock_ticks_per_second()
    rows = []
    for tid, (name, end_ticks) in after.items():
        start_ticks = before.get(tid, (name, end_ticks))[1]
        percent = 100.0 * (end_ticks - start_ticks) / ticks / seconds
        label = "emulation (main)" if tid == pid else name
        rows.append((label, tid, percent))
    rows.sort(key=lambda row: row[2], reverse=True)
    return rows


class ServerUnderTest:
    """A server this script started, and only that one, stopped on exit."""

    def __init__(self, server_filepath: Path, port: int, user_dirpath: Path,
                 args: list[str], log_filepath: Path):
        env = dict(os.environ, BEEBIUM_USER_DIRPATH=str(user_dirpath))
        self._log = log_filepath.open("w")
        self.process = subprocess.Popen(
            [str(server_filepath), "start", "--port", str(port), *args],
            stdin=subprocess.DEVNULL, stdout=self._log, stderr=subprocess.STDOUT,
            env=env, start_new_session=True,
        )

    @property
    def pid(self) -> int:
        return self.process.pid

    def stop(self) -> None:
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self._log.close()


def connect(port: int, server: ServerUnderTest, timeout: float = 30.0) -> Beebium:
    deadline = time.monotonic() + timeout
    while True:
        if server.process.poll() is not None:
            raise RuntimeError(f"server exited with status {server.process.returncode}")
        try:
            return Beebium.connect(f"localhost:{port}", timeout=2.0)
        except Exception:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)


def enter_program(bbc: Beebium, lines: tuple[str, ...]) -> None:
    """Type the program and RUN it, waiting until the last key has gone in.

    Typing is paced in emulated time (12.5 characters an emulated second),
    so it runs flat out meanwhile and no measurement overlaps it. The text
    goes in untranslated: translation maps the teletext glyph "#" to the
    code that shows it, which is not the "#" BASIC needs for a channel.
    """
    bbc.system.set_speed_multiplier(0.0)
    bbc.keyboard.type("".join(f"{line}\r" for line in (*lines, "RUN")), translate=False)
    bbc.keyboard.wait_until_typing_complete()
    bbc.system.set_speed_multiplier(1.0)


def run_perf(perf_args: list[str], pid: int, seconds: float, output_filepath: Path) -> subprocess.Popen:
    """Start perf against the server's pid for a fixed window."""
    return subprocess.Popen(
        ["perf", *perf_args, "-p", str(pid), "--", "sleep", str(seconds)],
        stdout=subprocess.DEVNULL, stderr=output_filepath.open("w"),
    )


def perf_report(data_filepath: Path, output_dirpath: Path, stem: str,
                top: int, caller_symbols: list[str]) -> None:
    flat = subprocess.run(
        ["perf", "report", "-i", str(data_filepath), "--stdio", "--no-children",
         "--sort", "comm,symbol", "-F", "overhead,comm,sym", "--percent-limit", "0.5",
         "-g", "none"],
        capture_output=True, text=True,
    ).stdout
    (output_dirpath / f"{stem}.report.txt").write_text(flat)
    print(f"    top {top} symbols (comm, symbol):")
    for line in top_rows(flat, top):
        print(f"      {line}")
    for symbol in caller_symbols:
        callers = subprocess.run(
            ["perf", "report", "-i", str(data_filepath), "--stdio", "--no-children",
             "--symbols", symbol, "-g", "caller,0.5,callee,function,percent"],
            capture_output=True, text=True,
        ).stdout
        (output_dirpath / f"{stem}.callers-{symbol}.txt").write_text(callers)
        print(f"    callers of {symbol}:")
        for caller, percent in direct_callers(callers):
            print(f"      {percent:6.2f}%  {caller}")


def direct_callers(report: str) -> list[tuple[str, float]]:
    """The immediate callers in a caller-graph report, as (function, percent of all samples).

    The report has a block per thread that ran the symbol, headed by the
    symbol's overhead in that thread. perf prints each call chain's share at
    the first branch below the symbol, as "|--12.34%--function" or
    "--12.34%--function"; a symbol with a single caller in a thread has no
    branch, and its caller, on the line after "---symbol", takes the block's
    whole overhead.
    """
    callers = []
    for overhead, lines in _caller_blocks(report):
        branches = []
        for line in lines:
            column = line.find("--")
            if column < 0 or "%--" not in line[column + 2:]:
                continue
            percent_text, function = line[column + 2:].split("%--", 1)
            try:
                branches.append((column, function.strip(), float(percent_text)))
            except ValueError:
                continue
        if branches:
            # Deeper branches split a caller's share among its own callers;
            # only the shallowest are the symbol's immediate callers.
            first_column = min(column for column, _, _ in branches)
            callers += [(function, percent) for column, function, percent in branches
                        if column == first_column]
            continue
        for index, line in enumerate(lines):
            if line.strip().startswith("---") and index + 1 < len(lines):
                caller = lines[index + 1].strip().lstrip("|").strip()
                if caller:
                    callers.append((caller, overhead))
                break
    return callers


def _caller_blocks(report: str) -> list[tuple[float, list[str]]]:
    """Split a caller-graph report into (overhead, lines) blocks, one per thread."""
    blocks = []
    for line in report.splitlines():
        if line.startswith("#"):
            continue
        fields = line.split()
        if fields and fields[0].endswith("%") and line.startswith("    ") and \
                not line.strip().startswith("|"):
            try:
                blocks.append((float(fields[0].rstrip("%")), []))
                continue
            except ValueError:
                pass
        if blocks:
            blocks[-1][1].append(line)
    return blocks


def top_rows(report: str, count: int) -> list[str]:
    """The busiest rows of a flat perf report, as "percent comm symbol"."""
    rows = []
    for line in report.splitlines():
        fields = line.split()
        if not fields or line.startswith("#") or not fields[0].endswith("%"):
            continue
        while fields and fields[-1] == "-":
            fields.pop()  # perf's empty placeholder columns
        rows.append((float(fields[0].rstrip("%")), " ".join(fields)))
    rows.sort(key=lambda row: row[0], reverse=True)
    return [text for _, text in rows[:count]]


def measure(args, scenario: Scenario) -> list[dict]:
    output_dirpath: Path = args.output_dirpath
    user_dirpath = output_dirpath / "user-state"
    user_dirpath.mkdir(parents=True, exist_ok=True)
    server_args = list(scenario.server_args)
    if args.rom_dirpath:
        server_args += ["--rom-dir", str(args.rom_dirpath)]
    if scenario.needs_disc:
        disc_filepath = output_dirpath / f"{scenario.name}.ssd"
        shutil.copyfile(BLANK_DFS_FILEPATH, disc_filepath)
        server_args += ["--floppy", f"0:{disc_filepath}"]

    print(f"== {scenario.name}: {scenario.description}")
    server = ServerUnderTest(args.server_filepath, args.port, user_dirpath, server_args,
                             output_dirpath / f"{scenario.name}.server.log")
    results = []
    try:
        bbc = connect(args.port, server)
        bbc.system.set_speed_multiplier(1.0)
        bbc.basic.wait_for_prompt(timeout=30.0)
        if scenario.program:
            enter_program(bbc, scenario.program)
        for speed in args.speeds:
            bbc.system.set_speed_multiplier(speed)
            time.sleep(args.settle_seconds)
            stem = f"{scenario.name}-x{speed:g}"
            stat_process = None
            if args.perf_stat:
                stat_process = run_perf(["stat", "-e",
                                         "task-clock,cycles,instructions,branch-misses,"
                                         "cache-misses,context-switches"],
                                        server.pid, args.seconds,
                                        output_dirpath / f"{stem}.perf-stat.txt")
            before = thread_cpu_ticks(server.pid)
            start = time.monotonic()
            time.sleep(args.seconds)
            after = thread_cpu_ticks(server.pid)
            elapsed = time.monotonic() - start
            pacing = bbc.system.get_pacing_stats()
            if stat_process:
                stat_process.wait()
            threads = thread_cpu_percent(server.pid, before, after, elapsed)
            result = {
                "scenario": scenario.name,
                "speed": speed,
                "achieved_speed_multiplier": pacing.achieved_speed_multiplier,
                "estimated_max_speed_multiplier": pacing.estimated_max_speed_multiplier,
                "threads": [{"name": n, "tid": t, "cpu_percent": round(p, 1)}
                            for n, t, p in threads if p >= 0.5],
            }
            results.append(result)
            print(f"  speed {speed:g}: achieved {pacing.achieved_speed_multiplier:.3f}x, "
                  f"estimated ceiling {pacing.estimated_max_speed_multiplier:.2f}x")
            for name, tid, percent in threads:
                if percent >= 0.5:
                    print(f"    {name:<18} {percent:6.1f}%")
            if args.perf_record:
                data_filepath = output_dirpath / f"{stem}.perf.data"
                record = run_perf(["record", "-F", str(args.frequency), "-g",
                                   "-o", str(data_filepath)],
                                  server.pid, args.seconds,
                                  output_dirpath / f"{stem}.perf-record.log")
                record.wait()
                perf_report(data_filepath, output_dirpath, stem, args.top, args.callers_of)
        # Keep the screen as the measurements end, so a run that stopped at
        # an error (a disc fault, a mistyped line) is seen, not measured.
        screen_filepath = output_dirpath / f"{scenario.name}.screen.txt"
        screen_filepath.write_text(bbc.basic.read_screen_text())
    finally:
        server.stop()
    return results


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--server", dest="server_filepath", type=Path, required=True,
                        help="the server executable, e.g. build-prof/src/server/beebium-model-b")
    parser.add_argument("--scenario", dest="scenarios", action="append",
                        choices=sorted(SCENARIOS), help="repeatable; default: all")
    parser.add_argument("--speed", dest="speeds", action="append", type=float,
                        help="repeatable; default: 1 and 10")
    parser.add_argument("--seconds", type=float, default=15.0,
                        help="length of each measurement window (default 15)")
    parser.add_argument("--settle-seconds", type=float, default=3.0,
                        help="wait after a speed change before measuring (default 3)")
    parser.add_argument("--port", type=int, default=48993)
    parser.add_argument("--rom-dir", dest="rom_dirpath", type=Path)
    parser.add_argument("--output-dirpath", type=Path, default=Path("perf-out"))
    parser.add_argument("--perf-stat", action="store_true", help="run perf stat over each window")
    parser.add_argument("--perf-record", action="store_true",
                        help="after each window, a perf record window of the same length")
    parser.add_argument("--frequency", type=int, default=997, help="perf record sample rate")
    parser.add_argument("--top", type=int, default=15, help="symbols to print from each report")
    parser.add_argument("--callers-of", action="append", default=[],
                        help="symbol whose callers to report (repeatable), "
                             "e.g. __aarch64_ldadd8_acq_rel")
    args = parser.parse_args(argv)
    args.scenarios = args.scenarios or list(SCENARIOS)
    args.speeds = args.speeds or [1.0, 10.0]
    if not sys.platform.startswith("linux"):
        parser.error("per-thread CPU comes from /proc, so this runs on Linux only")
    args.output_dirpath.mkdir(parents=True, exist_ok=True)

    all_results = []
    for name in args.scenarios:
        all_results += measure(args, SCENARIOS[name])
    (args.output_dirpath / "results.json").write_text(json.dumps(all_results, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
