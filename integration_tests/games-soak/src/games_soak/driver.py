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

"""Soak the emulator the way it is actually used: playing games, for a long
time, with the real macOS frontend attached.

Unlike the boot-and-idle soaks, this keeps one long-lived server and (optionally)
one attached macOS frontend window, then cycles discs through it:

    for each game (repeatedly):
        reset -> insert disc -> boot -> navigate to play/attract ->
        run in real time for a few minutes, watching for a freeze ->
        break out -> eject -> next game

Two freeze signatures are distinguished, matching the two reported failure
modes:

  * Server/core stall (Mode B): the emulated cycle counter stops advancing.
    gRPC usually stays responsive, so cycle_count -- not gRPC liveness -- is the
    signal. This is watched here directly.

  * Frontend-only stall (Mode A): the emulator keeps advancing (cycle_count
    climbs) but the attached frontend stops rendering. This harness cannot see
    the frontend's frame rate from outside, so it cannot auto-detect Mode A;
    instead it attaches a real frontend so a human watching sees it freeze, and
    on ANY captured stall it samples the frontend process too.

The frontend is attached deterministically via the beebium:// URL scheme
(macOS), pointed at this server's exact ephemeral port -- the server is not
required to be on mDNS.

Tube and non-Tube games need different machines, so a run covers one group:
pass --tube for the Tube games (Elite, Chuckie Egg), omit it for the rest.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeout
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from beebium.client import Beebium
from beebium.client.screen import dump_screen, screen_contains

from games_soak.games import GAMES, Game

REPO = Path(__file__).resolve().parents[4]
DEFAULT_ROMS = REPO / "roms"
DEFAULT_SERVER = REPO / "build" / "src" / "server" / "beebium-model-b"
REPORTS_DIRPATH = Path(__file__).resolve().parents[2] / "reports"

# Base machine, shared by every game in the table (see games.py).
MOS_ROM = "acorn-mos_1_20.rom"
BASIC_ROM = "bbc-basic_2.rom"
DFS_ROM = "acorn-dfs_2_26.rom"
DFS_SLOT = 14
FDC = "acorn-1770"

LSREGISTER = (
    "/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/"
    "LaunchServices.framework/Versions/A/Support/lsregister"
)


def _now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _log(message: str) -> None:
    stamp = datetime.now(timezone.utc).strftime("%H:%M:%S")
    print(f"[{stamp}] {message}", flush=True)


def _call_with_timeout(executor: ThreadPoolExecutor, fn, timeout: float):
    """Run a (possibly blocking) gRPC call with a hard timeout.

    Returns (ok, value_or_exception). A timeout means the gRPC layer itself is
    wedged, a distinct and louder failure than a frozen cycle counter.
    """
    future = executor.submit(fn)
    try:
        return True, future.result(timeout=timeout)
    except FutureTimeout:
        return False, "gRPC call timed out"
    except Exception as exc:  # noqa: BLE001 - diagnostic harness records anything
        return False, exc


def _wait_for(predicate, timeout: float, poll: float = 0.25) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(poll)
    return False


def _wait_for_text(bbc: Beebium, text: str, timeout: float, chunk: float) -> bool:
    """Fast-forward emulated time until `text` appears on the Mode 7 screen.

    Uses the client's run_until_or_timeout (the same mechanism the source game
    tests use), then confirms directly since its return value is advisory.
    """
    bbc.run_until_or_timeout(
        lambda: screen_contains(bbc, text),
        emulated_seconds=timeout,
        chunk_seconds=chunk,
    )
    return screen_contains(bbc, text)


# ----------------------------------------------------------------------------
# Frontend attach (macOS URL scheme)
# ----------------------------------------------------------------------------

def _port_of(target: str) -> str:
    return target.rpartition(":")[2]


def attach_frontend(app_filepath: Path, target: str) -> int | None:
    """Attach the macOS frontend to `target` via the beebium:// URL scheme.

    Registers the (possibly dev-built) app with LaunchServices so the scheme
    routes to it, then opens a connect URL for this server's exact port.
    Returns the frontend process id if it can be found, else None.
    """
    port = _port_of(target)
    url = f"beebium://connect?host=127.0.0.1&port={port}"
    # Register the bundle so LaunchServices routes beebium:// to this build.
    subprocess.run([LSREGISTER, "-f", str(app_filepath)],
                   capture_output=True, check=False)
    subprocess.run(["open", str(app_filepath)], capture_output=True, check=False)
    subprocess.run(["open", url], capture_output=True, check=False)
    _log(f"Asked the frontend to attach: {url}")
    # Give the window a moment to appear, then find the process.
    _wait_for(lambda: _frontend_pid() is not None, timeout=10.0, poll=0.5)
    return _frontend_pid()


def _frontend_pid() -> int | None:
    result = subprocess.run(["pgrep", "-x", "Beebium"],
                            capture_output=True, text=True, check=False)
    pids = [int(p) for p in result.stdout.split()]
    return pids[0] if pids else None


_FPS_PREDICATE = ('subsystem == "com.beebium.Beebium" AND '
                  'category == "framerate" AND processIdentifier == {pid}')
_FPS_RE = re.compile(r"received_fps=([0-9.]+)")


class FrameRateMonitor:
    """Tail the frontend's received-frame-rate log (VideoClient framerateLog).

    The frontend emits `received_fps=<n>` once a second; this reads them off the
    unified log for a specific process so the soak can detect a frontend-only
    freeze: cycle_count still advancing while received fps has been zero (or
    silent) for a while.
    """

    def __init__(self, pid: int) -> None:
        self._pid = pid
        self._proc: subprocess.Popen | None = None
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._last_fps: float | None = None
        self._seen_frames = False
        # Wall-clock of the last window that reported a non-zero rate. Frames
        # silent (no log line at all) also count as "not moving" via this clock.
        self._last_nonzero_wall = time.monotonic()

    def start(self) -> None:
        predicate = _FPS_PREDICATE.format(pid=self._pid)
        self._proc = subprocess.Popen(
            ["log", "stream", "--style", "ndjson", "--predicate", predicate],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self) -> None:
        assert self._proc is not None and self._proc.stdout is not None
        for line in self._proc.stdout:
            match = _FPS_RE.search(line)
            if not match:
                continue
            fps = float(match.group(1))
            with self._lock:
                self._last_fps = fps
                self._seen_frames = True
                if fps > 0.0:
                    self._last_nonzero_wall = time.monotonic()

    def note_frames_flowing(self) -> None:
        """Reset the silence clock, e.g. after navigation, so the play phase
        starts from a clean baseline rather than counting boot-time silence."""
        with self._lock:
            self._last_nonzero_wall = time.monotonic()

    def seen_frames(self) -> bool:
        with self._lock:
            return self._seen_frames

    def last_fps(self) -> float | None:
        with self._lock:
            return self._last_fps

    def silent_for(self) -> float:
        with self._lock:
            return time.monotonic() - self._last_nonzero_wall

    def stop(self) -> None:
        if self._proc is not None:
            self._proc.terminate()


# ----------------------------------------------------------------------------
# Diagnostics capture
# ----------------------------------------------------------------------------

def _server_pid(bbc: Beebium) -> int | None:
    server = getattr(bbc, "_server", None)
    proc = getattr(server, "_process", None) if server else None
    return proc.pid if proc is not None else None


def _run(cmd: list[str], timeout: float) -> str:
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout, check=False
        )
        return (result.stdout or "") + (result.stderr or "")
    except FileNotFoundError:
        return f"(tool not found: {cmd[0]})"
    except subprocess.TimeoutExpired:
        return f"(timed out after {timeout}s: {' '.join(cmd)})"


def _sample_process(label: str, pid: int) -> list[str]:
    lines = [f"=== {label} (pid {pid}) CPU (ps) ===",
             _run(["ps", "-o", "pid,%cpu,state,time,command", "-p", str(pid)], 10),
             "",
             f"=== {label} (pid {pid}) sample (5s) ===",
             # `sample` needs no debug entitlement and shows where a livelock
             # spins; a backtrace shows where a deadlock is blocked.
             _run(["/usr/bin/sample", str(pid), "5", "-mayDie"], 30),
             "",
             f"=== {label} (pid {pid}) lldb thread backtrace all ===",
             _run(["lldb", "-p", str(pid), "--batch",
                   "-o", "thread backtrace all", "-o", "detach", "-o", "quit"], 30),
             ""]
    return lines


def capture_diagnostics(bbc: Beebium, executor: ThreadPoolExecutor,
                        game: Game, reason: str,
                        frontend_pid: int | None,
                        fps_monitor: FrameRateMonitor | None = None) -> Path:
    REPORTS_DIRPATH.mkdir(exist_ok=True)
    report_filepath = REPORTS_DIRPATH / f"stall-{game.name.replace(' ', '_')}-{_now_iso()}.txt"
    server_pid = _server_pid(bbc)

    lines: list[str] = [
        "Games soak stall report",
        f"captured: {_now_iso()}",
        f"game: {game.name}",
        f"reason: {reason}",
        f"server pid: {server_pid}",
        f"frontend pid: {frontend_pid}",
        f"target: {bbc.target}",
    ]
    if fps_monitor is not None:
        lines.append(f"frontend last received_fps: {fps_monitor.last_fps()} "
                     f"(silent for {fps_monitor.silent_for():.0f}s)")
    lines.append("")

    # gRPC liveness while the emulation is (maybe) frozen: does the service
    # layer still answer? Sample cpu registers twice -- if the emulation thread
    # lives, PC moves; if wedged, PC is identical.
    ok, value = _call_with_timeout(executor, lambda: bbc.debugger.cycle_count, 5.0)
    lines.append(f"gRPC debugger.cycle_count probe: ok={ok} value={value}")
    ok2, regs1 = _call_with_timeout(executor, lambda: bbc.cpu.registers, 5.0)
    time.sleep(0.5)
    ok3, regs2 = _call_with_timeout(executor, lambda: bbc.cpu.registers, 5.0)
    lines.append(f"gRPC cpu.registers probe #1: ok={ok2} value={regs1}")
    lines.append(f"gRPC cpu.registers probe #2: ok={ok3} value={regs2}")
    ok4, pacing = _call_with_timeout(executor, lambda: bbc.system.get_pacing_stats(), 5.0)
    lines.append(f"system.get_pacing_stats probe: ok={ok4} value={pacing}")
    lines.append("")

    if server_pid is not None:
        lines += _sample_process("server", server_pid)
    if frontend_pid is not None:
        # Even for a server stall, sample the frontend: it distinguishes a
        # frontend that is also blocked from one that is merely showing a
        # stalled server's last frame.
        lines += _sample_process("frontend", frontend_pid)
        lines.append("=== frontend received_fps history (last 90s) ===")
        lines.append(_run(
            ["log", "show", "--last", "90s", "--style", "compact",
             "--predicate", _FPS_PREDICATE.format(pid=frontend_pid)], 20))
        lines.append("")

    report_filepath.write_text("\n".join(lines))
    _log(f"Diagnostics written to {report_filepath}")
    return report_filepath


# ----------------------------------------------------------------------------
# Per-game sequencing
# ----------------------------------------------------------------------------

def boot_game(bbc: Beebium, game: Game, disc_filepath: Path) -> None:
    """Mount the disc, auto-boot it, and navigate to a running/attract state."""
    # Tube games: let the cold-boot banner settle before auto-booting the disc,
    # so the second processor is up when DFS runs !BOOT.
    if game.boot_banner:
        if not _wait_for_text(bbc, game.boot_banner, timeout=30.0, chunk=1.0):
            raise RuntimeError(
                f"{game.name}: boot banner {game.boot_banner!r} never "
                f"appeared:\n{dump_screen(bbc)}")
    # Auto-boot exactly as a user does with Shift-Break; *OPT-agnostic, so it
    # works whether the disc's !BOOT is *EXEC, *RUN or *LOAD.
    bbc.boot_disc(disc_filepath)

    if game.landmark:
        if not _wait_for_text(bbc, game.landmark,
                              timeout=game.landmark_timeout_seconds,
                              chunk=game.landmark_chunk_seconds):
            raise RuntimeError(
                f"{game.name}: landmark {game.landmark!r} never appeared:\n"
                f"{dump_screen(bbc)}")

    for wait_text, key in game.nav:
        if not _wait_for_text(bbc, wait_text,
                              timeout=game.landmark_timeout_seconds,
                              chunk=game.landmark_chunk_seconds):
            raise RuntimeError(
                f"{game.name}: navigation step {wait_text!r} never appeared:\n"
                f"{dump_screen(bbc)}")
        bbc.keyboard.type(key)

    # run_until_or_timeout leaves the CPU stopped; resume real-time running so
    # the frontend renders and real-time-dependent bugs can manifest.
    bbc.debugger.ensure_running()

    # Drive an attract/demo mode with timed real-time keypresses (e.g. Galaforce
    # advances instruction pages on a timer, not at a distinct landmark).
    for key in game.attract_keys:
        bbc.keyboard.type(key)
        time.sleep(game.attract_interval_seconds)


def teardown_game(bbc: Beebium, game: Game) -> None:
    """Break out of the game and eject its disc, ready for the next."""
    bbc.keyboard.ctrl_break()
    drive = bbc.disc.drive(0)
    if not drive.is_empty:
        drive.eject()
        drive.wait_for_eject(timeout=15.0)


@dataclass
class StallReport:
    game: str
    report_filepath: Path
    reason: str


def watch_for_stall(bbc: Beebium, game: Game, args: argparse.Namespace,
                    executor: ThreadPoolExecutor,
                    frontend_pid: int | None,
                    fps_monitor: FrameRateMonitor | None = None) -> StallReport | None:
    """Run `game` in real time for run_minutes, watching cycle_count (and, if a
    frontend is attached, its received frame rate).

    Returns a StallReport if a freeze is caught, else None (the game ran clean).
    """
    if fps_monitor is not None:
        fps_monitor.note_frames_flowing()
    start_wall = time.monotonic()
    start_cycles = bbc.debugger.cycle_count
    last_cycles = start_cycles
    last_progress_wall = start_wall
    last_report_wall = start_wall
    deadline = start_wall + game.run_minutes * 60.0
    last_read_ok = True

    while True:
        time.sleep(args.poll_seconds)
        ok, value = _call_with_timeout(
            executor, lambda: bbc.debugger.cycle_count, args.rpc_timeout)
        now = time.monotonic()
        last_read_ok = ok

        if ok:
            cycles = value
            if cycles > last_cycles:
                last_cycles = cycles
                last_progress_wall = now
        else:
            # A dropped read (e.g. laptop sleep) is not itself a hang: treat it
            # as "no progress" and let the stall timer decide.
            cycles = last_cycles
            _log(f"cycle_count read failed (transient?): {value}")

        stalled_for = now - last_progress_wall
        fps_silent_for = fps_monitor.silent_for() if fps_monitor else None
        if now - last_report_wall >= args.report_seconds:
            rate = (cycles - start_cycles) / max(now - start_wall, 1e-9)
            fps_note = (f" fps={fps_monitor.last_fps()}" if fps_monitor else "")
            _log(f"[{game.name}] wall={now - start_wall:6.0f}s "
                 f"cycles={cycles:>14d} rate={rate/1e6:5.2f}MHz "
                 f"stalled={stalled_for:4.0f}s{fps_note}")
            last_report_wall = now

        # Mode B: the core itself stopped advancing.
        if stalled_for >= args.stall_seconds:
            signature = ("cycle_count frozen (gRPC responsive)" if last_read_ok
                         else "cycle_count unobservable (gRPC not answering)")
            reason = f"Mode B: {signature} at {cycles} for {stalled_for:.0f}s"
            _log(f"STALL in {game.name}: {reason}")
            report = capture_diagnostics(bbc, executor, game, reason,
                                         frontend_pid, fps_monitor)
            return StallReport(game.name, report, reason)

        # Mode A: the core keeps advancing but the frontend stopped rendering.
        # Only meaningful once frames have been seen and the core is NOT itself
        # stalled (else it is just Mode B seen from the frontend).
        if (fps_monitor is not None and fps_monitor.seen_frames()
                and stalled_for < args.stall_seconds
                and fps_silent_for is not None
                and fps_silent_for >= args.stall_seconds):
            reason = (f"Mode A: frontend received_fps 0 for {fps_silent_for:.0f}s "
                      f"while cycle_count advancing (at {cycles})")
            _log(f"STALL in {game.name}: {reason}")
            report = capture_diagnostics(bbc, executor, game, reason,
                                         frontend_pid, fps_monitor)
            return StallReport(game.name, report, reason)

        if now >= deadline:
            return None


# ----------------------------------------------------------------------------
# Soak loop
# ----------------------------------------------------------------------------

def run_soak(args: argparse.Namespace) -> StallReport | None:
    games = [g for g in GAMES if bool(g.tube_args) == args.tube]
    if not games:
        _log(f"No games match --tube={args.tube}; nothing to do.")
        return None
    _log(f"Games this run: {', '.join(g.name for g in games)}")

    extra_args = ["--fdc", FDC, "--sideways", f"{DFS_SLOT}:rom:{args.roms / DFS_ROM}"]
    if args.tube:
        extra_args = ["--tube-65c02", *extra_args]
    if args.advertise:
        extra_args.append("--advertise")

    frontend_pid: int | None = None
    fps_monitor: FrameRateMonitor | None = None

    with ThreadPoolExecutor(max_workers=1) as executor, Beebium.launch(
        mos_filepath=args.roms / MOS_ROM,
        basic_filepath=args.roms / BASIC_ROM,
        server_filepath=args.server,
        extra_args=extra_args,
        startup_timeout=20.0,
    ) as bbc:
        bbc.disc.set_spin_up_delay(False)
        if args.speed != 1.0:
            bbc.system.set_speed_multiplier(args.speed)
            _log(f"Speed multiplier set to {args.speed} (0.0 = unlimited)")

        if args.macos_app:
            frontend_pid = attach_frontend(args.macos_app, bbc.target)
            _log(f"Frontend pid: {frontend_pid}")
            if frontend_pid is not None:
                fps_monitor = FrameRateMonitor(frontend_pid)
                fps_monitor.start()
                _log("Watching frontend received_fps for Mode A freezes.")
        else:
            _log("No --macos-app given; running headless (Mode A freezes will "
                 "not be observed).")

        start_wall = time.monotonic()
        run_forever = args.max_minutes <= 0
        deadline = start_wall + args.max_minutes * 60.0
        if run_forever:
            _log("Cycling games indefinitely; Ctrl-C or kill to stop.")

        try:
            loop = 0
            while True:
                loop += 1
                for game in games:
                    disc_filepath = REPO / game.disc
                    if not disc_filepath.exists():
                        _log(f"SKIP {game.name}: disc not found at {disc_filepath}")
                        continue
                    _log(f"--- loop {loop}: {game.name} ---")
                    try:
                        boot_game(bbc, game, disc_filepath)
                    except Exception as exc:  # noqa: BLE001
                        # A boot/nav failure might itself be a freeze; capture it.
                        _log(f"{game.name}: boot/navigation failed: {exc}")
                        report = capture_diagnostics(
                            bbc, executor, game, f"boot/nav failure: {exc}",
                            frontend_pid, fps_monitor)
                        _hold(bbc, args.hold_minutes, frontend_pid)
                        return StallReport(game.name, report,
                                           f"boot/nav failure: {exc}")

                    stall = watch_for_stall(bbc, game, args, executor,
                                            frontend_pid, fps_monitor)
                    if stall is not None:
                        _hold(bbc, args.hold_minutes, frontend_pid)
                        return stall

                    teardown_game(bbc, game)

                    if not run_forever and time.monotonic() >= deadline:
                        _log(f"No stall within {args.max_minutes} min; giving up.")
                        return None
        finally:
            if fps_monitor is not None:
                fps_monitor.stop()


def _hold(bbc: Beebium, hold_minutes: float, frontend_pid: int | None) -> None:
    forever = hold_minutes <= 0
    horizon = "indefinitely" if forever else f"for {hold_minutes:.0f} min"
    _log(f"Holding the frozen server {horizon} for inspection at {bbc.target} "
         f"(server pid {_server_pid(bbc)}, frontend pid {frontend_pid}). "
         f"Ctrl-C or kill to stop.")
    try:
        if forever:
            while True:
                time.sleep(3600.0)
        else:
            time.sleep(hold_minutes * 60.0)
    except KeyboardInterrupt:
        _log("Interrupted; shutting down.")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tube", action=argparse.BooleanOptionalAction, default=False,
                        help="Run the Tube games (Elite, Chuckie Egg) instead of "
                             "the non-Tube games.")
    parser.add_argument("--macos-app", type=Path, default=None,
                        help="Path to Beebium.app to attach as the frontend via "
                             "the beebium:// URL scheme. Omit to run headless.")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="Speed multiplier (1.0 real-time, 0.0 unlimited).")
    parser.add_argument("--max-minutes", type=float, default=0.0,
                        help="Give up after this many wall-clock minutes "
                             "(0 or less = run indefinitely).")
    parser.add_argument("--stall-seconds", type=float, default=20.0,
                        help="No cycle_count progress for this long = a stall.")
    parser.add_argument("--poll-seconds", type=float, default=2.0,
                        help="Liveness poll interval.")
    parser.add_argument("--report-seconds", type=float, default=30.0,
                        help="How often to print a progress line.")
    parser.add_argument("--rpc-timeout", type=float, default=10.0,
                        help="Per-call gRPC timeout before declaring it wedged.")
    parser.add_argument("--hold-minutes", type=float, default=30.0,
                        help="Keep the frozen server alive this long after a "
                             "stall (0 or less = hold indefinitely).")
    parser.add_argument("--advertise", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Advertise over mDNS as well (frontend can also be "
                             "attached by hand from the discovery list).")
    parser.add_argument("--roms", type=Path, default=DEFAULT_ROMS)
    parser.add_argument("--server", type=Path, default=DEFAULT_SERVER)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.server.exists():
        print(f"Server not found: {args.server}", file=sys.stderr)
        return 2
    if args.macos_app is not None and not args.macos_app.exists():
        print(f"macOS app not found: {args.macos_app}", file=sys.stderr)
        return 2
    result = run_soak(args)
    if result is not None:
        _log(f"STALL reproduced in {result.game}: {result.reason}. "
             f"Report: {result.report_filepath}")
        return 1
    _log("No stall this run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
