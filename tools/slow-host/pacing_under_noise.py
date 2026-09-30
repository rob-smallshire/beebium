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

"""Stop/resume trials under noisy CPU load, sampling the server's pacing accounting.

Launches a server, renices it to +20 and runs NoisyLoad beside it. Each trial
measures the wall-clock rate over 2 s, stops the machine for STOP_SECONDS,
resumes it, samples GetPacingStats every 50 ms for a second and measures the
rate over the next 2 s. It prints the after/before rate ratio next to the
pacing controller's deficit (target minus actual cycles, positive when behind):
before the stop, the first value after the resume, and the range after it.

This is how #137 was settled. Under this load the ratio swung from 0.55 to
1.95 across trials while the deficit just after every resume stayed within a
few hundred cycles: the pacing re-anchored correctly and the swings were the
host. A pacing fault shows in the deficit instead: a burst starts near
STOP_SECONDS of cycles, a crawl goes strongly negative.

Run under the Python client's environment, e.g.::

    uv run --project clients/beebium-python-client \\
        python tools/slow-host/pacing_under_noise.py --trials 12
"""

from __future__ import annotations

import argparse
import subprocess
import time

from noisy_load import NoisyLoad

from beebium.client import Beebium

STOP_SECONDS = 3.0


def _rate(bbc: Beebium, seconds: float) -> float:
    start_cycles, start = bbc.debugger.cycle_count, time.monotonic()
    time.sleep(seconds)
    return (bbc.debugger.cycle_count - start_cycles) / (time.monotonic() - start)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--trials", type=int, default=12, help="stop/resume trials to run")
    args = p.parse_args()

    with NoisyLoad(), Beebium.launch() as bbc:
        # The server yields the CPU to the hogs whenever they run.
        subprocess.run(["renice", "+20", "-p", str(bbc._server._process.pid)],
                       check=True, stdout=subprocess.DEVNULL)
        bbc.system.set_speed_multiplier(1.0)
        bbc.debugger.ensure_running()
        time.sleep(0.5)
        for trial in range(args.trials):
            before = _rate(bbc, 2.0)
            deficit_before = bbc.system.get_pacing_stats().controller_drift
            bbc.debugger.ensure_stopped()
            time.sleep(STOP_SECONDS)
            ticks_at_resume = bbc.system.get_pacing_stats().ticks_executed

            bbc.debugger.ensure_running()
            resumed_at = time.monotonic()
            deficits = []
            while time.monotonic() - resumed_at < 1.0:
                stats = bbc.system.get_pacing_stats()
                # Only samples after the pacing timer ticked again reflect the
                # accounting after the resume.
                if stats.ticks_executed > ticks_at_resume:
                    deficits.append(stats.controller_drift)
                time.sleep(0.05)
            after = _rate(bbc, 2.0)

            first = f"{deficits[0]:,.0f}" if deficits else "none"
            low = f"{min(deficits):,.0f}" if deficits else "-"
            high = f"{max(deficits):,.0f}" if deficits else "-"
            print(f"trial {trial:2d}: before {before / 1e6:.2f} M/s, after {after / 1e6:.2f} M/s, "
                  f"ratio {after / before:.2f} | deficit before the stop {deficit_before:,.0f}, "
                  f"first after the resume {first}, then {low} to {high}", flush=True)


if __name__ == "__main__":
    main()
