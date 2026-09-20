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

"""Check what the parasite is doing during the L3FS polling loop.

Sample the parasite's PC to see where it's spending time.
"""

from __future__ import annotations

import os
import sys
import time
from collections import Counter
from pathlib import Path

from beebium.client import Beebium
from beebium.client.screen import dump_screen, read_mode7_screen


def _wait_for_screen_text(bbc, text, timeout_seconds=120.0, poll_interval=0.5):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        if text in "\n".join(rows):
            return True
        time.sleep(poll_interval)
    return False


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
        print("Waiting for BASIC prompt...")
        assert _wait_for_screen_text(bbc, ">", timeout_seconds=30)

        print("Starting L3FS...")
        bbc.keyboard.type("*RUN FS3v126\r")
        assert _wait_for_screen_text(bbc, "Number of drives:", timeout_seconds=120)
        bbc.keyboard.type("1\r")
        assert _wait_for_screen_text(bbc, "Command :", timeout_seconds=30)
        bbc.keyboard.type("S")
        assert _wait_for_screen_text(bbc, "Stations:", timeout_seconds=10)
        bbc.keyboard.type("2\r")
        assert _wait_for_screen_text(bbc, "01:21", timeout_seconds=60)

        print("L3FS running. Sampling parasite PC for 10 seconds...\n")

        parasite = bbc.connect_coprocessor(timeout=5.0)

        # Sample the parasite PC rapidly
        pc_counts = Counter()
        samples = 0
        t0 = time.monotonic()
        while time.monotonic() - t0 < 10.0:
            try:
                regs = parasite.cpu.registers
                pc_counts[regs.pc] += 1
                samples += 1
            except Exception:
                pass

        print(f"Collected {samples} PC samples in 10s\n")
        print(f"{'PC':>6}  {'Count':>8}  {'%':>6}  Disassembly")
        print("-" * 60)

        # Show top 20 hottest addresses
        for pc, count in pc_counts.most_common(20):
            pct = 100.0 * count / samples
            # Try to disassemble at this address
            try:
                dis = parasite.debugger.disassemble(pc, 1)
                dis_str = dis[0].text if dis else ""
            except Exception:
                dis_str = ""
            print(f"${pc:04X}  {count:>8}  {pct:>5.1f}%  {dis_str}")

        # Also sample the HOST PC
        print(f"\nSampling host PC for 10 seconds...\n")
        host_pc_counts = Counter()
        host_samples = 0
        t0 = time.monotonic()
        while time.monotonic() - t0 < 10.0:
            try:
                regs = bbc.cpu.registers
                host_pc_counts[regs.pc] += 1
                host_samples += 1
            except Exception:
                pass

        print(f"Collected {host_samples} PC samples in 10s\n")
        print(f"{'PC':>6}  {'Count':>8}  {'%':>6}  Disassembly")
        print("-" * 60)

        for pc, count in host_pc_counts.most_common(20):
            pct = 100.0 * count / host_samples
            try:
                dis = bbc.debugger.disassemble(pc, 1)
                dis_str = dis[0].text if dis else ""
            except Exception:
                dis_str = ""
            print(f"${pc:04X}  {count:>8}  {pct:>5.1f}%  {dis_str}")

        parasite.close()


if __name__ == "__main__":
    main()
