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

"""Reproduce and capture the COMAL long-run emulation hang.

Boots model-b-plus with the Acornsoft COMAL ROM, loads the "chaos game"
Sierpinski program from the weekend-challenge disc, runs it, and watches the
emulated cycle counter for forward progress. When the emulation stalls (the
reported symptom: the drawing freezes and the cursor stops blinking), it
captures a diagnostic snapshot -- a process sample, an all-thread backtrace,
CPU usage, pacing stats and a gRPC liveness probe -- then holds the frozen
server so it can also be inspected by hand.

The hang is a server-side emulation-thread stall: when it occurs gRPC stays
responsive (the frontend remains usable) but cycle_count stops advancing, so
the cycle counter is the liveness signal and a frozen-but-answering server is
the signature.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeout
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from beebium.client import Beebium
from beebium.client.screen import dump_screen, read_mode7_screen

REPO = Path(__file__).resolve().parents[4]
DEFAULT_ROMS = REPO / "roms"
DEFAULT_SERVER = REPO / "build" / "src" / "server" / "beebium-model-b-plus"
ASSET_SSD = Path(__file__).resolve().parents[2] / "assets" / "weekend-challenge.ssd"
REPORTS_DIRPATH = Path(__file__).resolve().parents[2] / "reports"

COMAL_SLOT = 10
DFS_SLOT = 11

# COMAL uses a right-bracket prompt, not BASIC's '>'.
COMAL_PROMPT = "]"
BASIC_PROMPT = ">"


def _now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _log(message: str) -> None:
    stamp = datetime.now(timezone.utc).strftime("%H:%M:%S")
    print(f"[{stamp}] {message}", flush=True)


def _call_with_timeout(executor: ThreadPoolExecutor, fn, timeout: float):
    """Run a (possibly blocking) gRPC call with a hard timeout.

    Returns (ok, value_or_exception). A timeout means the server's gRPC layer
    is itself wedged, which is a distinct and louder failure than a frozen
    cycle counter.
    """
    future = executor.submit(fn)
    try:
        return True, future.result(timeout=timeout)
    except FutureTimeout:
        return False, "gRPC call timed out"
    except Exception as exc:  # noqa: BLE001 - diagnostic harness records anything
        return False, exc


def _screen_has_prompt(bbc: Beebium, prompt: str) -> bool:
    for row in read_mode7_screen(bbc):
        if row.strip().startswith(prompt):
            return True
    return False


def _wait_for(predicate, timeout: float, poll: float = 0.25) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(poll)
    return False


# ----------------------------------------------------------------------------
# Setup sequencing
# ----------------------------------------------------------------------------

def boot_and_load(bbc: Beebium) -> None:
    """Boot, switch to COMAL, LOAD TRIANG, then RUN."""
    _log("Waiting for BASIC prompt...")
    if not _wait_for(lambda: _screen_has_prompt(bbc, BASIC_PROMPT), timeout=15.0):
        raise RuntimeError(f"No BASIC prompt after boot:\n{dump_screen(bbc)}")

    _log("Switching to COMAL (*COMAL)...")
    bbc.keyboard.type("*COMAL\r")
    if not _wait_for(lambda: _screen_has_prompt(bbc, COMAL_PROMPT), timeout=10.0):
        raise RuntimeError(f"No COMAL ']' prompt after *COMAL:\n{dump_screen(bbc)}")

    _log("Loading program (LOAD TRIANG)...")
    bbc.keyboard.type("LOAD TRIANG\r")
    # The load is bracketed by drive activity: motor on, then off.
    if not _wait_for(lambda: bbc.disc.drive0.motor_on, timeout=8.0):
        _log("WARNING: never saw motor spin up for LOAD; continuing")
    if not _wait_for(lambda: not bbc.disc.drive0.motor_on, timeout=30.0):
        raise RuntimeError("Drive never went idle; LOAD may not have completed")
    if not _wait_for(lambda: _screen_has_prompt(bbc, COMAL_PROMPT), timeout=10.0):
        raise RuntimeError(f"No COMAL prompt after LOAD:\n{dump_screen(bbc)}")

    _log("Running program (RUN)...")
    bbc.keyboard.type("RUN\r")
    # Confirm the emulation is actually advancing before we start watching.
    start = bbc.debugger.cycle_count
    if not _wait_for(lambda: bbc.debugger.cycle_count > start + 1_000_000, timeout=5.0):
        raise RuntimeError("cycle_count did not advance after RUN")
    _log("Program running; watching for a stall.")


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


def capture_diagnostics(bbc: Beebium, executor: ThreadPoolExecutor,
                        reason: str) -> Path:
    REPORTS_DIRPATH.mkdir(exist_ok=True)
    report_filepath = REPORTS_DIRPATH / f"hang-{_now_iso()}.txt"
    pid = _server_pid(bbc)

    lines: list[str] = []
    lines.append(f"COMAL soak hang report")
    lines.append(f"captured: {_now_iso()}")
    lines.append(f"reason: {reason}")
    lines.append(f"server pid: {pid}")
    lines.append(f"target: {bbc.target}")
    lines.append("")

    # gRPC liveness probe across two services: is the service layer still
    # answering while the emulation is frozen? (The reported symptom says yes.)
    # Sample the CPU registers (esp. PC) twice: if the emulation thread is
    # alive PC should move; if it is wedged PC is identical both times.
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

    if pid is not None:
        lines.append("=== CPU usage (ps) ===")
        lines.append(_run(["ps", "-o", "pid,%cpu,state,time,command", "-p", str(pid)], 10))
        lines.append("")

        # `sample` needs no debug entitlement and is ideal for a spin/livelock:
        # it shows where the thread is burning cycles.
        lines.append("=== process sample (5s) ===")
        lines.append(_run(["/usr/bin/sample", str(pid), "5", "-mayDie"], 30))
        lines.append("")

        # An all-thread backtrace is ideal for a deadlock: it shows the blocked
        # stack and which lock/cv each thread waits on.
        lines.append("=== lldb thread backtrace all ===")
        lines.append(_run([
            "lldb", "-p", str(pid), "--batch",
            "-o", "thread backtrace all", "-o", "detach", "-o", "quit",
        ], 30))

    report_filepath.write_text("\n".join(lines))
    _log(f"Diagnostics written to {report_filepath}")
    return report_filepath


# ----------------------------------------------------------------------------
# Soak loop
# ----------------------------------------------------------------------------

@dataclass
class SoakResult:
    hung: bool
    wall_seconds: float
    emulated_cycles: int
    report_filepath: Path | None


def run_soak(args: argparse.Namespace) -> SoakResult:
    extra_args = [
        "--sideways", f"slot={COMAL_SLOT}:type=rom:image={args.roms / 'comal_1_0.rom'}",
        "--sideways", f"slot={DFS_SLOT}:type=rom:image={args.roms / 'acorn-dfs_2_26.rom'}",
        "--floppy", f"0:{args.ssd}",
    ]
    if args.advertise:
        # The advertiser must be constructed at launch; the runtime toggle is a
        # no-op without it. With this flag the server is discoverable over mDNS
        # so the macOS frontend can attach and watch the same emulator.
        extra_args.append("--advertise")

    with ThreadPoolExecutor(max_workers=1) as executor, Beebium.launch(
        mos_filepath=args.roms / "acorn-mos_2_0.rom",
        basic_filepath=args.roms / "bbc-basic_2.rom",
        server_filepath=args.server,
        extra_args=extra_args,
        startup_timeout=20.0,
    ) as bbc:
        bbc.disc.set_spin_up_delay(False)

        if args.advertise:
            # Advertise over mDNS so the macOS frontend can discover this
            # server and watch the run (and the frozen screen after a hang).
            # Bonjour registration is asynchronous, so poll the state briefly
            # rather than trusting the immediate reply.
            bbc.system.set_advertisement(enabled=True)
            state = bbc.system.get_advertisement_state()
            if not state.enabled and state.available:
                _wait_for(lambda: bbc.system.get_advertisement_state().enabled,
                          timeout=3.0)
                state = bbc.system.get_advertisement_state()
            if state.enabled:
                _log(f"Advertising over mDNS as: {state.advertised_name!r} "
                     f"(connect the macOS frontend to watch)")
            elif not state.available:
                _log("mDNS advertisement not available on this platform")
            else:
                _log("mDNS advertisement requested but not confirmed yet")

        if args.speed != 1.0:
            bbc.system.set_speed_multiplier(args.speed)
            _log(f"Speed multiplier set to {args.speed} (0.0 = unlimited)")

        boot_and_load(bbc)

        start_wall = time.monotonic()
        start_cycles = bbc.debugger.cycle_count
        last_cycles = start_cycles
        last_progress_wall = start_wall
        last_report_wall = start_wall
        run_forever = args.max_minutes <= 0
        deadline = start_wall + args.max_minutes * 60.0
        if run_forever:
            _log("Running indefinitely (no deadline); Ctrl-C or kill to stop.")

        last_read_ok = True
        while True:
            time.sleep(args.poll_seconds)

            ok, value = _call_with_timeout(
                executor, lambda: bbc.debugger.cycle_count, args.rpc_timeout
            )
            now = time.monotonic()
            last_read_ok = ok

            if ok:
                cycles = value
                if cycles > last_cycles:
                    last_cycles = cycles
                    last_progress_wall = now
            else:
                # A failed/timed-out read does not by itself mean a hang: a
                # whole-laptop sleep/wake briefly drops the gRPC link. Treat it
                # the same as "no progress observed" and let the unified stall
                # timer decide -- a transient blip recovers and resets the
                # clock; only a persistent freeze trips capture.
                cycles = last_cycles
                _log(f"cycle_count read failed (transient?): {value}")

            stalled_for = now - last_progress_wall
            if now - last_report_wall >= args.report_seconds:
                rate = (cycles - start_cycles) / max(now - start_wall, 1e-9)
                ok_p, pacing = _call_with_timeout(
                    executor, lambda: bbc.system.get_pacing_stats(), args.rpc_timeout
                )
                drift = getattr(pacing, "controller_drift", "?") if ok_p else "?"
                _log(
                    f"wall={now - start_wall:7.0f}s cycles={cycles:>14d} "
                    f"rate={rate/1e6:6.2f}MHz stalled={stalled_for:4.0f}s drift={drift}"
                )
                last_report_wall = now

            if stalled_for >= args.stall_seconds:
                signature = ("cycle_count frozen (gRPC responsive)" if last_read_ok
                             else "cycle_count unobservable (gRPC not answering)")
                _log(f"STALL: {signature} at {cycles} for {stalled_for:.0f}s")
                report = capture_diagnostics(
                    bbc, executor,
                    reason=f"{signature} at {cycles} for {stalled_for:.0f}s",
                )
                _hold(bbc, args.hold_minutes)
                return SoakResult(True, now - start_wall, cycles - start_cycles, report)

            if not run_forever and now >= deadline:
                _log(f"No stall within {args.max_minutes} min; giving up this run.")
                return SoakResult(
                    False, now - start_wall, cycles - start_cycles, None
                )


def _hold(bbc: Beebium, hold_minutes: float) -> None:
    # hold_minutes <= 0 means hold indefinitely (until the process is killed),
    # so the frozen server stays up for diagnosis no matter when it fires.
    forever = hold_minutes <= 0
    horizon = "indefinitely" if forever else f"for {hold_minutes:.0f} min"
    _log(
        f"Holding the frozen server {horizon} for inspection at "
        f"{bbc.target} (pid {_server_pid(bbc)}). Ctrl-C or kill to stop."
    )
    try:
        if forever:
            while True:
                time.sleep(3600.0)
        else:
            time.sleep(hold_minutes * 60.0)
    except KeyboardInterrupt:
        _log("Interrupted; shutting down.")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--speed", type=float, default=1.0,
                        help="Speed multiplier (1.0 real-time, 0.0 unlimited).")
    parser.add_argument("--max-minutes", type=float, default=45.0,
                        help="Give up after this many wall-clock minutes "
                             "(0 or less = run indefinitely).")
    parser.add_argument("--stall-seconds", type=float, default=20.0,
                        help="No cycle_count progress for this long = a hang.")
    parser.add_argument("--poll-seconds", type=float, default=2.0,
                        help="Liveness poll interval.")
    parser.add_argument("--report-seconds", type=float, default=30.0,
                        help="How often to print a progress line.")
    parser.add_argument("--rpc-timeout", type=float, default=10.0,
                        help="Per-call gRPC timeout before declaring it wedged.")
    parser.add_argument("--hold-minutes", type=float, default=30.0,
                        help="Keep the frozen server alive this long after a "
                             "hang (0 or less = hold indefinitely).")
    parser.add_argument("--advertise", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Advertise over mDNS so the macOS frontend can watch.")
    parser.add_argument("--roms", type=Path, default=DEFAULT_ROMS)
    parser.add_argument("--server", type=Path, default=DEFAULT_SERVER)
    parser.add_argument("--ssd", type=Path, default=ASSET_SSD)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.server.exists():
        print(f"Server not found: {args.server}", file=sys.stderr)
        return 2
    result = run_soak(args)
    if result.hung:
        _log(f"HANG reproduced after {result.wall_seconds:.0f}s "
             f"({result.emulated_cycles/1e6:.0f}M emulated cycles). "
             f"Report: {result.report_filepath}")
        return 1
    _log("No hang this run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
