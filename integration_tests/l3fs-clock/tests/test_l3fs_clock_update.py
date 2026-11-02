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

import threading
import time

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.screen import screen_contains, dump_screen, read_mode7_screen
from beebium.ext.peripheral.acorn_rtc import AcornRtc, RtcEventType

# The server's Level 3 file server preset: ANFS, ADFS, Acorn SCSI with the
# bundled l3fs-v1_26b hard disc, the RTC, a 65C02 coprocessor, station 254.
L3FS_PRESET = "model-b-l3fs-aun"


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
def test_l3fs_clock_update_interval(server_filepath, roms_dirpath):
    """The L3FS should poll the RTC dongle approximately every 30 seconds.

    This test boots the L3FS from the server's Level 3 file server preset,
    whose bundled hard disc (l3fs-v1_26b, a fresh copy per launch) starts
    the file server with no keystrokes. It monitors the RTC activity stream
    for ~3 minutes, and verifies that RDDONG calls (groups of 6 register
    reads) occur at approximately 30-second intervals, not the broken 3-5
    minute intervals.
    """
    for rom_filename in ("acorn-anfs_4_18.rom", "acorn-adfs_1_30.rom"):
        if not (roms_dirpath / rom_filename).exists():
            pytest.skip(f"ROM not found: {roms_dirpath / rom_filename}")

    with Beebium.launch(
        server=server_filepath,
        preset=L3FS_PRESET,
        startup_timeout=30.0,
    ) as bbc:
        # The preset auto-boots the disc, whose !BOOT starts the file server
        # and answers its questions; it prints this once it serves.
        assert _wait_for_screen_text(bbc, "Starting - Ready", timeout_seconds=180), \
            f"The file server did not start:\n{dump_screen(bbc)}"

        print("\nL3FS is running. Monitoring RTC activity...")

        # The RTC's activity stream, through the client's adapter, which
        # reaches the extension's service over ExtensionRpc. The stream blocks
        # between the server's reads of the clock, so events are kept as they
        # arrive and taken after the observation period.
        rtc = bbc.extensions[AcornRtc]
        observation_seconds = 180
        events: list[tuple[int, str, int, int]] = []

        def collect():
            try:
                for event in rtc.watch_activity():
                    events.append((
                        event.timestamp_us,
                        "READ" if event.type == RtcEventType.REGISTER_READ else "WRITE",
                        event.register_number,
                        event.value,
                    ))
            except grpc.RpcError:
                pass  # the stream ends when the server shuts down

        threading.Thread(target=collect, daemon=True).start()
        time.sleep(observation_seconds)
        events = list(events)

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
