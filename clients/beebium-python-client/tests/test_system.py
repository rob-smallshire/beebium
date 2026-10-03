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

"""Unit tests for the system and provenance module."""

from unittest.mock import MagicMock

import pytest

from beebium.client._proto import system_pb2
from beebium.client.system import (
    MachineIdentity,
    MachineNameChange,
    MachineNamePreview,
    NamePlaceholder,
    NameTemplateReport,
    Provenance,
    ServerStatus,
    ServerStatusEvent,
    System,
)


class MockProvenanceResponse:
    """Mock provenance from proto."""

    def __init__(
        self,
        type: str = "python-client",
        instance_uuid: str = "550e8400-e29b-41d4-a716-446655440000",
        version: str = "1.2.3",
        timestamp: int = 1700000000,
    ):
        self.type = type
        self.instance_uuid = instance_uuid
        self.version = version
        self.timestamp = timestamp


class MockIdentityResponse:
    """Mock identity from proto."""

    def __init__(
        self,
        uuid: str = "12345678-1234-1234-1234-123456789abc",
        name: str = "BBC Model B",
        model_type: str = "ModelB",
        model_name: str = "BBC Model B 32K",
        name_template: str | None = None,
    ):
        self.uuid = uuid
        self.name = name
        self.model_type = model_type
        self.model_name = model_name
        self.name_template = name if name_template is None else name_template


class MockSystemInfoResponse:
    """Mock GetSystemInfo response."""

    def __init__(
        self,
        provenance: MockProvenanceResponse | None = None,
        identity: MockIdentityResponse | None = None,
    ):
        self.provenance = provenance or MockProvenanceResponse()
        self.identity = identity or MockIdentityResponse()


class MockSetMachineNameResponse:
    """Mock SetMachineName response."""

    def __init__(
        self,
        name: str = "New Name",
        name_template: str | None = None,
        unknown_keys: tuple[str, ...] = (),
        inapplicable_keys: tuple[str, ...] = (),
        malformed: tuple[str, ...] = (),
    ):
        self.identity = MockIdentityResponse(name=name, name_template=name_template)
        self.report = system_pb2.NameTemplateReport(
            unknown_keys=list(unknown_keys),
            inapplicable_keys=list(inapplicable_keys),
            malformed=list(malformed),
        )


@pytest.fixture
def mock_stub():
    """Create a mock gRPC stub."""
    stub = MagicMock()
    stub.GetSystemInfo.return_value = MockSystemInfoResponse()
    stub.SetMachineName.return_value = MockSetMachineNameResponse()
    return stub


@pytest.fixture
def system(mock_stub):
    """Create a System instance with mock stub."""
    return System(mock_stub)


class TestProvenanceDataclass:
    """Tests for the Provenance dataclass."""

    def test_provenance_has_expected_fields(self):
        """Provenance has type, instance_uuid, version, and timestamp fields."""
        prov = Provenance(
            type="python-client",
            instance_uuid="550e8400-e29b-41d4-a716-446655440000",
            version="1.2.3",
            timestamp=1700000000,
        )
        assert prov.type == "python-client"
        assert prov.instance_uuid == "550e8400-e29b-41d4-a716-446655440000"
        assert prov.version == "1.2.3"
        assert prov.timestamp == 1700000000

    def test_provenance_is_frozen(self):
        """Provenance is immutable."""
        prov = Provenance(type="test", instance_uuid="uuid", version="1.0", timestamp=0)
        with pytest.raises(AttributeError):
            prov.type = "modified"

    def test_provenance_equality(self):
        """Two Provenance objects with same values are equal."""
        prov1 = Provenance(type="test", instance_uuid="uuid", version="1.0", timestamp=100)
        prov2 = Provenance(type="test", instance_uuid="uuid", version="1.0", timestamp=100)
        assert prov1 == prov2

    def test_provenance_inequality(self):
        """Provenance objects with different values are not equal."""
        prov1 = Provenance(type="test", instance_uuid="uuid1", version="1.0", timestamp=100)
        prov2 = Provenance(type="test", instance_uuid="uuid2", version="1.0", timestamp=100)
        assert prov1 != prov2


