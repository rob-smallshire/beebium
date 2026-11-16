# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
# Copyright 2026 Mark J. Fisher
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

"""Tests for the serial port (MC6850 ACIA + Serial ULA) status client.

This covers SerialService, which reports the serial hardware registers. Driving
the serial port (rpc-serial / loopback-serial) is owned by extensions and tested
over the wire in test_serial_extensions.py.
"""

from __future__ import annotations

from unittest.mock import MagicMock

from beebium.client._proto import serial_pb2
from beebium.client.serial import Serial


def _make_status_proto(**overrides):
    defaults = dict(
        has_serial_socket=True,
        acia_control=0x15,
        acia_status=0x02,
        tdre=True,
        rdrf=False,
        not_dcd=False,
        not_cts=False,
        irq_pending=False,
        ula_control=0x40,
        tx_baud=19200,
        rx_baud=19200,
        rs423_selected=True,
        motor_on=False,
        tx_ula_divider=1,
        rx_ula_divider=1,
    )
    defaults.update(overrides)
    return serial_pb2.SerialStatus(**defaults)


def test_status_parses_response():
    stub = MagicMock()
    stub.GetSerialStatus.return_value = _make_status_proto(tx_baud=9600, motor_on=True)
    serial = Serial(stub)

    status = serial.status
    assert status.has_serial_socket is True
    assert status.acia_control == 0x15
    assert status.tdre is True
    assert status.tx_baud == 9600
    assert status.motor_on is True


def test_watch_status_yields_statuses():
    stub = MagicMock()
    stub.WatchSerialStatus.return_value = iter(
        [
            _make_status_proto(rs423_selected=True),
            _make_status_proto(rs423_selected=False),
        ]
    )
    serial = Serial(stub)

    statuses = list(serial.watch_status())
    assert len(statuses) == 2
    assert statuses[0].rs423_selected is True
    assert statuses[1].rs423_selected is False


def test_watch_status_default_interval_is_zero():
    stub = MagicMock()
    stub.WatchSerialStatus.return_value = iter([])
    serial = Serial(stub)

    list(serial.watch_status())
    request = stub.WatchSerialStatus.call_args[0][0]
    assert request.min_interval_ms == 0


def test_watch_status_passes_interval():
    stub = MagicMock()
    stub.WatchSerialStatus.return_value = iter([])
    serial = Serial(stub)

    list(serial.watch_status(min_interval_ms=200))
    request = stub.WatchSerialStatus.call_args[0][0]
    assert request.min_interval_ms == 200


def test_serial_status_available(bbc):
    status = bbc.serial.status
    assert status.has_serial_socket is True
    # Model B has an RS423 connector (static, machine-defined label).
    assert status.connector == "RS423"


def test_watch_status_stream_pushes_a_change(bbc):
    """The stream sends an initial snapshot, then pushes on a state change."""
    bbc.debugger.ensure_stopped()  # freeze chip state so only our write changes it
    statuses = bbc.serial.watch_status(min_interval_ms=20)

    initial = next(statuses)
    assert initial.has_serial_socket is True

    # Flip the Serial ULA's RS423/cassette select; the server observes the change
    # and pushes a fresh snapshot (blocks only until that guaranteed push).
    want_rs423 = not initial.rs423_selected
    bbc.memory.address.bus[0xFE10] = 0x40 if want_rs423 else 0x00

    changed = next(s for s in statuses if s.rs423_selected == want_rs423)
    assert changed.rs423_selected is want_rs423
    statuses.close()
