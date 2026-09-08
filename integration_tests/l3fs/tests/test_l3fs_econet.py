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

"""Level 3 File Server Econet integration tests.

Launches two Beebium instances connected via AUN loopback:
- Station 254: File server (Model B + Tube 65C02 + ADFS + SCSI + RTC)
- Station 221: Client (Model B + ANFS only)

The file server boots from a pre-built SCSI image containing FS3v126 on
the ADFS partition and an AFS0 data partition. The client connects over
Econet and exercises NFS operations.

Run with:
    cd integration_tests/l3fs
    uv run pytest -m slow tests/test_l3fs_econet.py -v -s
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.ext.econet.aun import Aun
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import dump_screen, lined, linearise, read_mode7_screen


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


def _start_file_server(bbc):
    """Navigate the L3FS startup questionnaire.

    Expects the machine to have booted with ADFS as the default filing
    system (Tube boot selects ADFS). Runs the file server and answers
    the prompts:
        Number of drives: 1
        Command: S (Startup)
        Stations: 2

    Waits for the "Starting - Ready" message.
    """
    # Run the file server from the SCSI hard disc.
    bbc.keyboard.type("*RUN $.FS3v126\r")

    # Number of drives: 1
    assert _wait_for_screen_text(bbc, "Number of drives:", timeout_seconds=120), \
        f"No 'Number of drives:' prompt:\n{_dump(bbc)}"
    bbc.keyboard.type("1\r")

    # Command: S (Startup, no RETURN needed)
    assert _wait_for_screen_text(bbc, "Command :", timeout_seconds=30), \
        f"No 'Command :' prompt:\n{_dump(bbc)}"
    bbc.keyboard.type("S")

    # Stations: 2
    assert _wait_for_screen_text(bbc, "Stations:", timeout_seconds=10), \
        f"No 'Stations:' prompt:\n{_dump(bbc)}"
    bbc.keyboard.type("2\r")

    # Wait for the file server to be ready.
    assert _wait_for_screen_text(bbc, "Starting - Ready", timeout_seconds=120), \
        f"File server did not reach 'Starting - Ready':\n{_dump(bbc)}"


@pytest.mark.slow
@pytest.mark.timeout(300)
def test_l3fs_client_login(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath,
    l3fs_scsi_filepath,
):
    """Start the L3FS, connect a client, and log in as SYST."""

    # ---- Launch the file server (station 254) ----
    server_extra_args = [
        "--sideways", f"9:rom:{anfs_filepath}",
        "--sideways", f"10:rom:{adfs_filepath}",
        "--fdc", "acorn-1770",
        "--station", str(SERVER_STATION),
        "--aun", f"port={SERVER_AUN_PORT}:map=0.{CLIENT_STATION}@127.0.0.1@{CLIENT_AUN_PORT}",
        "--machine-name", "L3FS",
        "--acorn-rtc", "layout=7bit-year-in-r7",
        "--acorn-scsi",
        "--scsi-hdd", f"0:{l3fs_scsi_filepath}",
        "--tube-65c02",
    ]

    # ---- Launch the client (station 221) ----
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
            # Boot the file server to BASIC prompt.
            assert _wait_for_screen_text(server_bbc, ">", timeout_seconds=30), \
                f"Server boot failed:\n{_dump(server_bbc)}"

            # Start the file server and navigate the questionnaire.
            _start_file_server(server_bbc)
            print(f"\nFile server ready. Screen:\n{_dump(server_bbc)}")

            # Dump Econet diagnostics for the server.
            server_econet = server_bbc.econet.status
            server_aun = server_bbc.transport[Aun].status
            print(f"\nServer Econet: station={server_econet.station_id} "
                  f"aun_mode={server_econet.aun_mode} "
                  f"connected={server_econet.connected} "
                  f"aun_port={server_aun.local_port} "
                  f"peers={server_aun.peer_count}")
            if server_econet.handshake:
                print(f"  Handshake: stage={server_econet.handshake.stage} "
                      f"flag_fill={server_econet.handshake.flag_fill_active}")
            for peer in server_bbc.transport[Aun].peers:
                print(f"  Peer: {peer.net}.{peer.stn} -> {peer.ip_address}:{peer.port}")

            # Now launch the client station.
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server_filepath=server_filepath,
                extra_args=client_extra_args,
                startup_timeout=30.0,
            ) as client_bbc:
                # Client should boot showing Econet station number.
                assert _wait_for_screen_text(client_bbc, "Econet Station", timeout_seconds=30), \
                    f"Client did not show Econet station message:\n{_dump(client_bbc)}"
                assert _wait_for_screen_text(client_bbc, ">", timeout_seconds=10), \
                    f"Client did not reach BASIC prompt:\n{_dump(client_bbc)}"

                print(f"\nClient booted. Screen:\n{_dump(client_bbc)}")

                # Dump Econet diagnostics for the client.
                client_econet = client_bbc.econet.status
                client_aun = client_bbc.transport[Aun].status
                print(f"\nClient Econet: station={client_econet.station_id} "
                      f"aun_mode={client_econet.aun_mode} "
                      f"connected={client_econet.connected} "
                      f"aun_port={client_aun.local_port} "
                      f"peers={client_aun.peer_count}")
                if client_econet.handshake:
                    print(f"  Handshake: stage={client_econet.handshake.stage} "
                          f"flag_fill={client_econet.handshake.flag_fill_active}")
                for peer in client_bbc.transport[Aun].peers:
                    print(f"  Peer: {peer.net}.{peer.stn} -> {peer.ip_address}:{peer.port}")

                # Select NFS as the active filing system, then log in.
                client_bbc.keyboard.type("*NET\r")
                assert _wait_for_prompt_after(client_bbc, "*NET", timeout_seconds=10), \
                    f"*NET did not return to prompt:\n{_dump(client_bbc)}"

                client_bbc.keyboard.type("*I AM SYST\r")

                # Wait for the command echo to appear on screen first,
                # confirming the keyboard input was processed.
                assert _wait_for_screen_text(client_bbc, "*I AM SYST", timeout_seconds=10), \
                    f"*I AM SYST not echoed on screen:\n{_dump(client_bbc)}"

                # Poll ADLC and handshake state on BOTH sides during the
                # transaction to understand where the packet flow breaks.
                import time as _time
                poll_deadline = _time.monotonic() + 5.0
                client_hs_states = set()
                server_hs_states = set()
                client_adlc_samples = []
                server_adlc_samples = []

                def _sample_adlc(st):
                    if not st.adlc:
                        return None
                    a = st.adlc
                    return (f"CR1={a.cr1:02X} CR2={a.cr2:02X} "
                            f"SR1={a.sr1:02X} SR2={a.sr2:02X} "
                            f"TXe={a.tx_fifo_empty} RXe={a.rx_fifo_empty} "
                            f"CTS={a.cts_input} IRQ={a.irq_output} "
                            f"TX={a.tx_frame_field} RX={a.rx_frame_field}")

                while _time.monotonic() < poll_deadline:
                    try:
                        cst = client_bbc.econet.status
                        if cst.handshake:
                            client_hs_states.add(cst.handshake.stage)
                        s = _sample_adlc(cst)
                        if s and (not client_adlc_samples or client_adlc_samples[-1] != s):
                            client_adlc_samples.append(s)
                    except Exception:
                        pass
                    try:
                        sst = server_bbc.econet.status
                        if sst.handshake:
                            server_hs_states.add(sst.handshake.stage)
                        s = _sample_adlc(sst)
                        if s and (not server_adlc_samples or server_adlc_samples[-1] != s):
                            server_adlc_samples.append(s)
                    except Exception:
                        pass
                    _time.sleep(0.02)

                print(f"\nClient handshake states: {client_hs_states}")
                print(f"Client ADLC samples ({len(client_adlc_samples)}):")
                for s in client_adlc_samples[:10]:
                    print(f"  {s}")
                print(f"\nServer handshake states: {server_hs_states}")
                print(f"Server ADLC samples ({len(server_adlc_samples)}):")
                for s in server_adlc_samples[:10]:
                    print(f"  {s}")

                # Now wait for either a successful login (fresh prompt
                # appears below the command) or an error message.
                # Give it a generous timeout since the first Econet
                # transaction can be slow.
                login_ok = _wait_for_prompt_after(client_bbc, "*I AM SYST", timeout_seconds=30)

                print(f"\nClient after *I AM SYST:\n{_dump(client_bbc)}")
                print(f"\nServer after client login:\n{_dump(server_bbc)}")

                # Check for common error conditions.
                client_screen = linearise(read_mode7_screen(client_bbc), lined)
                if "No reply" in client_screen:
                    pytest.fail(
                        f"Client received 'No reply' -- file server not "
                        f"reachable via AUN.\n"
                        f"Client screen:\n{_dump(client_bbc)}\n"
                        f"Server screen:\n{_dump(server_bbc)}"
                    )
                if "Line Jammed" in client_screen:
                    pytest.fail(
                        f"Client received 'Line Jammed' -- Econet hardware "
                        f"issue.\nClient screen:\n{_dump(client_bbc)}"
                    )
                if "Not listening" in client_screen:
                    pytest.fail(
                        f"Client received 'Not listening' -- file server "
                        f"station not responding.\n"
                        f"Client screen:\n{_dump(client_bbc)}\n"
                        f"Server screen:\n{_dump(server_bbc)}"
                    )
                if "Net Error" in client_screen:
                    pytest.fail(
                        f"Client received 'Net Error'.\n"
                        f"Client screen:\n{_dump(client_bbc)}\n"
                        f"Server screen:\n{_dump(server_bbc)}"
                    )

                assert login_ok, (
                    f"*I AM SYST did not return to prompt within timeout.\n"
                    f"Client screen:\n{_dump(client_bbc)}\n"
                    f"Server screen:\n{_dump(server_bbc)}"
                )

    except ServerNotFoundError as e:
        pytest.skip(str(e))