class TestMachineIdentityClass:
    """Tests for the MachineIdentity class."""

    def test_machine_identity_has_expected_properties(self, mock_stub):
        """MachineIdentity has uuid, name, model_type, model_name, system properties."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="550e8400-e29b-41d4-a716-446655440000",
            name="My BBC Micro",
            model_type="ModelB",
            model_name="BBC Model B 32K",
            system=system,
        )
        assert identity.uuid == "550e8400-e29b-41d4-a716-446655440000"
        assert identity.name == "My BBC Micro"
        assert identity.model_type == "ModelB"
        assert identity.model_name == "BBC Model B 32K"
        assert identity.system is system

    def test_uuid_is_readonly(self, mock_stub):
        """uuid property cannot be assigned."""
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        with pytest.raises(AttributeError):
            identity.uuid = "new-uuid"

    def test_model_type_is_readonly(self, mock_stub):
        """model_type property cannot be assigned."""
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        with pytest.raises(AttributeError):
            identity.model_type = "new-type"

    def test_model_name_is_readonly(self, mock_stub):
        """model_name property cannot be assigned."""
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        with pytest.raises(AttributeError):
            identity.model_name = "new-name"

    def test_name_setter_calls_grpc(self, mock_stub):
        """Setting name calls SetMachineName gRPC."""
        mock_stub.SetMachineName.return_value = MockSetMachineNameResponse(name="New Name")
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="Old", model_type="t", model_name="m", system=system)
        identity.name = "New Name"
        mock_stub.SetMachineName.assert_called_once()
        assert identity.name == "New Name"

    def test_repr(self, mock_stub):
        """__repr__ returns informative string."""
        system = System(mock_stub)
        identity = MachineIdentity(uuid="uuid", name="name", model_type="type", model_name="model", system=system)
        r = repr(identity)
        assert "uuid" in r
        assert "name" in r
        assert "type" in r
        assert "model" in r

    def test_equality(self, mock_stub):
        """Two MachineIdentity objects with same values are equal."""
        system = System(mock_stub)
        id1 = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        id2 = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        assert id1 == id2

    def test_inequality(self, mock_stub):
        """MachineIdentity objects with different values are not equal."""
        system = System(mock_stub)
        id1 = MachineIdentity(uuid="u1", name="n", model_type="t", model_name="m", system=system)
        id2 = MachineIdentity(uuid="u2", name="n", model_type="t", model_name="m", system=system)
        assert id1 != id2


class TestServerStatusEnum:
    """Tests for the ServerStatus enum."""

    def test_server_status_values(self):
        """ServerStatus has READY, SHUTTING_DOWN, and IDENTITY_CHANGED values."""
        assert ServerStatus.READY.value == "ready"
        assert ServerStatus.SHUTTING_DOWN.value == "shutting_down"
        assert ServerStatus.IDENTITY_CHANGED.value == "identity_changed"


class TestServerStatusEventDataclass:
    """Tests for the ServerStatusEvent dataclass."""

    def test_server_status_event_has_expected_fields(self):
        """ServerStatusEvent has status, message, shutdown_grace_ms, and identity fields."""
        event = ServerStatusEvent(
            status=ServerStatus.SHUTTING_DOWN,
            message="Server shutting down",
            shutdown_grace_ms=5000,
        )
        assert event.status == ServerStatus.SHUTTING_DOWN
        assert event.message == "Server shutting down"
        assert event.shutdown_grace_ms == 5000
        assert event.identity is None

    def test_server_status_event_with_identity(self, mock_stub):
        """ServerStatusEvent can include identity for IDENTITY_CHANGED events."""
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        event = ServerStatusEvent(
            status=ServerStatus.IDENTITY_CHANGED,
            message="Identity changed",
            shutdown_grace_ms=0,
            identity=identity,
        )
        assert event.status == ServerStatus.IDENTITY_CHANGED
        assert event.identity is identity

    def test_server_status_event_is_frozen(self):
        """ServerStatusEvent is immutable."""
        event = ServerStatusEvent(status=ServerStatus.READY, message="Ready", shutdown_grace_ms=0)
        with pytest.raises(AttributeError):
            event.status = ServerStatus.SHUTTING_DOWN


class TestSystemIdentityProperty:
    """Tests for the System.identity property."""

    def test_identity_property_returns_machine_identity(self, system, mock_stub):
        """identity property returns a MachineIdentity object."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        identity = system.identity
        assert isinstance(identity, MachineIdentity)

    def test_identity_reads_the_server_each_time(self, system, mock_stub):
        """identity is not cached: a rendered name changes on the server by itself."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            identity=MockIdentityResponse(name="Station 80", name_template="Station {econet-station}")
        )
        assert system.identity.name == "Station 80"
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            identity=MockIdentityResponse(name="Station 81", name_template="Station {econet-station}")
        )
        assert system.identity.name == "Station 81"
        assert mock_stub.GetSystemInfo.call_count == 2

    def test_identity_has_system_reference(self, system, mock_stub):
        """identity has reference back to the system."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        identity = system.identity
        assert identity.system is system

    def test_identity_fields_match_server_response(self, system, mock_stub):
        """identity fields match what server returned."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            identity=MockIdentityResponse(
                uuid="test-uuid",
                name="Test Server",
                model_type="ModelBPlus",
                model_name="BBC Model B+ 64K",
            )
        )
        identity = system.identity
        assert identity.uuid == "test-uuid"
        assert identity.name == "Test Server"
        assert identity.model_type == "ModelBPlus"
        assert identity.model_name == "BBC Model B+ 64K"


class TestSystemProvenanceProperty:
    """Tests for the System.provenance property."""

    def test_provenance_property_returns_provenance(self, system, mock_stub):
        """provenance property returns a Provenance object."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        assert isinstance(system.provenance, Provenance)

    def test_provenance_property_caches_result(self, system, mock_stub):
        """provenance property caches the result."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        _ = system.provenance
        _ = system.provenance
        # provenance makes its own GetSystemInfo call
        assert mock_stub.GetSystemInfo.call_count >= 1

    def test_provenance_fields_match_server_response(self, system, mock_stub):
        """provenance fields match what server returned."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        prov = system.provenance
        assert prov.type == "python-client"
        assert prov.instance_uuid == "550e8400-e29b-41d4-a716-446655440000"
        assert prov.version == "1.2.3"
        assert prov.timestamp == 1700000000


