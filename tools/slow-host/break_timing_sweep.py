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

"""Sweep the emulated delay between the Tube banner and a plain BREAK.

For each delay given on the command line (emulated seconds), launches a Tube
Model B with DFS 2.26 and the auto-boot keyboard link, fast-forwards unpaced to
the "Acorn TUBE" banner, inserts the Elite disc, advances exactly that much
emulated time, then presses BREAK at the slow speed and fast-forwards to see
whether Elite boots. It reports, per delay, whether the machine booted, or came
up on the tape filing system ("Searching"), which is the #132 failure. With no
delays given it sweeps 0 to 1 s. Written for #132; delays of 0 to 1 s all
booted, so the delay alone is not the trigger.

Run under the Python client's environment, e.g.::

    uv run --project clients/beebium-python-client \\
        python tools/slow-host/break_timing_sweep.py --speed 0.4 0 0.05 0.1 0.5 1.0
"""

from __future__ import annotations

import argparse
from pathlib import Path

from beebium.client import Beebium
from beebium.client.screen import screen_contains

ELITE_BANNER = "6502 Second Processor ELITE"


def _repo_root() -> Path:
    here = Path(__file__).resolve()
    for parent in here.parents:
        if (parent / "roms").is_dir() and (parent / "clients").is_dir():
            return parent
    raise RuntimeError("could not locate beebium repo root")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("delays", nargs="*", type=float,
                   default=[0.0, 0.02, 0.05, 0.1, 0.2, 0.3, 0.5, 1.0],
                   help="emulated seconds between the banner and BREAK")
    p.add_argument("--speed", type=float, default=0.4,
                   help="speed for the real-time parts, standing in for a slow host")
    args = p.parse_args()

    root = _repo_root()
    dfs_filepath = root / "roms" / "acorn-dfs_2_26.rom"
    disc_filepath = root / "tests" / "assets" / "discs" / "Disc999-EliteSNG45.ssd"

    def fast_forward_until(bbc: Beebium, predicate, seconds: float, chunk: float) -> bool:
        bbc.system.set_speed_multiplier(0.0)
        try:
            return bbc.run_until_or_timeout(predicate, seconds, chunk_seconds=chunk)
        finally:
            bbc.system.set_speed_multiplier(args.speed)

    for delay in args.delays:
        with Beebium.launch(extra_args=["--tube-65c02", "--fdc", "acorn-1770",
                                        "--sideways", f"slot=14:type=rom:image={dfs_filepath}",
                                        "--auto-boot"]) as bbc:
            bbc.disc.set_spin_up_delay(False)
            if not fast_forward_until(bbc, lambda: screen_contains(bbc, "Acorn TUBE"), 30.0, 0.05):
                print(f"delay={delay:5.2f}s: no Tube banner")
                continue
            banner_cycle = bbc.debugger.cycle_count
            bbc.disc.drive(0).insert(disc_filepath)
            if delay > 0:
                fast_forward_until(bbc, lambda: False, delay, delay)
            break_after = (bbc.debugger.cycle_count - banner_cycle) / 2_000_000
            bbc.debugger.ensure_running()
            bbc.keyboard.press_break()
            booted = fast_forward_until(
                bbc, lambda: screen_contains(bbc, ELITE_BANNER), 60.0, 1.0)
            text = bbc.video.screen_text().text
            outcome = "BOOT" if booted else ("TAPE" if "Searching" in text else "NO BOOT")
            print(f"delay={delay:5.2f}s (BREAK {break_after:.3f}s after the banner): {outcome}",
                  flush=True)


if __name__ == "__main__":
    main()
