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

"""Dump memory at the hotspot addresses found by PC sampling."""

from __future__ import annotations

import os
import time
from pathlib import Path

from beebium.client import Beebium
from beebium.client.screen import read_mode7_screen


def _wait_for_screen_text(bbc, text, timeout_seconds=120.0, poll_interval=0.5):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        if text in "\n".join(rows):
            return True
        time.sleep(poll_interval)
    return False


def hex_dump(mem, start, length):
    """Hex dump memory range."""
    data = mem.address.peek[start:start + length]
    for offset in range(0, length, 16):
        addr = start + offset
        chunk = data[offset:offset + 16]
        hex_str = " ".join(f"{b:02X}" for b in chunk)
        print(f"  ${addr:04X}: {hex_str}")


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
        assert _wait_for_screen_text(bbc, ">", timeout_seconds=30)
        bbc.keyboard.type("*RUN FS3v126\r")
        assert _wait_for_screen_text(bbc, "Number of drives:", timeout_seconds=120)
        bbc.keyboard.type("1\r")
        assert _wait_for_screen_text(bbc, "Command :", timeout_seconds=30)
        bbc.keyboard.type("S")
        assert _wait_for_screen_text(bbc, "Stations:", timeout_seconds=10)
        bbc.keyboard.type("2\r")
        assert _wait_for_screen_text(bbc, "01:21", timeout_seconds=60)

        parasite = bbc.connect_coprocessor(timeout=5.0)

        print("=== PARASITE memory at hotspots ($FA70-$FAA0) ===")
        print("(Parasite spends 91% of time at $FA93 and $FA96)\n")
        hex_dump(parasite.memory, 0xFA70, 48)

        print("\n=== PARASITE registers ===")
        regs = parasite.cpu.registers
        print(f"  PC=${regs.pc:04X} A=${regs.a:02X} X=${regs.x:02X} Y=${regs.y:02X} SP=${regs.sp:02X} P=${regs.p:02X}")

        print("\n=== HOST memory at hotspots ===")
        print("\n--- $8E40-$8E60 (host spends 21% here) ---")
        hex_dump(bbc.memory, 0x8E40, 32)

        print("\n--- $0030-$0050 (host spends 18% here) ---")
        hex_dump(bbc.memory, 0x0030, 32)

        print("\n--- $F168-$F190 (host spends 8% here) ---")
        hex_dump(bbc.memory, 0xF168, 40)

        print("\n--- $06C0-$06D0 (host spends 3% here) ---")
        hex_dump(bbc.memory, 0x06C0, 16)

        print("\n=== HOST registers ===")
        regs = bbc.cpu.registers
        print(f"  PC=${regs.pc:04X} A=${regs.a:02X} X=${regs.x:02X} Y=${regs.y:02X} SP=${regs.sp:02X} P=${regs.p:02X}")

        parasite.close()


if __name__ == "__main__":
    main()