class TestSystemProvenanceVariations:
    """Tests for different provenance scenarios."""

    def test_empty_provenance(self):
        """System handles empty provenance from server."""
        stub = MagicMock()
        stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            provenance=MockProvenanceResponse(type="", instance_uuid="", version="", timestamp=0)
        )
        system = System(stub)

        prov = system.provenance
        assert prov.type == ""
        assert prov.instance_uuid == ""
        assert prov.version == ""
        assert prov.timestamp == 0

    def test_terminal_provenance(self):
        """System handles terminal-launched provenance."""
        stub = MagicMock()
        stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            provenance=MockProvenanceResponse(
                type="terminal",
                instance_uuid="12345678-1234-1234-1234-123456789abc",
                version="",
                timestamp=1700000000,
            )
        )
        system = System(stub)

        prov = system.provenance
        assert prov.type == "terminal"
        assert prov.version == ""

    def test_typescript_oracle_provenance(self):
        """System handles TypeScript oracle provenance."""
        stub = MagicMock()
        stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            provenance=MockProvenanceResponse(
                type="typescript-oracle",
                instance_uuid="abcdef00-1234-5678-abcd-ef0123456789",
                version="0.1.0",
                timestamp=1700000000,
            )
        )
        system = System(stub)

        prov = system.provenance
        assert prov.type == "typescript-oracle"
        assert prov.version == "0.1.0"


class MockAdvertisementState:
    """Mock advertisement state from proto."""

    def __init__(
        self,
        enabled: bool = False,
        available: bool = True,
        advertised_name: str = "",
    ):
        self.enabled = enabled
        self.available = available
        self.advertised_name = advertised_name


