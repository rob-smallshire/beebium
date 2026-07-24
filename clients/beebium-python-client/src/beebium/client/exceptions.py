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

"""Exception hierarchy for the beebium client."""


class BeebiumError(Exception):
    """Base exception for all beebium client errors."""

    pass


class ConnectionError(BeebiumError):
    """Failed to connect to the beebium server."""

    pass


class ProtocolMismatchError(ConnectionError):
    """The server's wire protocol does not match this client's.

    Raised at connect time when the server's protocol fingerprint differs from
    the client's compiled-in fingerprint. Install a server and client built from
    the same protocol.
    """

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


class InvalidConditionError(DebuggerError):
    """A breakpoint or watchpoint condition expression is invalid."""

    pass


class MemoryAccessError(BeebiumError):
    """A memory read or write operation failed."""

    pass


class TimeoutError(BeebiumError):
    """An operation timed out."""

    pass


class ScreenExpectTimeout(TimeoutError):
    """Expected screen text did not appear within the timeout (see Beebium.expect)."""

    pass


class DiscError(BeebiumError):
    """A disc operation failed."""

    pass


class EconetError(BeebiumError):
    """An Econet operation failed."""

    pass


class SerialError(BeebiumError):
    """A serial port operation failed."""

    pass


class ExtensionError(BeebiumError):
    """A peripheral-extension client-adapter operation failed."""

    pass


class ExtensionNotLoadedError(ExtensionError):
    """The requested extension is not loaded on the connected server.

    Raised by the extension bridge (``bbc.extensions[...]`` / ``Adapter.attach``)
    when no loaded extension matches the requested name or adapter type.
    """

    pass


class ExtensionAmbiguousError(ExtensionError):
    """More than one loaded extension matches a name/type request.

    Raised by the extension/transport bridges when a type or name key matches
    several loaded instances of the same kind. Address one by its instance id
    (``bbc.extensions["<id>"]`` / ``bbc.transport["<id>"]``) to disambiguate.
    """

    pass


class ExtensionAdapterNotInstalledError(ExtensionError):
    """No client adapter is installed for a loaded extension.

    Raised when the server has loaded an extension but no ``beebium.ext``
    adapter is registered to drive it (e.g. its adapter distribution is not
    installed). The extension is still visible via ``bbc.extensions.loaded``.
    """

    pass
