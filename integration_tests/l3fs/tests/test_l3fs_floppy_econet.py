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

"""Level 3 File Server Econet test using ADFS floppy image.

Uses the same L3FS-KL.adl floppy image that works in BeebEm, removing
SCSI from the equation. This narrows the problem to the Beebium server
side Econet handling with a Tube second processor.

The L3FS-KL.adl image autoboots into a running file server without
needing to answer any questionnaire prompts.

Run with:
    cd integration_tests/l3fs
    uv run pytest -m slow tests/test_l3fs_floppy_econet.py -v -s
"""

from __future__ import annotations

import os
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.ext.econet.aun import Aun
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import dump_screen, lined, linearise, read_mode7_screen


REPO_ROOT = Path(__file__).parent.parent.parent.parent
SCSI_ASSETS_DIRPATH = REPO_ROOT / "tests" / "assets" / "scsi"

# AUN port assignments (convention: 10000 + station number).
SERVER_STATION = 254
SERVER_AUN_PORT = 10254
CLIENT_STATION = 221
CLIENT_AUN_PORT = 10221


def _wait_for_screen_text(bbc, text, timeout_seconds=120.0, poll_interval=0.5):
    """Wait until the given text appears on the MODE 7 screen."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        screen_text = "\n".join(rows)
        if text in screen_text:
            return True
        time.sleep(poll_interval)
    return False


def _wait_for_prompt_after(bbc, command_text, timeout_seconds=30.0, poll_interval=0.5):
    """Wait for a '>' prompt to appear on a line after the given command text."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        found_command = False
        for row in rows:
            stripped = row.strip()
            if command_text in stripped:
                found_command = True
            elif found_command and stripped == ">":
                return True
        time.sleep(poll_interval)
    return False


def _dump(bbc):
    """Return the current screen text for diagnostics."""
    return dump_screen(bbc)


@pytest.fixture(scope="session")
def roms_dirpath():
    env = os.environ.get("BEEBIUM_ROM_DIR")
    if env:
        return Path(env)
    p = REPO_ROOT / "roms"
    if p.is_dir():
        return p
    raise FileNotFoundError("ROM directory not found")