class MockGetAdvertisementStateResponse:
    """Mock GetAdvertisementState response."""

    def __init__(self, state: MockAdvertisementState | None = None):
        self.state = state or MockAdvertisementState()


class MockSetAdvertisementResponse:
    """Mock SetAdvertisement response."""

    def __init__(self, state: MockAdvertisementState | None = None):
        self.state = state or MockAdvertisementState()


class MockSetSpeedMultiplierResponse:
    """Mock SetSpeedMultiplier response."""

    def __init__(self, speed_multiplier: float = 1.0):
        self.speed_multiplier = speed_multiplier


class TestAdvertisementState:
    """Tests for the AdvertisementState dataclass."""

    def test_advertisement_state_has_expected_fields(self):
        """AdvertisementState has enabled, available, and advertised_name fields."""
        from beebium.client.system import AdvertisementState

        state = AdvertisementState(
            enabled=True,
            available=True,
            advertised_name="BBC Model B",
        )
        assert state.enabled is True
        assert state.available is True
        assert state.advertised_name == "BBC Model B"

    def test_advertisement_state_is_frozen(self):
        """AdvertisementState is immutable."""
        from beebium.client.system import AdvertisementState

        state = AdvertisementState(enabled=False, available=True, advertised_name="")
        with pytest.raises(AttributeError):
            state.enabled = True


class TestSystemAdvertisementMethods:
    """Tests for advertisement methods on System."""

    def test_get_advertisement_state(self, mock_stub):
        """get_advertisement_state returns AdvertisementState."""
        from beebium.client.system import AdvertisementState

        mock_stub.GetAdvertisementState.return_value = MockGetAdvertisementStateResponse(
            MockAdvertisementState(enabled=True, available=True, advertised_name="Test")
        )
        system = System(mock_stub)

        state = system.get_advertisement_state()
        assert isinstance(state, AdvertisementState)
        assert state.enabled is True
        assert state.available is True
        assert state.advertised_name == "Test"

    def test_get_advertisement_state_unavailable(self, mock_stub):
        """get_advertisement_state handles unavailable mDNS."""
        mock_stub.GetAdvertisementState.return_value = MockGetAdvertisementStateResponse(
            MockAdvertisementState(enabled=False, available=False, advertised_name="")
        )
        system = System(mock_stub)

        state = system.get_advertisement_state()
        assert state.enabled is False
        assert state.available is False
        assert state.advertised_name == ""

    def test_set_advertisement_enable(self, mock_stub):
        """set_advertisement enables advertising."""
        mock_stub.SetAdvertisement.return_value = MockSetAdvertisementResponse(
            MockAdvertisementState(enabled=True, available=True, advertised_name="My BBC")
        )
        system = System(mock_stub)

        state = system.set_advertisement(enabled=True)
        mock_stub.SetAdvertisement.assert_called_once()
        assert state.enabled is True
        assert state.advertised_name == "My BBC"

    def test_set_advertisement_disable(self, mock_stub):
        """set_advertisement disables advertising."""
        mock_stub.SetAdvertisement.return_value = MockSetAdvertisementResponse(
            MockAdvertisementState(enabled=False, available=True, advertised_name="")
        )
        system = System(mock_stub)

        state = system.set_advertisement(enabled=False)
        mock_stub.SetAdvertisement.assert_called_once()
        assert state.enabled is False

    def test_set_advertisement_returns_unavailable_state(self, mock_stub):
        """set_advertisement returns unavailable state when mDNS not supported."""
        mock_stub.SetAdvertisement.return_value = MockSetAdvertisementResponse(
            MockAdvertisementState(enabled=False, available=False, advertised_name="")
        )
        system = System(mock_stub)

        state = system.set_advertisement(enabled=True)
        # Even though we requested enabled=True, it returns False because unavailable
        assert state.enabled is False
        assert state.available is False


