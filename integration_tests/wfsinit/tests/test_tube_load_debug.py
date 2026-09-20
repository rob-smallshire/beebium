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

# Focused Tube *LOAD debugging: examine what DFS and the Tube Host Code
# actually do when loading a file with Tube active.

from __future__ import annotations
from pathlib import Path
import pytest
from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen, read_mode7_screen
ASM_DIRPATH = Path(__file__).parent.parent / "asm"

def run_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=1.0):
    return bbc.run_until_or_timeout(
        predicate, emulated_seconds, chunk_seconds=chunk_seconds)


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_tube_load_detailed_debug(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath,
):
    """Detailed examination of *LOAD with Tube active."""
    ssd_filepath = ASM_DIRPATH / "test_hello_proper.ssd"
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
                "--sideways", f"slot=14:type=rom:image={dfs_filepath}",
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

            # First: *CAT to see what DFS thinks is on the disc
            bbc.keyboard.type("*CAT\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=10.0,
            )
            print(f"After *CAT:\n{dump_screen(bbc)}")

            # Fill host RAM &0000-&2FFF with sentinel pattern
            # so we can find where data lands
            for addr in range(0x1E00, 0x2100):
                bbc.memory.address.bus[addr] = 0xAA

            # Also fill some parasite memory
            parasite = bbc.connect_coprocessor()
            for addr in range(0x0800, 0x2100):
                parasite.memory.address.bus[addr] = 0xBB

            # Check file info
            bbc.keyboard.type("*INFO TEST\r")
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=10.0,
            )
            print(f"After *INFO:\n{dump_screen(bbc)}")

            # Now do *LOAD with explicit address.
            # Wait for the command to complete by checking for a ">" prompt
            # on a line AFTER "LOAD" appears on screen.
            def _load_complete():
                rows = read_mode7_screen(bbc)
                found_load = False
                for row in rows:
                    if "LOAD TEST" in row:
                        found_load = True
                    elif found_load and row.strip() == ">":
                        return True
                return False

            bbc.keyboard.type("*LOAD TEST 1F00\r")
            ok = run_until_or_timeout(
                bbc,
                _load_complete,
                emulated_seconds=30.0,
            )
            screen = dump_screen(bbc)
            print(f"After *LOAD:\n{screen}")

            # Check if there was an error
            rows = read_mode7_screen(bbc)
            screen_text = "\n".join(rows)
            if "fault" in screen_text.lower() or "error" in screen_text.lower():
                print("ERROR detected on screen!")

            # Scan host RAM for the file signature (A2 00 = LDX #0)
            print("\nHost RAM scan for A2 00 (LDX #0):")
            for page in range(0x00, 0x80):
                addr = page * 256
                b0 = bbc.memory.address.peek[addr]
                b1 = bbc.memory.address.peek[addr + 1]
                if b0 == 0xA2 and b1 == 0x00:
                    # Check more bytes to confirm
                    chunk = bytes(bbc.memory.address.peek[addr:addr+8])
                    print(f"  ${addr:04X}: {chunk.hex()}")

            # Check specific host locations
            print("\nHost RAM at $1F00:")
            print(f"  {bytes(bbc.memory.address.peek[0x1F00:0x1F10]).hex()}")
            print("Host RAM at $0800 (PAGE without Tube):")
            print(f"  {bytes(bbc.memory.address.peek[0x0800:0x0810]).hex()}")
            print("Host RAM at $0E00 (PAGE with DFS):")
            print(f"  {bytes(bbc.memory.address.peek[0x0E00:0x0E10]).hex()}")

            # Check parasite memory
            print("\nParasite RAM at $1F00:")
            print(f"  {bytes(parasite.memory.address.peek[0x1F00:0x1F10]).hex()}")
            print("Parasite RAM at $0800 (Tube PAGE):")
            print(f"  {bytes(parasite.memory.address.peek[0x0800:0x0810]).hex()}")

            # Check Tube Host Code workspace
            print("\nTube Host Code state:")
            print(f"  tube_claim_flag ($14): ${bbc.memory.address.peek[0x14]:02X}")
            print(f"  tube_claimed_id ($15): ${bbc.memory.address.peek[0x15]:02X}")
            print(f"  tube_data_ptr ($12-13): ${bbc.memory.address.peek[0x13]:02X}{bbc.memory.address.peek[0x12]:02X}")

            # Check Tube ULA state
            tube_state = bbc.tube_ula.state
            print(f"\nTube ULA:\n{tube_state}")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_exec_boot_with_tube(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath,
):
    """Test *EXEC !BOOT manually with Tube — this is what auto-boot does."""
    ssd_filepath = ASM_DIRPATH / "test_hello_proper.ssd"
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
                "--sideways", f"slot=14:type=rom:image={dfs_filepath}",
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

            # Type *EXEC !BOOT — this is what OPT 3 auto-boot does
            def _after_exec():
                rows = read_mode7_screen(bbc)
                text = "\n".join(rows)
                return "DONE" in text or "fault" in text.lower() or "error" in text.lower()

            bbc.keyboard.type('*EXEC !BOOT\r')
            ok = run_until_or_timeout(
                bbc,
                _after_exec,
                emulated_seconds=60.0,
            )

            screen = dump_screen(bbc)
            print(f"After *EXEC !BOOT:\n{screen}")

            rows = read_mode7_screen(bbc)
            text = "\n".join(rows)
            if "DONE" in text:
                print("SUCCESS: Program ran via *EXEC !BOOT")
            elif "HELLO" in text:
                print("Partial: HELLO printed but DONE not seen")
            else:
                print("FAILED or hung")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_run_from_keyboard_with_tube(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath,
):
    """Test *RUN TEST from keyboard with Tube — should load and execute."""
    ssd_filepath = ASM_DIRPATH / "test_hello_proper.ssd"
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
                "--sideways", f"slot=14:type=rom:image={dfs_filepath}",
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

            def _run_complete():
                rows = read_mode7_screen(bbc)
                text = "\n".join(rows)
                return "DONE" in text

            bbc.keyboard.type('*RUN TEST\r')
            ok = run_until_or_timeout(
                bbc,
                _run_complete,
                emulated_seconds=30.0,
            )

            screen = dump_screen(bbc)
            print(f"Screen:\n{screen}")

            if ok:
                print("SUCCESS: *RUN TEST completed")
            else:
                print("FAILED: *RUN TEST did not produce DONE")
                pytest.fail("*RUN TEST failed with Tube. See screen output.")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_auto_boot_with_tube(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath,
):
    """Shift-Break auto-boot with Tube active.

    Same minimal config as test_exec_boot_with_tube, but driven via
    --auto-boot rather than a typed *EXEC !BOOT. The SSD's !BOOT chain
    must reach "HELLO DONE" without manual keyboard input. Regression
    cover for issue #23.
    """
    ssd_filepath = ASM_DIRPATH / "test_hello_proper.ssd"
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
                "--sideways", f"slot=14:type=rom:image={dfs_filepath}",
                "--floppy", f"0:{ssd_filepath}",
                "--auto-boot",
            ],
            startup_timeout=30.0,
        ) as bbc:
            def _boot_complete():
                rows = read_mode7_screen(bbc)
                text = "\n".join(rows)
                return "DONE" in text or "fault" in text.lower()

            ok = run_until_or_timeout(
                bbc,
                _boot_complete,
                emulated_seconds=30.0,
            )

            screen = dump_screen(bbc)
            print(f"Screen:\n{screen}")

            rows = read_mode7_screen(bbc)
            text = "\n".join(rows)
            if "fault" in text.lower():
                pytest.fail(
                    "Auto-boot produced a Disc fault with Tube active. "
                    "See screen output."
                )
            if not ok:
                pytest.fail(
                    "Auto-boot did not reach DONE within timeout with Tube "
                    "active. See screen output."
                )
            assert "HELLO" in text

    except ServerNotFoundError as e:
        pytest.skip(str(e))
