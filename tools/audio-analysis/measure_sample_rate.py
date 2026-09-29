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

"""Measure the audio sample rate a server delivers against emulated and wall time.

Boots a disc (a fresh copy, with a fresh disc work directory) at 1x, presses
SPACE a few times to get past instruction screens, lets it settle, then counts
the samples the AudioService produced over a window. The count is taken from
the stream's produced-sample index (AudioChunk.sequence), so dropped samples
count as produced, and is divided by the emulated time (the debugger's cycle
count over the same window) and by the wall time. A server that honours its
declared AudioFormat.sample_rate delivers it per emulated second; at 1x the
two figures agree. Chunks are 128 samples, so the window edges limit the
resolution to about 2 Hz over 60 s (#126). Use capture_disc.py to record WAVs.

Run under the Python client's environment, e.g.::

    uv run --project clients/beebium-python-client \\
        python tools/audio-analysis/measure_sample_rate.py \\
        --disc discs/games/Disc025-JetSetWilly.ssd --seconds 60
"""

from __future__ import annotations

import argparse
import os
import shutil
import tempfile
import threading
import time
from pathlib import Path

from beebium.client import Beebium

CHUNK_SAMPLES = 128


def _repo_root() -> Path:
    here = Path(__file__).resolve()
    for parent in here.parents:
        if (parent / "roms").is_dir() and (parent / "clients").is_dir():
            return parent
    raise RuntimeError("could not locate beebium repo root")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--disc", required=True, help="path to the disc image to boot")
    p.add_argument("--seconds", type=float, default=60.0,
                   help="length of the measurement window in seconds (default 60)")
    p.add_argument("--spaces", type=int, default=3,
                   help="SPACE presses after boot, 3 s apart (default 3)")
    p.add_argument("--settle", type=float, default=8.0,
                   help="seconds to wait after the last press before measuring (default 8)")
    p.add_argument("--dfs-rom", default=None,
                   help="DFS ROM path (default: roms/acorn-dfs_2_26.rom)")
    args = p.parse_args()

    dfs_filepath = (Path(args.dfs_rom) if args.dfs_rom
                    else _repo_root() / "roms" / "acorn-dfs_2_26.rom")
    source_disc_filepath = Path(args.disc).expanduser().resolve()

    # Isolate the run: a fresh copy of the disc and an empty disc work directory.
    run_dirpath = Path(tempfile.mkdtemp(prefix="beebium_rate_"))
    disc_filepath = run_dirpath / source_disc_filepath.name
    shutil.copyfile(source_disc_filepath, disc_filepath)
    os.environ["BEEBIUM_DISC_WORK_DIR"] = str(run_dirpath / "workdir")
    os.makedirs(os.environ["BEEBIUM_DISC_WORK_DIR"], exist_ok=True)

    produced_end = [0]  # produced index just past the latest chunk
    lock = threading.Lock()
    stop = threading.Event()

    try:
        with Beebium.launch(
            extra_args=["--fdc", "acorn-1770",
                        "--sideways", f"slot=14:type=rom:image={dfs_filepath}"],
            startup_timeout=20.0,
        ) as bbc:
            sample_rate = bbc.audio.format.sample_rate

            def read_stream() -> None:
                for chunk in bbc.audio.subscribe(chunk_size=CHUNK_SAMPLES):
                    with lock:
                        produced_end[0] = chunk.sequence + chunk.sample_count
                    if stop.is_set():
                        return

            threading.Thread(target=read_stream, daemon=True).start()
            bbc.system.set_speed_multiplier(1.0)
            bbc.debugger.ensure_running()
            bbc.boot_disc(disc_filepath)
            for _ in range(args.spaces):
                time.sleep(3.0)
                bbc.keyboard.type(" ")
            time.sleep(args.settle)

            with lock:
                produced_start = produced_end[0]
            cycles_start = bbc.debugger.cycle_count
            wall_start = time.monotonic()
            time.sleep(args.seconds)
            with lock:
                produced_stop = produced_end[0]
            cycles_stop = bbc.debugger.cycle_count
            wall_stop = time.monotonic()
            clock_hz = bbc.system.clock_speed_hz or 2_000_000
            achieved = bbc.system.get_pacing_stats().achieved_speed_multiplier
            stop.set()
    finally:
        shutil.rmtree(run_dirpath, ignore_errors=True)

    produced = produced_stop - produced_start
    emulated_seconds = (cycles_stop - cycles_start) / clock_hz
    wall_seconds = wall_stop - wall_start
    print(f"declared sample rate:        {sample_rate} Hz")
    print(f"window:                      {wall_seconds:.2f} s wall, "
          f"{emulated_seconds:.3f} s emulated, {produced} samples")
    print(f"samples per emulated second: {produced / emulated_seconds:.1f}")
    print(f"samples per wall second:     {produced / wall_seconds:.1f}")
    print(f"achieved speed:              {emulated_seconds / wall_seconds:.5f} "
          f"(pacing reports {achieved:.4f})")


if __name__ == "__main__":
    main()
