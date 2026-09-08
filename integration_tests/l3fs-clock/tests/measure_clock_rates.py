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

"""Measure effective host and parasite clock rates and Tube transfer
rates during L3FS operation.

Empirically determines where time is being spent.
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
        screen_text = "\n".join(rows)
        if text in screen_text:
            return True
        time.sleep(poll_interval)
    return False


def _get_tube_counters(channel):
    """Read Tube transfer counters via DeviceInspection.GetTubeState."""
    stub = debugger_pb2_grpc.DeviceInspectionStub(channel)
    request = debugger_pb2.GetTubeStateRequest()
    response = stub.GetTubeState(request)
    c = response.counters
    return {
        "r1_h2p_w": c.r1_h2p_writes, "r1_h2p_r": c.r1_h2p_reads,
        "r2_h2p_w": c.r2_h2p_writes, "r2_h2p_r": c.r2_h2p_reads,
        "r3_h2p_w": c.r3_h2p_writes, "r3_h2p_r": c.r3_h2p_reads,
        "r4_h2p_w": c.r4_h2p_writes, "r4_h2p_r": c.r4_h2p_reads,
        "r1_p2h_w": c.r1_p2h_writes, "r1_p2h_r": c.r1_p2h_reads,
        "r2_p2h_w": c.r2_p2h_writes, "r2_p2h_r": c.r2_p2h_reads,
        "r3_p2h_w": c.r3_p2h_writes, "r3_p2h_r": c.r3_p2h_reads,
        "r4_p2h_w": c.r4_p2h_writes, "r4_p2h_r": c.r4_p2h_reads,
    }


def main():
    repo_root = Path(__file__).parent.parent.parent.parent
    roms = repo_root / "roms"

    server_filepath = os.environ.get(
        "BEEBIUM_SERVER",
        str(repo_root / "build-release" / "src" / "server" / "beebium-model-b-romram"),
    )

    extra_args = [
        "--sideways", f"9:rom:{roms / 'acorn-anfs_4_18.rom'}",
        "--sideways", f"10:rom:{roms / 'acorn-adfs_1_30.rom'}",
        "--sideways", f"11:rom:{roms / 'acorn-dfs_2_26.rom'}",
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
        print("Waiting for BASIC prompt...")
        assert _wait_for_screen_text(bbc, ">", timeout_seconds=30), \
            f"Boot failed:\n{dump_screen(bbc)}"

        print("Starting L3FS...")
        bbc.keyboard.type("*RUN FS3v126\r")
        assert _wait_for_screen_text(bbc, "Number of drives:", timeout_seconds=120), \
            f"FS failed:\n{dump_screen(bbc)}"
        bbc.keyboard.type("1\r")
        assert _wait_for_screen_text(bbc, "Command :", timeout_seconds=30), \
            f"FS failed:\n{dump_screen(bbc)}"
        bbc.keyboard.type("S")
        assert _wait_for_screen_text(bbc, "Stations:", timeout_seconds=10), \
            f"FS failed:\n{dump_screen(bbc)}"
        bbc.keyboard.type("2\r")
        assert _wait_for_screen_text(bbc, "01:21", timeout_seconds=60), \
            f"FS failed:\n{dump_screen(bbc)}"

        print("L3FS is running. Connecting to parasite...\n")

        parasite = bbc.connect_coprocessor(timeout=5.0)
        host_channel = grpc.insecure_channel(bbc.target)

        # ---- Measurement loop ----
        interval = 10.0
        num_intervals = 6

        print(f"Measuring over {num_intervals} x {interval:.0f}s intervals\n")

        # Header
        print(f"{'#':>3}  {'Host MHz':>9}  {'Para MHz':>9}  {'Ratio':>6}  "
              f"{'R4 h2p/s':>9}  {'R4 p2h/s':>9}  "
              f"{'R2 h2p/s':>9}  {'R2 p2h/s':>9}  "
              f"{'Para cyc/R4':>12}")
        print("-" * 110)

        for i in range(num_intervals):
            host_c0 = bbc.debugger.cycle_count
            para_c0 = parasite.debugger.cycle_count
            tube0 = _get_tube_counters(host_channel)
            t0 = time.monotonic()

            time.sleep(interval)

            host_c1 = bbc.debugger.cycle_count
            para_c1 = parasite.debugger.cycle_count
            tube1 = _get_tube_counters(host_channel)
            t1 = time.monotonic()

            dt = t1 - t0
            host_d = host_c1 - host_c0
            para_d = para_c1 - para_c0

            host_mhz = host_d / dt / 1e6
            para_mhz = para_d / dt / 1e6
            ratio = para_d / host_d if host_d else 0

            # Tube R4 is the OSBYTE/OSWORD command channel
            r4_h2p_d = (tube1["r4_h2p_w"] - tube0["r4_h2p_w"])
            r4_p2h_d = (tube1["r4_p2h_w"] - tube0["r4_p2h_w"])
            # Tube R2 is the single-byte data transfer channel
            r2_h2p_d = (tube1["r2_h2p_w"] - tube0["r2_h2p_w"])
            r2_p2h_d = (tube1["r2_p2h_w"] - tube0["r2_p2h_w"])

            r4_rate_h2p = r4_h2p_d / dt
            r4_rate_p2h = r4_p2h_d / dt
            r2_rate_h2p = r2_h2p_d / dt
            r2_rate_p2h = r2_p2h_d / dt

            # Parasite cycles per R4 transfer (rough OSBYTE cost)
            total_r4 = r4_h2p_d + r4_p2h_d
            para_per_r4 = para_d / total_r4 if total_r4 else 0

            print(f"{i+1:>3}  {host_mhz:>9.3f}  {para_mhz:>9.3f}  {ratio:>6.3f}  "
                  f"{r4_rate_h2p:>9.0f}  {r4_rate_p2h:>9.0f}  "
                  f"{r2_rate_h2p:>9.0f}  {r2_rate_p2h:>9.0f}  "
                  f"{para_per_r4:>12.0f}")

        # Print cumulative tube counters
        final_tube = _get_tube_counters(host_channel)
        print(f"\nCumulative Tube transfer counts:")
        for name, val in sorted(final_tube.items()):
            if val > 0:
                print(f"  {name:>12}: {val:>12,}")

        parasite.close()
        host_channel.close()


if __name__ == "__main__":
    main()
