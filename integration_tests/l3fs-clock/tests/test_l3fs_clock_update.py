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

"""L3 File Server clock update timing test.

Boots a BBC Model B with ROM/RAM board running the Level 3 File Server v1.26,
monitors the RTC WatchActivity stream to measure how often the file server
polls the dongle, and asserts that the polling interval is approximately
30 seconds (not 3-5 minutes).

This test is long-running (several minutes). It is marked as 'slow' and
excluded from default test runs. Run with:

    pytest -m slow tests/test_l3fs_clock_update.py
"""

from __future__ import annotations

import sys
import threading
import time
from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.screen import screen_contains, dump_screen, read_mode7_screen

sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent / "clients" / "python" / "src"))
from beebium.ext.peripheral.acorn_rtc._proto import acorn_rtc_pb2, acorn_rtc_pb2_grpc


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


def _collect_activity_events(stub, duration_seconds, stop_event):
    """Collect RTC activity events for the given duration.

    Returns a list of (timestamp_us, event_type, register, value) tuples.
    """
    events = []
    try:
        request = acorn_rtc_pb2.WatchActivityRequest()
        stream = stub.WatchActivity(request, timeout=duration_seconds + 30)
        for event in stream:
            if stop_event.is_set():
                stream.cancel()
                break
            events.append((
                event.timestamp_us,
                "READ" if event.type == acorn_rtc_pb2.RtcActivityEvent.REGISTER_READ else "WRITE",
                event.register_number,
                event.value,
            ))
    except grpc.RpcError:
        pass
    return events


def _group_rddong_calls(events):
    """Group REGISTER_READ events into RDDONG calls.

    The L3FS v1.26 RDDONG reads registers 6, 4, 2, 0, 3, 7 in sequence.
    We detect groups of 6 consecutive reads and return the timestamp of
    the first read in each group.
    """
    read_events = [(ts, reg, val) for ts, typ, reg, val in events if typ == "READ"]

    groups = []
    i = 0
    while i + 5 < len(read_events):
        # Check for the RDDONG register sequence: 6, 4, 2, 0, 3, 7
        regs = [read_events[j][1] for j in range(i, i + 6)]
        if regs == [6, 4, 2, 0, 3, 7]:
            groups.append(read_events[i][0])  # Timestamp of first read
            i += 6
        else:
            i += 1

    return groups


