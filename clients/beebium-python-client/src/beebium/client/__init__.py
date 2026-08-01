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

"""
Beebium - Python client for the Beebium BBC Micro emulator.

Usage:
    from beebium.client import Beebium

    # Connect to an existing server
    with Beebium.connect() as bbc:
        bbc.debugger.stop()
        print(f"PC = ${bbc.cpu.pc:04X}")

    # Launch and manage a server
    with Beebium.launch(mos_filepath="acorn-mos_1_20.rom") as bbc:
        bbc.keyboard.type("PRINT 42")
        bbc.keyboard.press_return()
"""

from beebium.client._proto.protocol_fingerprint import PROTOCOL_FINGERPRINT
from beebium.client._version import __version__
from beebium.client.audio import (
    AudioChunk,
    AudioFormat,
    AudioSource,
    ChannelGroup,
    SourceEncoding,
)
from beebium.client.client import Beebium
from beebium.client.econet import (
    AdlcStatus,
    EconetEvent,
    EconetFrameInfo,
    EconetStatus,
    HandshakeStatus,
)
from beebium.client.econet_transport import TransportInfo
from beebium.client.exceptions import (
    BeebiumError,
    ConnectionError,
    DebuggerError,
    DiscError,
    EconetError,
    ExtensionAdapterNotInstalledError,
    ExtensionAmbiguousError,
    ExtensionError,
    ExtensionNotLoadedError,
    InvalidConditionError,
    MemoryAccessError,
    ProtocolMismatchError,
    ServerNotFoundError,
    ServerStartupError,
    TimeoutError,
)
from beebium.client.extension import (
    EconetTransportAdapter,
    ExtensionAdapter,
    PeripheralExtensionAdapter,
)
from beebium.client.extension_ui import (
    Button,
    Choice,
    Control,
    ControlKind,
    DispatchResult,
    Group,
    Indicator,
    IndicatorState,
    Label,
    SubscriptionHandle,
    TextInput,
    Toggle,
    View,
)
from beebium.client.extensions import (
    ExtensionInfo,
    Extensions,
    ParameterSchemaInfo,
    StorageDevice,
    StorageKind,
)
from beebium.client.host import (
    host_identifier,
    is_local_host,
    local_host_fingerprint,
)
from beebium.client.system import (
    AdvertisementState,
    MachineIdentity,
    Provenance,
    ServerStatus,
    ServerStatusEvent,
    ShutdownConditionStatus,
    ShutdownMode,
    ShutdownResponse,
)

# Default gRPC port for beebium servers (0xBEEB = 48875)
DEFAULT_GRPC_PORT = 0xBEEB

__all__ = [
    "AdlcStatus",
    "AdvertisementState",
    "AudioChunk",
    "AudioFormat",
    "AudioSource",
    "Beebium",
    "BeebiumError",
    "Button",
    "ChannelGroup",
    "Choice",
    "ConnectionError",
    "Control",
    "ExtensionInfo",
    "Extensions",
    "host_identifier",
    "PROTOCOL_FINGERPRINT",
    "is_local_host",
    "local_host_fingerprint",
    "ProtocolMismatchError",
    "ControlKind",
    "DebuggerError",
    "DEFAULT_GRPC_PORT",
    "DiscError",
    "DispatchResult",
    "EconetError",
    "EconetEvent",
    "EconetFrameInfo",
    "EconetStatus",
    "EconetTransportAdapter",
    "ExtensionAdapter",
    "PeripheralExtensionAdapter",
    "ExtensionAdapterNotInstalledError",
    "ExtensionAmbiguousError",
    "ExtensionError",
    "ExtensionNotLoadedError",
    "Group",
    "HandshakeStatus",
    "Indicator",
    "IndicatorState",
    "Label",
    "MachineIdentity",
    "MemoryAccessError",
    "ParameterSchemaInfo",
    "Provenance",
    "ServerNotFoundError",
    "ServerStartupError",
    "ServerStatus",
    "ServerStatusEvent",
    "ShutdownConditionStatus",
    "ShutdownMode",
    "ShutdownResponse",
    "SourceEncoding",
    "StorageDevice",
    "StorageKind",
    "SubscriptionHandle",
    "TextInput",
    "TimeoutError",
    "Toggle",
    "TransportInfo",
    "View",
    "__version__",
]