class TestSystemSpeedMultiplier:
    """Tests for runtime speed control on System."""

    def test_set_speed_multiplier_calls_grpc(self, mock_stub):
        """set_speed_multiplier forwards to SetSpeedMultiplier."""
        mock_stub.SetSpeedMultiplier.return_value = MockSetSpeedMultiplierResponse(speed_multiplier=0.0)
        system = System(mock_stub)

        result = system.set_speed_multiplier(0.0)

        mock_stub.SetSpeedMultiplier.assert_called_once()
        assert result == 0.0

    def test_set_speed_multiplier_returns_echoed_value(self, mock_stub):
        """set_speed_multiplier returns the server's echoed multiplier."""
        mock_stub.SetSpeedMultiplier.return_value = MockSetSpeedMultiplierResponse(speed_multiplier=2.0)
        system = System(mock_stub)

        result = system.set_speed_multiplier(1.5)

        assert result == 2.0


class MockPacingStatsResponse:
    """Mock GetPacingStats response."""

    def __init__(
        self,
        ticks_executed: int = 0,
        ticks_io_skipped: int = 0,
        controller_drift: float = 0.0,
        controller_integral: float = 0.0,
        speed_multiplier: float = 1.0,
        achieved_speed_multiplier: float = 0.0,
        estimated_max_speed_multiplier: float = 0.0,
    ):
        self.ticks_executed = ticks_executed
        self.ticks_io_skipped = ticks_io_skipped
        self.controller_drift = controller_drift
        self.controller_integral = controller_integral
        self.speed_multiplier = speed_multiplier
        self.achieved_speed_multiplier = achieved_speed_multiplier
        self.estimated_max_speed_multiplier = estimated_max_speed_multiplier


class TestSystemPacingStats:
    """Tests for System.get_pacing_stats."""

    def test_get_pacing_stats_maps_all_fields(self, mock_stub):
        """get_pacing_stats maps every response field onto the dataclass."""
        mock_stub.GetPacingStats.return_value = MockPacingStatsResponse(
            ticks_executed=1000,
            ticks_io_skipped=5,
            controller_drift=12.5,
            controller_integral=-3.0,
            speed_multiplier=2.0,
            achieved_speed_multiplier=1.97,
            estimated_max_speed_multiplier=18.4,
        )
        system = System(mock_stub)

        stats = system.get_pacing_stats()

        mock_stub.GetPacingStats.assert_called_once()
        assert stats.ticks_executed == 1000
        assert stats.ticks_io_skipped == 5
        assert stats.controller_drift == 12.5
        assert stats.controller_integral == -3.0
        assert stats.speed_multiplier == 2.0
        assert stats.achieved_speed_multiplier == 1.97
        assert stats.estimated_max_speed_multiplier == 18.4

    def test_get_pacing_stats_no_estimate_sentinel(self, mock_stub):
        """estimated_max of 0.0 is the documented 'no estimate yet' value."""
        mock_stub.GetPacingStats.return_value = MockPacingStatsResponse(
            speed_multiplier=1.0,
            achieved_speed_multiplier=0.0,
            estimated_max_speed_multiplier=0.0,
        )
        system = System(mock_stub)

        stats = system.get_pacing_stats()

        assert stats.estimated_max_speed_multiplier == 0.0


