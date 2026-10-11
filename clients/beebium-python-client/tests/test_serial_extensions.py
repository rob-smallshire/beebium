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

"""Integration tests for the serial device extensions over gRPC.

Each launches a real server with a serial extension owning the serial port and
drives the BBC's ACIA/Serial ULA through the bus, exactly as a ROM would.
"""

from __future__ import annotations

import os
import sys

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.ext.peripheral.host_serial import HostSerial
from beebium.ext.peripheral.rpc_serial import RpcSerial

# host-serial pty/device modes rely on POSIX pseudo-terminals.
_posix_only = pytest.mark.skipif(sys.platform.startswith("win"), reason="host-serial pty/device is POSIX-only")

# MC6850 control: /16 divide, 8N1, /RTS low, no TX IRQ -- the MOS serial config.
_ACIA_8N1 = 0x15
# Serial ULA: RS423 select (carrier present), 19200 baud on TX and RX.
_ULA_RS423_19200 = 0x40


def _program_serial(bbc: Beebium) -> None:
    """Poke the ACIA + Serial ULA into the standard 19200 8N1 configuration."""
    bbc.memory.address.bus[0xFE08] = _ACIA_8N1
    bbc.memory.address.bus[0xFE10] = _ULA_RS423_19200


@pytest.fixture
def rpc_serial_bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    """A BBC whose serial port is owned by the rpc-serial extension."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=["--rpc-serial"],
        ) as instance:
            instance.debugger.stop()
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture
def loopback_serial_bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    """A BBC whose serial port is owned by the loopback-serial extension."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=["--loopback-serial"],
        ) as instance:
            instance.debugger.stop()
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_rpc_serial_transmit_round_trip(rpc_serial_bbc):
    """A byte the BBC transmits is collected by the rpc-serial client."""
    bbc = rpc_serial_bbc
    _program_serial(bbc)

    bbc.memory.address.bus[0xFE09] = ord("Q")  # write TDR
    bbc.debugger.step_cycles(4000)

    assert bytes(bbc.extensions[RpcSerial].receive()) == b"Q"


def test_rpc_serial_receive_round_trip(rpc_serial_bbc):
    """Bytes the rpc-serial client sends are shifted into the ACIA."""
    bbc = rpc_serial_bbc
    _program_serial(bbc)

    assert bbc.extensions[RpcSerial].send(b"Z") == 1

    got_rdrf = False
    for _ in range(10):
        bbc.debugger.step_cycles(2000)
        if bbc.memory.address.peek[0xFE08] & 0x01:  # RDRF
            got_rdrf = True
            break
    assert got_rdrf, "ACIA never reported RDRF for the injected byte"
    assert bbc.memory.address.bus[0xFE09] == ord("Z")


def test_loopback_serial_echo(loopback_serial_bbc):
    """The loopback-serial extension echoes a transmitted byte back to RDRF."""
    bbc = loopback_serial_bbc
    _program_serial(bbc)

    bbc.memory.address.bus[0xFE09] = ord("K")  # transmit

    got_rdrf = False
    for _ in range(12):
        bbc.debugger.step_cycles(2000)
        if bbc.memory.address.peek[0xFE08] & 0x01:  # RDRF
            got_rdrf = True
            break
    assert got_rdrf, "loopback byte never arrived at the receiver"
    assert bbc.memory.address.bus[0xFE09] == ord("K")


@pytest.fixture
def host_serial_bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    """A BBC whose serial port is owned by the host-serial extension (pty mode)."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=["--host-serial", "mode=pty"],
        ) as instance:
            instance.debugger.stop()
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@_posix_only
def test_host_serial_get_config_reports_pty(host_serial_bbc):
    """GetConfig reports the pty bridge created at launch."""
    config = host_serial_bbc.extensions[HostSerial].get_config()
    assert config.mode == "pty"
    assert config.path  # the advertised pty slave path
    assert config.serial_open


@_posix_only
def test_host_serial_set_config_rejects_runtime_pty(host_serial_bbc):
    """A runtime switch to pty is rejected (a pty is startup-only)."""
    with pytest.raises(grpc.RpcError) as excinfo:
        host_serial_bbc.extensions[HostSerial].set_config(mode="pty")
    assert excinfo.value.code() == grpc.StatusCode.INVALID_ARGUMENT


@_posix_only
def test_host_serial_set_config_repoints_to_device(host_serial_bbc):
    """SetConfig re-points the bridge to a device path; a baud-only diff then
    keeps the path. The re-point lands on the next emulation tick, so we step the
    machine and poll GetConfig (feedback, not a fixed sleep)."""
    bbc = host_serial_bbc

    # A fresh pty to point the bridge at; keep both ends open for its lifetime.
    master_fd, slave_fd = os.openpty()
    try:
        device_path = os.ttyname(slave_fd)

        bbc.extensions[HostSerial].set_config(mode="device", path=device_path, baud=9600)
        config = None
        for _ in range(20):
            bbc.debugger.step_cycles(2000)  # let process_pending_reopen run
            config = bbc.extensions[HostSerial].get_config()
            if config.path == device_path:
                break
        assert config is not None and config.path == device_path
        assert config.mode == "device"
        assert config.baud == 9600

        # Partial update: change only the baud, path is kept.
        bbc.extensions[HostSerial].set_config(baud=2400)
        for _ in range(20):
            bbc.debugger.step_cycles(2000)
            config = bbc.extensions[HostSerial].get_config()
            if config.baud == 2400:
                break
        assert config.baud == 2400
        assert config.path == device_path  # unchanged by the baud-only diff
    finally:
        os.close(master_fd)
        os.close(slave_fd)
