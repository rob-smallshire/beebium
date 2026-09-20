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

"""Tube OSBYTE throughput test.

Measures wall-clock time for OSBYTE calls through the Tube under normal
paced execution. No coupled stepping -- pure real-time pacing throughout,
matching interactive use.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import dump_screen, read_mode7_screen, screen_contains

OSBYTE_COUNT = 1024


def _wait_for(bbc, text, timeout=60):
    """Wait for text to appear at the start of a screen row."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for row in read_mode7_screen(bbc):
            if row.strip().startswith(text):
                return True
        time.sleep(0.5)
    return False


@pytest.fixture(scope="function")
def bbc(beebium_roms_dirpath: Path, mos_filepath: Path, basic_filepath: Path | None):
    """Model B ROM/RAM board with Tube and ANFS. No coupled stepping."""
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    server = None
    for c in [
        repo_root / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]:
        if c.exists():
            server = c
            break
    if server is None:
        pytest.skip("beebium-model-b-romram not found")

    anfs = beebium_roms_dirpath / "acorn-anfs_4_18.rom"
    if not anfs.exists():
        pytest.skip(f"ANFS ROM not found: {anfs}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server,
            extra_args=[
                "--sideways",
                f"slot=9:type=rom:image={anfs}",
                "--tube-65c02",
                "--station",
                "254",
                "--aun",
                "port=0",
            ],
            startup_timeout=20.0,
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.timeout(300)
def test_tube_osbyte_throughput(bbc):
    """Measure OSBYTE 51 throughput through the Tube under normal pacing."""

    # Wait for boot (normal pacing, no coupled stepping)
    assert _wait_for(bbc, ">", timeout=30), f"Boot failed:\n{dump_screen(bbc)}"
    assert screen_contains(bbc, "TUBE"), f"Tube not active:\n{dump_screen(bbc)}"

    # Machine code at &2000 (13 bytes): 256-iteration OSBYTE 51 loop.
    #   LDA #51 / LDX #0 / JSR &FFF4 / INC &2030 / BNE loop / RTS
    # Counter at &2030 wraps 0->255->0, giving exactly 256 calls per CALL.
    # BASIC calls it N_BLOCKS times, timing each block of 256 calls.
    n_blocks = OSBYTE_COUNT // 256
    lines = [
        "10 FOR I%=0 TO 12:READ B%:?(&2000+I%)=B%:NEXT",
        f"20 N%={n_blocks}",
        "30 DIM R%(N%)",
        '40 PRINT "=GO"',
        "50 FOR I%=1 TO N%",
        "60 ?&2030=0:T%=TIME:CALL &2000:R%(I%)=TIME-T%",
        "70 NEXT",
        "80 FOR I%=1 TO N%:PRINT R%(I%);:NEXT",
        '90 PRINT:PRINT "=OK"',
        "100 DATA &A9,&33,&A2,0,&20,&F4,&FF,&EE,&30,&20,&D0,&F4,&60",
        "RUN",
    ]
    for line in lines:
        bbc.keyboard.type(line + "\r")
        time.sleep(0.3)

    # Wait for =GO
    assert _wait_for(bbc, "=GO", timeout=60), f"Program did not reach =GO:\n{dump_screen(bbc)}"

    t0 = time.monotonic()

    # Wait for =OK
    ok = _wait_for(bbc, "=OK", timeout=180)
    t1 = time.monotonic()
    wall_seconds = t1 - t0

    rows = read_mode7_screen(bbc)
    print(f"\n{dump_screen(bbc)}")

    if not ok:
        print(f"\n{dump_screen(bbc)}")
        pytest.fail(f"{OSBYTE_COUNT} OSBYTE 51 calls did not complete in 180 wall-clock seconds under normal pacing.")

    # Parse per-block centisecond values from the output.
    # Values are printed space-separated between =GO and =OK lines.
    # Collect integers only from rows between these markers.
    latencies_cs = []
    in_results = False
    for row in rows:
        s = row.strip()
        if s.startswith("=OK"):
            break
        if in_results and s:
            for token in s.split():
                try:
                    latencies_cs.append(int(token))
                except ValueError:
                    pass
        if s.startswith("=GO"):
            in_results = True

    n_blocks = OSBYTE_COUNT // 256
    wall_rate = OSBYTE_COUNT / wall_seconds if wall_seconds > 0 else float("inf")
    wall_latency = wall_seconds / OSBYTE_COUNT * 1000 if wall_seconds > 0 else 0

    print(f"\n{'=' * 60}")
    print(f"  Tube OSBYTE 51 throughput ({OSBYTE_COUNT} calls)")
    print(f"{'=' * 60}")
    print(f"  Calls:       {OSBYTE_COUNT} ({n_blocks} blocks of 256)")
    print(f"  Wall time:   {wall_seconds:.3f}s")
    print(f"  Wall rate:   {wall_rate:.0f} OSBYTE/s")
    print(f"  Wall avg:    {wall_latency:.2f} ms/call")

    if latencies_cs:
        # Each value is centiseconds for 256 OSBYTE calls
        block_secs = [cs / 100.0 for cs in latencies_cs]
        block_rates = [256 / s if s > 0 else float("inf") for s in block_secs]
        per_call_ms = [s / 256 * 1000 for s in block_secs]
        total_cs = sum(latencies_cs)
        total_secs = total_cs / 100.0
        emu_rate = OSBYTE_COUNT / total_secs if total_secs > 0 else float("inf")

        print("\n  BASIC TIME (emulated, 10ms resolution):")
        print(f"    total:     {total_secs:.2f}s ({total_cs} cs)")
        print(f"    emu rate:  {emu_rate:.0f} OSBYTE/s")
        print("\n  Per-block breakdown (256 OSBYTE each):")
        print(f"  {'Block':>5}  {'Time(cs)':>8}  {'OSBYTE/s':>9}  {'ms/call':>8}")
        for i, (cs, rate, lat) in enumerate(zip(latencies_cs, block_rates, per_call_ms, strict=False)):
            print(f"  {i:>5}  {cs:>8}  {rate:>9.0f}  {lat:>8.2f}")
        if len(block_rates) > 1:
            finite_rates = [r for r in block_rates if r != float("inf")]
            if finite_rates:
                print(f"  {'min':>5}  {min(latencies_cs):>8}  {max(finite_rates):>9.0f}  {min(per_call_ms):>8.2f}")
                print(f"  {'max':>5}  {max(latencies_cs):>8}  {min(finite_rates):>9.0f}  {max(per_call_ms):>8.2f}")

    print("\n  Expected (real HW): ~1100 OSBYTE/s, ~0.9 ms/call")
    print(f"{'=' * 60}")

    assert wall_rate > 500, f"OSBYTE throughput {wall_rate:.0f}/s ({wall_latency:.2f}ms each), expected >500/s."