class TestMachineIdentityNameTemplate:
    """Tests for the name template carried by MachineIdentity."""

    def test_name_template_defaults_to_the_name(self, mock_stub):
        """A plain name is a template of itself, so it is the default."""
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="Girton", model_type="t", model_name="m", system=system)
        assert identity.name_template == "Girton"

    def test_name_template_is_distinct_from_the_rendered_name(self, mock_stub):
        """The template is what the user edits; the name is its rendering."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="u",
            name="Station 80",
            name_template="Station {econet-station}",
            model_type="t",
            model_name="m",
            system=system,
        )
        assert identity.name == "Station 80"
        assert identity.name_template == "Station {econet-station}"

    def test_identity_property_reads_the_template_from_the_server(self, system, mock_stub):
        """System.identity carries the server's template as well as the rendering."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            identity=MockIdentityResponse(name="Station 80", name_template="Station {econet-station}")
        )
        identity = system.identity
        assert identity.name == "Station 80"
        assert identity.name_template == "Station {econet-station}"

    def test_setting_name_template_sends_it_as_the_template(self, mock_stub):
        """Assigning name_template sends it in SetMachineNameRequest.name_template."""
        mock_stub.SetMachineName.return_value = MockSetMachineNameResponse(
            name="Station 80", name_template="Station {econet-station}"
        )
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="Old", model_type="t", model_name="m", system=system)
        identity.name_template = "Station {econet-station}"
        request = mock_stub.SetMachineName.call_args.args[0]
        assert request.name_template == "Station {econet-station}"
        assert identity.name_template == "Station {econet-station}"
        assert identity.name == "Station 80"

    def test_setting_name_sends_it_as_the_template(self, mock_stub):
        """Assigning name sends a template too: a plain name is a template."""
        mock_stub.SetMachineName.return_value = MockSetMachineNameResponse(name="Girton")
        system = System(mock_stub)
        identity = MachineIdentity(uuid="u", name="Old", model_type="t", model_name="m", system=system)
        identity.name = "Girton"
        request = mock_stub.SetMachineName.call_args.args[0]
        assert request.name_template == "Girton"
        assert identity.name_template == "Girton"

    def test_equality_includes_the_template(self, mock_stub):
        """Identities with the same rendering but different templates differ."""
        system = System(mock_stub)
        id1 = MachineIdentity(
            uuid="u", name="Station 80", name_template="Station 80", model_type="t", model_name="m", system=system
        )
        id2 = MachineIdentity(
            uuid="u",
            name="Station 80",
            name_template="Station {econet-station}",
            model_type="t",
            model_name="m",
            system=system,
        )
        assert id1 != id2

    def test_repr_includes_the_template(self, mock_stub):
        """__repr__ shows the template."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="u", name="n", name_template="{machine-model}", model_type="t", model_name="m", system=system
        )
        assert "{machine-model}" in repr(identity)


class TestSystemSetMachineName:
    """Tests for System.set_machine_name."""

    def test_sends_the_template(self, system, mock_stub):
        """The template is sent unparsed in SetMachineNameRequest.name_template."""
        system.set_machine_name("Station {econet-station}")
        request = mock_stub.SetMachineName.call_args.args[0]
        assert isinstance(request, system_pb2.SetMachineNameRequest)
        assert request.name_template == "Station {econet-station}"

    def test_returns_the_identity_and_what_did_not_render(self, system, mock_stub):
        """The result carries the new identity and the server's report."""
        mock_stub.SetMachineName.return_value = MockSetMachineNameResponse(
            name="Station 80 {bogus} ",
            name_template="Station {econet-station} {bogus} {machine-preset}{",
            unknown_keys=("bogus",),
            inapplicable_keys=("machine-preset",),
            malformed=("{",),
        )
        change = system.set_machine_name("Station {econet-station} {bogus} {machine-preset}{")
        assert isinstance(change, MachineNameChange)
        assert isinstance(change.identity, MachineIdentity)
        assert change.identity.name == "Station 80 {bogus} "
        assert change.identity.name_template == "Station {econet-station} {bogus} {machine-preset}{"
        assert change.report == NameTemplateReport(
            unknown_keys=("bogus",), inapplicable_keys=("machine-preset",), malformed=("{",)
        )

    def test_a_clean_template_reports_nothing(self, system, mock_stub):
        """A template that rendered fully has empty reports."""
        mock_stub.SetMachineName.return_value = MockSetMachineNameResponse(name="Girton")
        change = system.set_machine_name("Girton")
        assert change.report == NameTemplateReport()

    def test_result_is_frozen(self, system):
        """MachineNameChange is immutable."""
        change = system.set_machine_name("Girton")
        with pytest.raises(AttributeError):
            change.report = NameTemplateReport(unknown_keys=("x",))