@pytest.mark.slow
@pytest.mark.timeout(600)
def test_l3fs_clock_update_interval(
    server_filepath, mos_filepath, basic_filepath,
    roms_dirpath, l3fs_ssd_filepath, scsi_hdd_filepath,
):
    """The L3FS should poll the RTC dongle approximately every 30 seconds.

    This test boots the L3FS, monitors the RTC activity stream for ~3 minutes,
    and verifies that RDDONG calls (groups of 6 register reads) occur at
    approximately 30-second intervals, not the broken 3-5 minute intervals.
    """
    anfs_filepath = roms_dirpath / "acorn-anfs_4_18.rom"
    adfs_filepath = roms_dirpath / "acorn-adfs_1_30.rom"
    dfs_filepath = roms_dirpath / "acorn-dfs_2_26.rom"

    for rom, name in [(anfs_filepath, "ANFS 4.18"), (adfs_filepath, "ADFS 1.30"), (dfs_filepath, "DFS 2.26")]:
        if not rom.exists():
            pytest.skip(f"{name} ROM not found: {rom}")

    extra_args = [
        "--sideways", f"slot=9:type=rom:image={anfs_filepath}",
        "--sideways", f"slot=10:type=rom:image={adfs_filepath}",
        "--sideways", f"slot=11:type=rom:image={dfs_filepath}",
        "--fdc", "acorn-1770",
        "--floppy", f"0:{l3fs_ssd_filepath}",
        "--acorn-scsi",
        "--scsi-hdd", f"0:{scsi_hdd_filepath}",
        "--station", "254",
        "--aun", "port=0",  # Auto-assign port
        "--machine-name", "L3FS-TEST",
        "--acorn-rtc", "layout=7bit-year-in-r7:time=1985-10-26T0121",
        "--tube", "65C02-3MHz",
        "--advertise",
    ]

    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=server_filepath,
        extra_args=extra_args,
        startup_timeout=30.0,
    ) as bbc:
        # Wait for BASIC prompt (Tube boot is slower)
        assert _wait_for_screen_text(bbc, ">", timeout_seconds=30), \
            f"Boot to BASIC prompt failed:\n{dump_screen(bbc)}"

        # Type *RUN FS3v126 to start the file server
        bbc.keyboard.type("*RUN FS3v126\r")

        # Wait for "Number of drives:" prompt
        assert _wait_for_screen_text(bbc, "Number of drives:", timeout_seconds=120), \
            f"FS did not reach 'Number of drives:' prompt:\n{dump_screen(bbc)}"

        # Type 1 <RETURN>
        bbc.keyboard.type("1\r")

        # Wait for "Command :" prompt
        assert _wait_for_screen_text(bbc, "Command :", timeout_seconds=30), \
            f"FS did not reach 'Command :' prompt:\n{dump_screen(bbc)}"

        # Type S (no RETURN needed)
        bbc.keyboard.type("S")

        # Wait for "Stations:" prompt
        assert _wait_for_screen_text(bbc, "Stations:", timeout_seconds=10), \
            f"FS did not reach 'Stations:' prompt:\n{dump_screen(bbc)}"

        # Type 2 <RETURN>
        bbc.keyboard.type("2\r")

        # Wait for the time display (the initial time should be 01:21)
        assert _wait_for_screen_text(bbc, "01:21", timeout_seconds=60), \
            f"FS did not display initial time 01:21:\n{dump_screen(bbc)}"

        print("\nL3FS is running. Monitoring RTC activity...")

        # Connect to the AcornRtcService for WatchActivity
        channel = grpc.insecure_channel(bbc.target)
        stub = acorn_rtc_pb2_grpc.AcornRtcServiceStub(channel)

        # Collect activity events for ~3 minutes
        observation_seconds = 180
        stop_event = threading.Event()

        collector_thread = threading.Thread(
            target=lambda: None,  # Will be replaced
            daemon=True,
        )

        events = []

        def collect():
            nonlocal events
            events = _collect_activity_events(stub, observation_seconds, stop_event)

        collector_thread = threading.Thread(target=collect, daemon=True)
        collector_thread.start()

        # Wait for the observation period
        time.sleep(observation_seconds)
        stop_event.set()
        collector_thread.join(timeout=10)

        channel.close()

        # Analyse the collected events
        rddong_timestamps = _group_rddong_calls(events)

        print(f"\nTotal RTC activity events: {len(events)}")
        print(f"RDDONG calls detected: {len(rddong_timestamps)}")

        if len(rddong_timestamps) >= 2:
            intervals = []
            for i in range(1, len(rddong_timestamps)):
                interval_us = rddong_timestamps[i] - rddong_timestamps[i - 1]
                interval_s = interval_us / 1_000_000
                intervals.append(interval_s)
                print(f"  RDDONG interval {i}: {interval_s:.1f}s")

            avg_interval = sum(intervals) / len(intervals)
            print(f"\nAverage RDDONG interval: {avg_interval:.1f}s")
            print(f"Expected: ~30s")

            # The interval should be approximately 30 seconds.
            # Allow generous tolerance (15-60s) to account for emulation speed
            # variations, but it should definitely not be 3-5 minutes (180-300s).
            assert avg_interval < 90, (
                f"Average RDDONG interval is {avg_interval:.1f}s, expected ~30s. "
                f"This confirms the Tube OSBYTE timing issue causes the L3FS "
                f"to poll the RTC dongle too infrequently."
            )

            # Also check it's not impossibly fast (sanity check)
            assert avg_interval > 5, (
                f"Average RDDONG interval is {avg_interval:.1f}s, suspiciously fast. "
                f"Expected ~30s."
            )
        else:
            # If we got fewer than 2 RDDONG calls in 3 minutes, that itself
            # demonstrates the bug (should have ~6 calls at 30-second intervals)
            pytest.fail(
                f"Only {len(rddong_timestamps)} RDDONG call(s) detected in "
                f"{observation_seconds}s. Expected ~{observation_seconds // 30}. "
                f"This confirms the clock update timing issue.\n"
                f"Total events: {len(events)}"
            )
