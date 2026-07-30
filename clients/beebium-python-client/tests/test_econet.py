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

"""Unit tests for the Econet module."""

from unittest.mock import MagicMock

import pytest

from beebium.client.econet import (
    AdlcStatus,
    Econet,
    EconetStatus,
    HandshakeStatus,
)
from beebium.client.exceptions import EconetError


class MockAdlcStatus:
    """Mock ADLC status from proto."""

    def __init__(
        self,
        cr1=0,
        cr2=0,
        cr3=0,
        cr4=0,
        sr1=0,
        sr2=0,
        irq_output=False,
        tx_fifo_empty=True,
        tx_fifo_full=False,
        rx_fifo_empty=True,
        rx_fifo_full=False,
        tx_frame_field="idle",
        rx_frame_field="idle",
        pse_level=0,
        cts_input=False,
    ):
        self.cr1 = cr1
        self.cr2 = cr2
        self.cr3 = cr3
        self.cr4 = cr4
        self.sr1 = sr1
        self.sr2 = sr2
        self.irq_output = irq_output
        self.tx_fifo_empty = tx_fifo_empty
        self.tx_fifo_full = tx_fifo_full
        self.rx_fifo_empty = rx_fifo_empty
        self.rx_fifo_full = rx_fifo_full
        self.tx_frame_field = tx_frame_field
        self.rx_frame_field = rx_frame_field
        self.pse_level = pse_level
        self.cts_input = cts_input


class MockHandshakeStatus:
    """Mock handshake status from proto."""

    def __init__(self, stage="idle", flag_fill_active=False,
                 frames_held=0, frames_redelivered=0,
                 frames_expired=0, frames_dropped=0):
        self.stage = stage
        self.flag_fill_active = flag_fill_active
        self.frames_held = frames_held
        self.frames_redelivered = frames_redelivered
        self.frames_expired = frames_expired
        self.frames_dropped = frames_dropped


class MockGetEconetStatusResponse:
    """Mock GetEconetStatus response."""

    def __init__(
        self,
        has_econet_socket=True,
        enabled=False,
        station_id=0,
        aun_mode=False,
        connected=False,
        adlc=None,
        handshake=None,
        tick_count=0,
        cr1_0x82_write_count=0,
        rx_frames_received_count=0,
        rx_blocked_by_reset_count=0,
        scout_ack_generated_count=0,
        tx_frames_from_beeb_count=0,
        unexpected_tx_reset_count=0,
        tx_from_idle_count=0,
        max_handshake_timer_seen=0,
        watchdog_timeout_count=0,
        send_stage_log="",
        ticks_with_timer_active=0,
        read_stretch_parasite_ticks=0,
    ):
        self.has_econet_socket = has_econet_socket
        self.enabled = enabled
        self.station_id = station_id
        self.aun_mode = aun_mode
        self.connected = connected
        self.adlc = adlc
        self.handshake = handshake
        self.tick_count = tick_count
        self.cr1_0x82_write_count = cr1_0x82_write_count
        self.rx_frames_received_count = rx_frames_received_count
        self.rx_blocked_by_reset_count = rx_blocked_by_reset_count
        self.scout_ack_generated_count = scout_ack_generated_count
        self.tx_frames_from_beeb_count = tx_frames_from_beeb_count
        self.unexpected_tx_reset_count = unexpected_tx_reset_count
        self.tx_from_idle_count = tx_from_idle_count
        self.max_handshake_timer_seen = max_handshake_timer_seen
        self.watchdog_timeout_count = watchdog_timeout_count
        self.send_stage_log = send_stage_log
        self.ticks_with_timer_active = ticks_with_timer_active
        self.read_stretch_parasite_ticks = read_stretch_parasite_ticks
        self._has_adlc = adlc is not None
        self._has_handshake = handshake is not None

    def HasField(self, name):
        if name == "adlc":
            return self._has_adlc
        if name == "handshake":
            return self._has_handshake
        return False


class MockEnableEconetResponse:
    """Mock EnableEconet response."""

    def __init__(self, success=True, error="", actual_aun_port=32768):
        self.success = success
        self.error = error
        self.actual_aun_port = actual_aun_port


class MockDisableEconetResponse:
    """Mock DisableEconet response."""

    def __init__(self, success=True, error=""):
        self.success = success
        self.error = error


class MockSetStationIdResponse:
    """Mock SetStationId response."""

    def __init__(self, success=True, error=""):
        self.success = success
        self.error = error