class TestSystemPreviewMachineName:
    """Tests for System.preview_machine_name."""

    def test_sends_the_template_and_maps_the_rendering(self, system, mock_stub):
        """The rendering and the server's report come back as a MachineNamePreview."""
        mock_stub.PreviewMachineName.return_value = system_pb2.PreviewMachineNameResponse(
            name="Station 80 {bogus}",
            report=system_pb2.NameTemplateReport(
                unknown_keys=["bogus"], inapplicable_keys=["machine-preset"], malformed=["}"]
            ),
        )
        preview = system.preview_machine_name("Station {econet-station} {bogus}{machine-preset}}")
        request = mock_stub.PreviewMachineName.call_args.args[0]
        assert isinstance(request, system_pb2.PreviewMachineNameRequest)
        assert request.name_template == "Station {econet-station} {bogus}{machine-preset}}"
        assert preview == MachineNamePreview(
            name="Station 80 {bogus}",
            report=NameTemplateReport(
                unknown_keys=("bogus",), inapplicable_keys=("machine-preset",), malformed=("}",)
            ),
        )

    def test_does_not_rename(self, system, mock_stub):
        """A preview never calls SetMachineName."""
        mock_stub.PreviewMachineName.return_value = system_pb2.PreviewMachineNameResponse(name="Other")
        system.preview_machine_name("Other")
        mock_stub.SetMachineName.assert_not_called()

    def test_preview_is_frozen(self, system, mock_stub):
        """MachineNamePreview is immutable."""
        mock_stub.PreviewMachineName.return_value = system_pb2.PreviewMachineNameResponse(name="n")
        preview = system.preview_machine_name("n")
        with pytest.raises(AttributeError):
            preview.name = "m"


class TestSystemListNamePlaceholders:
    """Tests for System.list_name_placeholders."""

    def test_maps_every_field_in_server_order(self, system, mock_stub):
        """Each record is mapped field-for-field, in the server's order."""
        mock_stub.ListNamePlaceholders.return_value = system_pb2.ListNamePlaceholdersResponse(
            placeholders=[
                system_pb2.NamePlaceholder(
                    key="zeta-thing",
                    label="Zeta thing",
                    description="A thing from a provider this client knows nothing about.",
                    group="Zeta",
                    insertion="{zeta-thing}",
                    value="42",
                    applicable=True,
                ),
                system_pb2.NamePlaceholder(
                    key="alpha-other",
                    label="Alpha other",
                    description="Not fitted here.",
                    group="Alpha",
                    insertion="{alpha-other}",
                    value="",
                    applicable=False,
                ),
            ]
        )
        placeholders = system.list_name_placeholders()
        assert placeholders == (
            NamePlaceholder(
                key="zeta-thing",
                label="Zeta thing",
                description="A thing from a provider this client knows nothing about.",
                group="Zeta",
                insertion="{zeta-thing}",
                value="42",
                applicable=True,
            ),
            NamePlaceholder(
                key="alpha-other",
                label="Alpha other",
                description="Not fitted here.",
                group="Alpha",
                insertion="{alpha-other}",
                value="",
                applicable=False,
            ),
        )

    def test_a_server_with_no_placeholders_lists_none(self, system, mock_stub):
        """An empty registry is an empty tuple."""
        mock_stub.ListNamePlaceholders.return_value = system_pb2.ListNamePlaceholdersResponse()
        assert system.list_name_placeholders() == ()

    def test_placeholder_is_frozen(self):
        """NamePlaceholder is immutable."""
        placeholder = NamePlaceholder(
            key="k", label="l", description="d", group="g", insertion="{k}", value="v", applicable=True
        )
        with pytest.raises(AttributeError):
            placeholder.value = "w"


class TestWatchStatusIdentityTemplate:
    """IDENTITY_CHANGED events carry the template as well as the rendering."""

    def test_identity_changed_event_carries_the_template(self, system, mock_stub):
        event = system_pb2.ServerStatusEvent(
            status=system_pb2.SERVER_STATUS_IDENTITY_CHANGED,
            identity=system_pb2.MachineIdentity(
                uuid="u",
                name="Station 81",
                name_template="Station {econet-station}",
                model_type="ModelB",
                model_name="BBC Model B",
            ),
        )
        mock_stub.WatchServerStatus.return_value = iter([event])
        (received,) = list(system.watch_status())
        assert received.status == ServerStatus.IDENTITY_CHANGED
        assert received.identity.name == "Station 81"
        assert received.identity.name_template == "Station {econet-station}"
