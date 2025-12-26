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

"""Exception hierarchy for the beebium client."""


class BeebiumError(Exception):
    """Base exception for all beebium client errors."""

    pass


class ConnectionError(BeebiumError):
    """Failed to connect to the beebium server."""

    pass


class ServerStartupError(BeebiumError):
    """The beebium-server process failed to start."""

    pass


class ServerNotFoundError(BeebiumError):
    """The beebium-server executable could not be found."""

    pass


class DebuggerError(BeebiumError):
    """A debugger operation failed."""

    pass


class MemoryAccessError(BeebiumError):
    """A memory read or write operation failed."""

    pass


class TimeoutError(BeebiumError):
    """An operation timed out."""

    pass