@pytest.fixture
def mock_stub():
    """Create a mock gRPC stub."""
    stub = MagicMock()
    stub.GetEconetStatus.return_value = MockGetEconetStatusResponse()
    stub.EnableEconet.return_value = MockEnableEconetResponse()
    stub.DisableEconet.return_value = MockDisableEconetResponse()
    stub.SetStationId.return_value = MockSetStationIdResponse()
    return stub


@pytest.fixture
def econet(mock_stub):
    """Create an Econet instance with mock stub."""
    return Econet(mock_stub)


class TestEconetStatus:
    """Tests for the status property."""

    def test_status_disabled(self, econet):
        """Default status returns disabled Econet."""
        status = econet.status
        assert isinstance(status, EconetStatus)
        assert status.has_econet_socket is True
        assert status.enabled is False
        assert status.station_id == 0
        assert status.aun_mode is False
        assert status.connected is False
        assert status.adlc is None
        assert status.handshake is None

    def test_status_enabled_with_adlc(self, mock_stub, econet):
        """Enabled status populates ADLC fields."""
        mock_stub.GetEconetStatus.return_value = MockGetEconetStatusResponse(
            enabled=True,
            station_id=254,
            aun_mode=True,
            connected=True,
            adlc=MockAdlcStatus(
                cr1=0x40,
                sr1=0x01,
                irq_output=True,
                tx_fifo_empty=False,
                rx_fifo_full=True,
                tx_frame_field="data",
                rx_frame_field="address",
                pse_level=2,
                cts_input=True,
            ),
        )
        status = econet.status
        assert status.enabled is True
        assert status.station_id == 254
        assert status.adlc is not None
        assert status.adlc.cr1 == 0x40
        assert status.adlc.sr1 == 0x01
        assert status.adlc.irq_output is True
        assert status.adlc.tx_fifo_empty is False
        assert status.adlc.rx_fifo_full is True
        assert status.adlc.tx_frame_field == "data"
        assert status.adlc.rx_frame_field == "address"
        assert status.adlc.pse_level == 2
        assert status.adlc.cts_input is True

    def test_status_enabled_with_handshake(self, mock_stub, econet):
        """Enabled AUN status populates handshake fields."""
        mock_stub.GetEconetStatus.return_value = MockGetEconetStatusResponse(
            enabled=True,
            station_id=100,
            aun_mode=True,
            handshake=MockHandshakeStatus(
                stage="scout_ack_sent",
                flag_fill_active=True,
                frames_held=2,
                frames_redelivered=7,
                frames_expired=1,
                frames_dropped=3,
            ),
        )
        status = econet.status
        assert status.handshake is not None
        assert status.handshake.stage == "scout_ack_sent"
        assert status.handshake.flag_fill_active is True
        assert status.handshake.frames_held == 2
        assert status.handshake.frames_redelivered == 7
        assert status.handshake.frames_expired == 1
        assert status.handshake.frames_dropped == 3


class TestWatchStatus:
    """Tests for the watch_status streaming method."""

    def test_watch_status_yields_statuses(self, mock_stub, econet):
        """watch_status yields an EconetStatus per server message."""
        mock_stub.WatchEconetStatus.return_value = iter(
            [
                MockGetEconetStatusResponse(enabled=False),
                MockGetEconetStatusResponse(enabled=True, station_id=42),
                MockGetEconetStatusResponse(enabled=True, station_id=42, connected=True),
            ]
        )
        statuses = list(econet.watch_status())
        assert len(statuses) == 3
        assert statuses[0].enabled is False
        assert statuses[1].enabled is True
        assert statuses[1].station_id == 42
        assert statuses[2].connected is True

    def test_watch_status_default_interval_is_zero(self, mock_stub, econet):
        """Default min_interval_ms of 0 lets the server pick its own."""
        mock_stub.WatchEconetStatus.return_value = iter([])
        list(econet.watch_status())
        request = mock_stub.WatchEconetStatus.call_args[0][0]
        assert request.min_interval_ms == 0

    def test_watch_status_passes_interval(self, mock_stub, econet):
        """Explicit min_interval_ms is forwarded to the server."""
        mock_stub.WatchEconetStatus.return_value = iter([])
        list(econet.watch_status(min_interval_ms=200))
        request = mock_stub.WatchEconetStatus.call_args[0][0]
        assert request.min_interval_ms == 200