@pytest.fixture(scope="session")
def mos_filepath(roms_dirpath):
    p = roms_dirpath / "acorn-mos_1_20.rom"
    if not p.exists():
        pytest.skip(f"MOS 1.20 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def basic_filepath(roms_dirpath):
    p = roms_dirpath / "bbc-basic_2.rom"
    if not p.exists():
        pytest.skip(f"BASIC 2 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def anfs_filepath(roms_dirpath):
    p = roms_dirpath / "acorn-anfs_4_18.rom"
    if not p.exists():
        pytest.skip(f"ANFS 4.18 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def adfs_filepath(roms_dirpath):
    p = roms_dirpath / "acorn-adfs_1_30.rom"
    if not p.exists():
        pytest.skip(f"ADFS 1.30 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def dfs_filepath(roms_dirpath):
    p = roms_dirpath / "acorn-dfs_2_26.rom"
    if not p.exists():
        pytest.skip(f"DFS 2.26 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def l3fs_floppy_filepath():
    p = SCSI_ASSETS_DIRPATH / "L3FS-KL.adl"
    if not p.exists():
        pytest.skip(f"L3FS-KL.adl not found: {p}")
    return p


@pytest.fixture(scope="session")
def server_filepath():
    import sys
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        REPO_ROOT / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        REPO_ROOT / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        REPO_ROOT / "cmake-build-debug" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c
    pytest.skip("beebium-model-b-romram not found")


@pytest.fixture(scope="session")
def client_server_filepath():
    """Client uses plain beebium-model-b (no ROM/RAM board needed)."""
    import sys
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        REPO_ROOT / "build-release" / "src" / "server" / f"beebium-model-b{exe_suffix}",
        REPO_ROOT / "build" / "src" / "server" / f"beebium-model-b{exe_suffix}",
        REPO_ROOT / "cmake-build-debug" / "src" / "server" / f"beebium-model-b{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c
    pytest.skip("beebium-model-b not found")


@pytest.mark.slow
@pytest.mark.timeout(600)
def test_l3fs_floppy_client_login(
    server_filepath, client_server_filepath,
    mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    l3fs_floppy_filepath,
):
    """Boot L3FS from ADFS floppy, connect a client, and log in as SYST.

    Uses the same L3FS-KL.adl image that works in BeebEm.
    The image autoboots the file server without a questionnaire.
    """

    # ---- File server (station 254) ----
    # Model B with ROM/RAM board, ANFS + ADFS, 1770 FDC, Tube 65C02,
    # RTC (required by L3FS), and the L3FS ADFS floppy.
    # --auto-boot triggers the !BOOT exec on reset.
    server_extra_args = [
        "--sideways", f"9:rom:{anfs_filepath}",
        "--sideways", f"10:rom:{adfs_filepath}",
        "--sideways", f"11:rom:{dfs_filepath}",
        "--fdc", "acorn-1770",
        "--floppy", f"0:{l3fs_floppy_filepath}",
        "--station", str(SERVER_STATION),
        "--aun", f"port={SERVER_AUN_PORT}:map=0.{CLIENT_STATION}@127.0.0.1@{CLIENT_AUN_PORT}",
        "--machine-name", "L3FS",
        "--acorn-rtc", "layout=7bit-year-in-r7",
        "--tube-65c02",
    ]

    # ---- Client (station 221) ----
    client_extra_args = [
        "--sideways", f"9:rom:{anfs_filepath}",
        "--station", str(CLIENT_STATION),
        "--aun", f"port={CLIENT_AUN_PORT}:map=0.{SERVER_STATION}@127.0.0.1@{SERVER_AUN_PORT}",
        "--machine-name", "Station 221",
    ]

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=server_extra_args,
            startup_timeout=30.0,
        ) as server_bbc:
            # Boot to BASIC prompt, select ADFS, then run the file server.
            assert _wait_for_screen_text(server_bbc, ">", timeout_seconds=30), \
                f"Server boot failed:\n{_dump(server_bbc)}"

            # Select ADFS (DFS is default with 1770 FDC), then launch L3FS.
            server_bbc.keyboard.type("*ADFS\r")
            assert _wait_for_screen_text(server_bbc, ">", timeout_seconds=10), \
                f"*ADFS failed:\n{_dump(server_bbc)}"

            server_bbc.keyboard.type('CHAIN "GoFS3-125"\r')

            # Wait for the file server to reach ready state.
            assert _wait_for_screen_text(server_bbc, "Starting - Ready", timeout_seconds=120), \
                f"File server did not reach 'Starting - Ready':\n{_dump(server_bbc)}"
            print(f"\nFile server ready. Screen:\n{_dump(server_bbc)}")

            # Dump server Econet status.
            s_eco = server_bbc.econet.status
            s_aun = server_bbc.transport[Aun].status
            print(f"\nServer Econet: station={s_eco.station_id} "
                  f"aun_mode={s_eco.aun_mode} connected={s_eco.connected} "
                  f"aun_port={s_aun.local_port} peers={s_aun.peer_count}")
            for peer in server_bbc.transport[Aun].peers:
                print(f"  Peer: {peer.net}.{peer.stn} -> {peer.ip_address}:{peer.port}")

            # Launch the client.
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server_filepath=client_server_filepath,
                extra_args=client_extra_args,
                startup_timeout=30.0,
            ) as client_bbc:
                assert _wait_for_screen_text(client_bbc, "Econet Station", timeout_seconds=30), \
                    f"Client did not show Econet station:\n{_dump(client_bbc)}"
                assert _wait_for_screen_text(client_bbc, ">", timeout_seconds=10), \
                    f"Client did not reach prompt:\n{_dump(client_bbc)}"
                print(f"\nClient booted. Screen:\n{_dump(client_bbc)}")

                # Check server tick rate with both processes running.
                import time as _time
                s1 = server_bbc.econet.status.tick_count
                _time.sleep(5)
                s2 = server_bbc.econet.status.tick_count
                rate = (s2 - s1) / 5.0
                print(f"\nServer tick rate with client running: {rate:.0f} ticks/sec ({rate/2e6*100:.1f}% of 2MHz)")

                # Record state before the command.
                # Skip *NET -- ANFS in slot 9 should already be active.
                wall_clock_before = time.monotonic()
                server_cycles_before = server_bbc.debugger.get_state().cycle_count
                client_cycles_before = client_bbc.debugger.get_state().cycle_count
                server_status_before = server_bbc.econet.status
                client_status_before = client_bbc.econet.status
                print(f"\nBefore *I AM SYST:")
                print(f"  Server: ticks={server_status_before.tick_count} "
                      f"cr1_82={server_status_before.cr1_0x82_write_count} "
                      f"handshake={server_status_before.handshake.stage if server_status_before.handshake else 'N/A'}")
                print(f"  Client: ticks={client_status_before.tick_count} "
                      f"cr1_82={client_status_before.cr1_0x82_write_count} "
                      f"handshake={client_status_before.handshake.stage if client_status_before.handshake else 'N/A'}")

                client_bbc.keyboard.type("*I AM SYST\r")

                # Wait for the command to complete. The NFS ROM's reply
                # timeout is ~22 seconds at 2MHz. The file server needs
                # ~2 seconds of emulated time to process the request.
                # Give it plenty of time.
                login_ok = _wait_for_prompt_after(client_bbc, "*I AM SYST", timeout_seconds=60)

                # Record state after the command.
                wall_clock_after = time.monotonic()
                server_status_after = server_bbc.econet.status
                client_status_after = client_bbc.econet.status
                wall_clock_elapsed = wall_clock_after - wall_clock_before
                print(f"\nAfter *I AM SYST:")
                print(f"  Server: ticks={server_status_after.tick_count} "
                      f"cr1_82={server_status_after.cr1_0x82_write_count} "
                      f"handshake={server_status_after.handshake.stage if server_status_after.handshake else 'N/A'} "
                      f"adlc_cr1=0x{server_status_after.adlc.cr1:02X}" if server_status_after.adlc else "")
                print(f"  Client: ticks={client_status_after.tick_count} "
                      f"cr1_82={client_status_after.cr1_0x82_write_count} "
                      f"handshake={client_status_after.handshake.stage if client_status_after.handshake else 'N/A'} "
                      f"adlc_cr1=0x{client_status_after.adlc.cr1:02X}" if client_status_after.adlc else "")
                def _delta(after, before, field):
                    return getattr(after, field) - getattr(before, field)

                print(f"\nDelta:")
                print(f"  Server Econet ticks: {_delta(server_status_after, server_status_before, 'tick_count')}")
                print(f"  Client Econet ticks: {_delta(client_status_after, client_status_before, 'tick_count')}")
                print(f"  Server cr1_82 writes: {_delta(server_status_after, server_status_before, 'cr1_0x82_write_count')}")
                print(f"  Client cr1_82 writes: {_delta(client_status_after, client_status_before, 'cr1_0x82_write_count')}")
                print(f"  Server rx_frames_received: {_delta(server_status_after, server_status_before, 'rx_frames_received_count')}")
                print(f"  Client rx_frames_received: {_delta(client_status_after, client_status_before, 'rx_frames_received_count')}")
                print(f"  Server rx_blocked_by_reset: {_delta(server_status_after, server_status_before, 'rx_blocked_by_reset_count')}")
                print(f"  Client rx_blocked_by_reset: {_delta(client_status_after, client_status_before, 'rx_blocked_by_reset_count')}")
                print(f"  Server scout_ack_generated: {_delta(server_status_after, server_status_before, 'scout_ack_generated_count')}")
                print(f"  Server tx_frames_from_beeb: {_delta(server_status_after, server_status_before, 'tx_frames_from_beeb_count')}")
                print(f"  Server unexpected_tx_reset: {_delta(server_status_after, server_status_before, 'unexpected_tx_reset_count')}")
                print(f"  Client scout_ack_generated: {_delta(client_status_after, client_status_before, 'scout_ack_generated_count')}")
                print(f"  Client tx_frames_from_beeb: {_delta(client_status_after, client_status_before, 'tx_frames_from_beeb_count')}")
                print(f"  Client unexpected_tx_reset: {_delta(client_status_after, client_status_before, 'unexpected_tx_reset_count')}")
                print(f"  Server tx_from_idle: {_delta(server_status_after, server_status_before, 'tx_from_idle_count')}")
                print(f"  Server max_handshake_timer: {server_status_after.max_handshake_timer_seen}")
                print(f"  Client tx_from_idle: {_delta(client_status_after, client_status_before, 'tx_from_idle_count')}")
                print(f"  Client max_handshake_timer: {client_status_after.max_handshake_timer_seen}")
                print(f"  Server watchdog_timeouts: {_delta(server_status_after, server_status_before, 'watchdog_timeout_count')}")
                print(f"  Client watchdog_timeouts: {_delta(client_status_after, client_status_before, 'watchdog_timeout_count')}")
                print(f"  Server send_stage_log: {server_status_after.send_stage_log}")
                print(f"  Client send_stage_log: {client_status_after.send_stage_log}")
                print(f"  Server ticks_with_timer_active: {server_status_after.ticks_with_timer_active}")
                print(f"  Client ticks_with_timer_active: {client_status_after.ticks_with_timer_active}")
                print(f"  Server read_stretch_parasite_ticks: {server_status_after.read_stretch_parasite_ticks}")
                print(f"  Client read_stretch_parasite_ticks: {client_status_after.read_stretch_parasite_ticks}")

                server_tick_delta = _delta(server_status_after, server_status_before, 'tick_count')
                client_tick_delta = _delta(client_status_after, client_status_before, 'tick_count')
                # Get CPU cycle counts -- these count ALL host cycles including stretch
                server_cycles_after = server_bbc.debugger.get_state().cycle_count
                client_cycles_after = client_bbc.debugger.get_state().cycle_count

                server_cycle_delta = server_cycles_after - server_cycles_before
                client_cycle_delta = client_cycles_after - client_cycles_before

                print(f"\nWall-clock analysis:")
                print(f"  Wall-clock elapsed: {wall_clock_elapsed:.2f} seconds")
                print(f"  Server cycle delta: {server_cycle_delta} ({server_cycle_delta / wall_clock_elapsed / 1e6:.2f} MHz)")
                print(f"  Client cycle delta: {client_cycle_delta} ({client_cycle_delta / wall_clock_elapsed / 1e6:.2f} MHz)")
                print(f"  Server ticks/cycle ratio: {server_tick_delta / server_cycle_delta:.4f} (expect ~0.5)")
                print(f"  Client ticks/cycle ratio: {client_tick_delta / client_cycle_delta:.4f} (expect ~0.5)")
                print(f"  Server tick rate: {server_tick_delta / wall_clock_elapsed:.0f} ticks/sec "
                      f"({server_tick_delta / wall_clock_elapsed / 1e6 * 100:.1f}% of 1M/sec nominal)")
                print(f"  Client tick rate: {client_tick_delta / wall_clock_elapsed:.0f} ticks/sec "
                      f"({client_tick_delta / wall_clock_elapsed / 1e6 * 100:.1f}% of 1M/sec nominal)")
                print(f"  Server emulated time: {server_tick_delta / 1e6:.3f} sec")
                print(f"  Client emulated time: {client_tick_delta / 1e6:.3f} sec")

                print(f"\nClient after *I AM SYST:\n{_dump(client_bbc)}")
                print(f"\nServer after client login:\n{_dump(server_bbc)}")

                # Read stderr for AUN trace BEFORE any assertions that might
                # terminate the test.

                client_screen = linearise(read_mode7_screen(client_bbc), lined)
                if "No reply" in client_screen:
                    pytest.fail(
                        f"Client received 'No reply'.\n"
                        f"Client screen:\n{_dump(client_bbc)}\n"
                        f"Server screen:\n{_dump(server_bbc)}"
                    )

                assert login_ok, (
                    f"*I AM SYST did not return to prompt.\n"
                    f"Client screen:\n{_dump(client_bbc)}\n"
                    f"Server screen:\n{_dump(server_bbc)}"
                )

    except ServerNotFoundError as e:
        pytest.skip(str(e))
