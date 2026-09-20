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

"""Measure R2 throughput relative to pacing tick rate.

If the host and parasite are independently paced at 200 Hz, each Tube R2
exchange may cost one tick boundary crossing (~5ms). This script measures
R2 bytes per second to confirm.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

import grpc

from beebium.client import Beebium
from beebium.client.screen import dump_screen, read_mode7_screen

sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent / "clients" / "python" / "src"))
from beebium.client._proto import debugger_pb2, debugger_pb2_grpc


def _wait_for_screen_text(bbc, text, timeout_seconds=120.0, poll_interval=0.5):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        if text in "\n".join(rows):
            return True
        time.sleep(poll_interval)
    return False


def _get_tube_r2_counts(channel):
    stub = debugger_pb2_grpc.DeviceInspectionStub(channel)
    resp = stub.GetTubeState(debugger_pb2.GetTubeStateRequest())
    c = resp.counters
    return c.r2_h2p_writes, c.r2_p2h_writes


def main():
    repo_root = Path(__file__).parent.parent.parent.parent
    roms = repo_root / "roms"

    server_filepath = os.environ.get(
        "BEEBIUM_SERVER",
        str(repo_root / "build-release" / "src" / "server" / "beebium-model-b-romram"),
    )

    extra_args = [
        "--sideways", f"slot=9:type=rom:image={roms / 'acorn-anfs_4_18.rom'}",
        "--sideways", f"slot=10:type=rom:image={roms / 'acorn-adfs_1_30.rom'}",
        "--sideways", f"slot=11:type=rom:image={roms / 'acorn-dfs_2_26.rom'}",
        "--fdc", "acorn-1770",
        "--floppy", "0:/Users/rjs/Code/L3V126/FS3v126.ssd",
        "--acorn-scsi",
        "--scsi-hdd", "0:/Users/rjs/Code/beebem-windows/UserData/DiscIms/scsi0.dat",
        "--station", "254",
        "--aun", "port=0",
        "--machine-name", "L3FS-DIAG",
        "--acorn-rtc", "layout=7bit-year-in-r7:time=1985-10-26T0121",
        "--tube", "65C02-3MHz",
        "--advertise",
    ]

    with Beebium.launch(
        mos_filepath=roms / "acorn-mos_1_20.rom",
        basic_filepath=roms / "bbc-basic_2.rom",
        server_filepath=server_filepath,
        extra_args=extra_args,
        startup_timeout=30.0,
    ) as bbc:
        assert _wait_for_screen_text(bbc, ">", timeout_seconds=30)
        bbc.keyboard.type("*RUN FS3v126\r")
        assert _wait_for_screen_text(bbc, "Number of drives:", timeout_seconds=120)
        bbc.keyboard.type("1\r")
        assert _wait_for_screen_text(bbc, "Command :", timeout_seconds=30)
        bbc.keyboard.type("S")
        assert _wait_for_screen_text(bbc, "Stations:", timeout_seconds=10)
        bbc.keyboard.type("2\r")
        assert _wait_for_screen_text(bbc, "01:21", timeout_seconds=60)

        print("L3FS running.\n")

        host_channel = grpc.insecure_channel(bbc.target)
        parasite = bbc.connect_coprocessor(timeout=5.0)

        # Measure over short intervals to see the pattern
        print(f"{'Interval':>8}  {'R2 h2p':>8}  {'R2 p2h':>8}  {'h2p/s':>8}  {'p2h/s':>8}  "
              f"{'OSBYTE/s':>9}  {'ms/OSBYTE':>10}  {'Para cyc':>12}  {'Host cyc':>12}")
        print("-" * 120)

        for i in range(12):
            r2_h0, r2_p0 = _get_tube_r2_counts(host_channel)
            pc0 = parasite.debugger.cycle_count
            hc0 = bbc.debugger.cycle_count
            t0 = time.monotonic()

            time.sleep(5.0)

            r2_h1, r2_p1 = _get_tube_r2_counts(host_channel)
            pc1 = parasite.debugger.cycle_count
            hc1 = bbc.debugger.cycle_count
            t1 = time.monotonic()

            dt = t1 - t0
            r2h = r2_h1 - r2_h0
            r2p = r2_p1 - r2_p0
            h2p_rate = r2h / dt
            p2h_rate = r2p / dt

            # Each OSBYTE = 3 p2h bytes (command type + X + A) + 1 h2p byte (result X)
            # So OSBYTE count ≈ h2p count (one response per OSBYTE)
            osbyte_rate = h2p_rate
            ms_per_osbyte = 1000 / osbyte_rate if osbyte_rate > 0 else float('inf')

            pd = pc1 - pc0
            hd = hc1 - hc0

            print(f"{i+1:>8}  {r2h:>8}  {r2p:>8}  {h2p_rate:>8.0f}  {p2h_rate:>8.0f}  "
                  f"{osbyte_rate:>9.0f}  {ms_per_osbyte:>10.1f}  {pd:>12,}  {hd:>12,}")

        print(f"\nAt 200 Hz pacing, theoretical max = 200 OSBYTE/s (if each costs 1 tick)")
        print(f"Expected on real hardware: ~1100 OSBYTE/s (0.9ms each)")
        print(f"WAIT3 needs 400 OSBYTE calls.")
        print(f"  At 1100/s: 0.36s per WAIT3, 85*0.36 = 30.6s per PRTIM")
        print(f"  At 200/s:  2.0s per WAIT3, 85*2.0 = 170s per PRTIM")
        print(f"  At 67/s:   6.0s per WAIT3, 85*6.0 = 510s per PRTIM")

        parasite.close()
        host_channel.close()


if __name__ == "__main__":
    main()