class TestConvenienceProperties:
    """Tests for is_enabled and station_id convenience properties."""

    def test_is_enabled(self, mock_stub, econet):
        """is_enabled reflects status."""
        mock_stub.GetEconetStatus.return_value = MockGetEconetStatusResponse(enabled=True, station_id=254)
        assert econet.is_enabled is True

    def test_is_enabled_when_disabled(self, econet):
        """is_enabled returns False when disabled."""
        assert econet.is_enabled is False

    def test_station_id(self, mock_stub, econet):
        """station_id reflects status."""
        mock_stub.GetEconetStatus.return_value = MockGetEconetStatusResponse(enabled=True, station_id=42)
        assert econet.station_id == 42

    def test_station_id_when_disabled(self, econet):
        """station_id returns 0 when disabled."""
        assert econet.station_id == 0


class TestEnable:
    """Tests for the enable method."""

    def test_enable_success(self, mock_stub, econet):
        """enable returns actual AUN port on success."""
        mock_stub.EnableEconet.return_value = MockEnableEconetResponse(success=True, actual_aun_port=32768)
        port = econet.enable(station_id=254)
        assert port == 32768
        mock_stub.EnableEconet.assert_called_once()

    def test_enable_failure_raises(self, mock_stub, econet):
        """enable raises EconetError on failure."""
        mock_stub.EnableEconet.return_value = MockEnableEconetResponse(success=False, error="Station ID out of range")
        with pytest.raises(EconetError, match="Station ID out of range"):
            econet.enable(station_id=0)

    def test_enable_no_network(self, mock_stub, econet):
        """enable passes no_network kwarg to stub."""
        mock_stub.EnableEconet.return_value = MockEnableEconetResponse(success=True, actual_aun_port=0)
        econet.enable(station_id=254, no_network=True)
        call_args = mock_stub.EnableEconet.call_args
        request = call_args[0][0]
        assert request.station_id == 254
        assert request.no_network is True

    def test_enable_custom_port(self, mock_stub, econet):
        """enable passes custom aun_port to stub."""
        mock_stub.EnableEconet.return_value = MockEnableEconetResponse(success=True, actual_aun_port=9999)
        port = econet.enable(station_id=100, aun_port=9999)
        assert port == 9999
        call_args = mock_stub.EnableEconet.call_args
        request = call_args[0][0]
        assert request.aun_port == 9999


class TestDisable:
    """Tests for the disable method."""

    def test_disable_success(self, mock_stub, econet):
        """disable succeeds without exception."""
        mock_stub.DisableEconet.return_value = MockDisableEconetResponse(success=True)
        econet.disable()
        mock_stub.DisableEconet.assert_called_once()

    def test_disable_failure_raises(self, mock_stub, econet):
        """disable raises EconetError on failure."""
        mock_stub.DisableEconet.return_value = MockDisableEconetResponse(success=False, error="Econet not enabled")
        with pytest.raises(EconetError, match="Econet not enabled"):
            econet.disable()


class TestSetStationId:
    """Tests for the set_station_id method."""

    def test_set_station_id_success(self, mock_stub, econet):
        """set_station_id succeeds without exception."""
        econet.set_station_id(200)
        mock_stub.SetStationId.assert_called_once()

    def test_set_station_id_failure_raises(self, mock_stub, econet):
        """set_station_id raises EconetError on failure."""
        mock_stub.SetStationId.return_value = MockSetStationIdResponse(success=False, error="Econet is not enabled")
        with pytest.raises(EconetError, match="Econet is not enabled"):
            econet.set_station_id(42)

    def test_set_station_id_passes_args(self, mock_stub, econet):
        """set_station_id passes station_id to stub."""
        econet.set_station_id(123)
        request = mock_stub.SetStationId.call_args[0][0]
        assert request.station_id == 123


class TestDataclasses:
    """Tests for dataclass properties."""

    def test_econet_status_is_frozen(self):
        """EconetStatus is immutable."""
        status = EconetStatus(
            has_econet_socket=True,
            enabled=False,
            station_id=0,
            aun_mode=False,
            connected=False,
            adlc=None,
            handshake=None,
        )
        with pytest.raises(AttributeError):
            status.enabled = True

    def test_adlc_status_is_frozen(self):
        """AdlcStatus is immutable."""
        adlc = AdlcStatus(
            cr1=0,
            cr2=0,
            cr3=0,
            cr4=0,
            sr1=0,
            sr2=0,
            irq_output=False,
            tx_fifo_empty=True,
            tx_fifo_full=False,
            rx_fifo_empty=True,
            rx_fifo_full=False,
            tx_frame_field="idle",
            rx_frame_field="idle",
            pse_level=0,
            cts_input=False,
        )
        with pytest.raises(AttributeError):
            adlc.cr1 = 1

    def test_handshake_status_is_frozen(self):
        """HandshakeStatus is immutable."""
        hs = HandshakeStatus(stage="idle", flag_fill_active=False)
        with pytest.raises(AttributeError):
            hs.stage = "scout"
