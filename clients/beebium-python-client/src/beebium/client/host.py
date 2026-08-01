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

"""Identifying this host, to compare against the one a server reports.

Any exchange of filesystem paths -- inserting a disc image by path, or acting
on a path the server reported -- depends on the two processes sharing a
filesystem. Network addresses cannot settle that: a client may reach a server
on its own machine over loopback, over that machine's LAN address, or through
a Bonjour ".local" name that resolves back to itself.

The recipe here mirrors ``src/service/include/beebium/service/HostFingerprint.hpp``
so that the two agree byte for byte. See ``docs/frontend-local-server-gating.md``.
"""

from __future__ import annotations

import ctypes
import functools
import hashlib
import sys
from pathlib import Path

# Domain separation, so this digest is only ever comparable with another host
# fingerprint. Part of the protocol: both ends must use this exact string.
_DOMAIN = "beebium-host-v1:"

# Where a Linux host records its identity. systemd's location first, with the
# older D-Bus one for systems that predate it or do not run systemd.
_LINUX_MACHINE_ID_FILEPATHS = (
    Path("/etc/machine-id"),
    Path("/var/lib/dbus/machine-id"),
)


def _darwin_host_identifier() -> str | None:
    """The host UUID, via gethostuuid(2).

    Matches the server's ``gethostuuid`` + ``uuid_unparse_lower``: lowercase
    and hyphenated.
    """
    try:
        libc = ctypes.CDLL(None)
        gethostuuid = libc.gethostuuid
    except (OSError, AttributeError):
        return None

    class _Timespec(ctypes.Structure):
        _fields_ = [("tv_sec", ctypes.c_long), ("tv_nsec", ctypes.c_long)]

    raw = (ctypes.c_ubyte * 16)()
    # A zero timeout means do not wait; the value is available immediately.
    no_wait = _Timespec(0, 0)
    if gethostuuid(raw, ctypes.byref(no_wait)) != 0:
        return None

    digits = bytes(raw).hex()
    return "-".join(
        (digits[0:8], digits[8:12], digits[12:16], digits[16:20], digits[20:32])
    )


def _linux_host_identifier() -> str | None:
    """The systemd machine-id, read as the server reads it: the first line."""
    for filepath in _LINUX_MACHINE_ID_FILEPATHS:
        try:
            with filepath.open(encoding="ascii") as f:
                identifier = f.readline().rstrip("\n")
        except OSError:
            continue
        if identifier:
            return identifier
    return None


def _windows_host_identifier() -> str | None:
    """The MachineGuid registry value.

    Read from the 64-bit view explicitly, so a 32-bit interpreter on a 64-bit
    Windows sees the same value the server does rather than being redirected
    into the WOW6432Node mirror.
    """
    import winreg  # Imported here: absent on other platforms.

    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"SOFTWARE\Microsoft\Cryptography",
            0,
            winreg.KEY_READ | winreg.KEY_WOW64_64KEY,
        ) as key:
            value, value_type = winreg.QueryValueEx(key, "MachineGuid")
    except OSError:
        return None

    if value_type != winreg.REG_SZ or not isinstance(value, str) or not value:
        return None
    return value


def host_identifier() -> str | None:
    """An identifier for this host, or None if the platform will not say.

    The same for every process on this host and different on any other. A
    container reports its own identity, which is the right answer: it has its
    own filesystem, so a path from outside it does not mean what the sender
    intended.
    """
    if sys.platform == "darwin":
        return _darwin_host_identifier()
    if sys.platform == "win32":
        return _windows_host_identifier()
    return _linux_host_identifier()


@functools.cache
def local_host_fingerprint() -> str | None:
    """This host's fingerprint, comparable with a server's.

    ``sha256(_DOMAIN + host_identifier())`` in lowercase hex, matching the
    server's derivation exactly.

    Returns None if this host will not identify itself, which callers must
    read as "unknown" rather than as matching another unknown.

    Cached: it cannot change while the process lives.
    """
    identifier = host_identifier()
    if identifier is None:
        return None
    return hashlib.sha256((_DOMAIN + identifier).encode("utf-8")).hexdigest()


def is_local_host(fingerprint: str) -> bool:
    """Whether a fingerprint reported by a server names this same host.

    An unknown value on either side is not a match. Treating two unknowns as
    equal would licence exactly the path exchanges that break, in the case
    where least is known about either end.
    """
    if not fingerprint:
        return False
    mine = local_host_fingerprint()
    return mine is not None and fingerprint == mine
