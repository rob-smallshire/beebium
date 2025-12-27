# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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
    from beebium import Beebium

    # Connect to an existing server
    with Beebium.connect() as bbc:
        bbc.debugger.stop()
        print(f"PC = ${bbc.cpu.pc:04X}")

    # Launch and manage a server
    with Beebium.launch(mos_filepath="acorn-mos_1_20.rom") as bbc:
        bbc.keyboard.type("PRINT 42")
        bbc.keyboard.press_return()
"""

from beebium.client import Beebium
from beebium.exceptions import (
    BeebiumError,
    ConnectionError,
    DebuggerError,
    MemoryAccessError,
    ServerNotFoundError,
    ServerStartupError,
    TimeoutError,
)

__version__ = "0.1.0"

# Default gRPC port for beebium servers (0xBEEB = 48875)
DEFAULT_GRPC_PORT = 0xBEEB

__all__ = [
    "Beebium",
    "BeebiumError",
    "ConnectionError",
    "DebuggerError",
    "DEFAULT_GRPC_PORT",
    "MemoryAccessError",
    "ServerNotFoundError",
    "ServerStartupError",
    "TimeoutError",
    "__version__",
]
