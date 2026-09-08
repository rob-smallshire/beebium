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

# Quick diagnostic: check if the MOS detects the Tube.

from __future__ import annotations
from pathlib import Path
import pytest
from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen


def run_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=1.0):
    return bbc.run_until_or_timeout(
        predicate, emulated_seconds, chunk_seconds=chunk_seconds)


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_tube_presence_flag(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath, scsi_hdd_filepath,
):
    """Check MOS Tube presence flag at &027A."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
                "--sideways", f"14:rom:{dfs_filepath}",
            ],
            startup_timeout=30.0,
        ) as bbc:
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=30.0,
            )
            assert ok, f"Boot failed:\n{dump_screen(bbc)}"

            # Check MOS Tube presence flag
            tube_flag = bbc.memory.address.peek[0x027A]
            print(f"MOS Tube presence flag (&027A): ${tube_flag:02X}")
            print(f"  bit 7 set = Tube present: {bool(tube_flag & 0x80)}")

            # Also check Tube ULA register readback
            tube_r1_stat = bbc.memory.address.peek[0xFEE0]
            print(f"Tube R1 status (&FEE0): ${tube_r1_stat:02X}")

            # Print the MOS FSCV vector (filing system control vector)
            fscv_lo = bbc.memory.address.peek[0x021E]
            fscv_hi = bbc.memory.address.peek[0x021F]
            print(f"FSCV (&021E): ${fscv_hi:02X}{fscv_lo:02X}")

            assert tube_flag & 0x80, \
                f"MOS does not detect Tube (flag=${tube_flag:02X})"

            # Now check if *LOAD puts data in HOST memory
            # Load the WFSINIT SSD which has WFSINIT at load address &0800
            wfsinit_ssd = Path(__file__).parent.parent.parent.parent / "tests" / "assets" / "scsi" / "wfsinit.ssd"
            if not wfsinit_ssd.exists():
                pytest.skip(f"WFSINIT SSD not found")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_load_puts_data_in_host_ram(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath,
):
    """Check whether *LOAD writes to HOST memory or PARASITE memory.

    If Tube claims aren't working, the data ends up in host RAM.
    """
    ssd_filepath = Path(__file__).parent.parent / "asm" / "test_hello_proper.ssd"
    if not ssd_filepath.exists():
        pytest.skip(f"SSD not found: {ssd_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
                "--sideways", f"14:rom:{dfs_filepath}",
                "--floppy", f"0:{ssd_filepath}",
            ],
            startup_timeout=30.0,
        ) as bbc:
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=30.0,
            )
            assert ok, f"Boot failed:\n{dump_screen(bbc)}"

            # Clear host RAM at &1F00 to distinguish loaded data from zeros
            for i in range(16):
                bbc.memory.address.bus[0x1F00 + i] = 0xAA

            # Load the file (load address is &1F00 from the catalogue)
            bbc.keyboard.type('*LOAD TEST\r')
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "LOAD") and
                        screen_contains(bbc, ">"),
                emulated_seconds=30.0,
            )
            assert ok, f"*LOAD did not complete:\n{dump_screen(bbc)}"

            # Check host memory at &1F00
            host_data = bytes(bbc.memory.address.peek[0x1F00:0x1F10])
            print(f"Host RAM at $1F00 after *LOAD: {host_data.hex()}")

            # Check parasite memory at &1F00
            parasite = bbc.connect_coprocessor()
            para_data = bytes(parasite.memory.address.peek[0x1F00:0x1F10])
            print(f"Parasite RAM at $1F00 after *LOAD: {para_data.hex()}")

            # The hello program starts with LDX #0 (A2 00)
            host_has_data = host_data[0] == 0xA2
            para_has_data = para_data[0] == 0xA2
            host_still_aa = host_data[0] == 0xAA

            print(f"Host has loaded data: {host_has_data}")
            print(f"Host still has sentinel: {host_still_aa}")
            print(f"Parasite has loaded data: {para_has_data}")

            if host_has_data and not para_has_data:
                pytest.fail(
                    "BUG CONFIRMED: *LOAD wrote to HOST RAM, not parasite. "
                    "The Tube address claim is not redirecting file loads "
                    "through R3/R4 to the parasite."
                )
            elif para_has_data:
                print("Data correctly loaded into parasite memory.")
            elif host_still_aa:
                print("Data went to neither host nor parasite at &1F00!")

    except ServerNotFoundError as e:
        pytest.skip(str(e))
