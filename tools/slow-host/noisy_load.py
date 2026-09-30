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

"""CPU load that comes and goes, to make this host behave like a noisy CI runner.

Runs a changing number of ``yes`` processes, re-choosing the count every 0.2 to
2 s from LEVELS, so the CPU available to everything else varies on the same
timescale as a shared runner's. Each process is tracked and killed by its own
PID, never by name. On an M-series Mac the hogs alone barely slow a server;
renice the server below them (renice_servers_plugin.py, or ``renice +20 -p
<pid>``) for the load to bite.

As a command, it runs until interrupted (Ctrl-C or SIGTERM)::

    python tools/slow-host/noisy_load.py [--levels 0,20,40,80] [--seed 7]

From Python, use it as a context manager::

    with NoisyLoad():
        ...
"""

from __future__ import annotations

import argparse
import random
import signal
import subprocess
import threading

LEVELS = (0, 20, 40, 80)


class NoisyLoad:
    """Run CPU hogs whose number changes at random while the context is open."""

    def __init__(self, levels: tuple[int, ...] = LEVELS, seed: int = 7):
        self._levels = levels
        self._rng = random.Random(seed)
        self._hogs: list[subprocess.Popen] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def __enter__(self) -> NoisyLoad:
        self._thread.start()
        return self

    def __exit__(self, *exc) -> None:
        self._stop.set()
        self._thread.join()

    def _run(self) -> None:
        try:
            while not self._stop.is_set():
                want = self._rng.choice(self._levels)
                while len(self._hogs) < want:
                    self._hogs.append(subprocess.Popen(["yes"], stdout=subprocess.DEVNULL))
                while len(self._hogs) > want:
                    hog = self._hogs.pop()
                    hog.kill()
                    hog.wait()
                self._stop.wait(self._rng.uniform(0.2, 2.0))
        finally:
            for hog in self._hogs:
                hog.kill()
                hog.wait()
            self._hogs.clear()


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--levels", default=",".join(str(n) for n in LEVELS),
                   help="comma-separated hog counts to choose among")
    p.add_argument("--seed", type=int, default=7, help="random seed")
    args = p.parse_args()
    levels = tuple(int(n) for n in args.levels.split(","))

    done = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: done.set())
    with NoisyLoad(levels, args.seed):
        try:
            while not done.wait(0.5):
                pass
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
