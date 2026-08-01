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

"""Identifying this host, and agreeing with the server about it.

The derivation has to match the server's byte for byte on every platform, so
the interesting test is the one that compares the two -- which needs a real
server and lives at the bottom of this file.
"""

from __future__ import annotations

import hashlib
import re
import sys
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.host import (
    host_identifier,
    is_local_host,
    local_host_fingerprint,
)

SHA256_HEX = re.compile(r"\A[0-9a-f]{64}\Z")


class TestHostIdentifier:
    def test_this_platform_identifies_itself(self) -> None:
        # All three supported platforms have somewhere to look. A None here
        # means the lookup broke, not that the OS declined.
        assert host_identifier() is not None

    def test_identifier_is_stable(self) -> None:
        assert host_identifier() == host_identifier()

    @pytest.mark.skipif(sys.platform != "darwin", reason="macOS host UUID format")
    def test_macos_identifier_is_a_lowercase_hyphenated_uuid(self) -> None:
        # The server renders it with uuid_unparse_lower; anything else would
        # hash differently and make the two ends disagree.
        identifier = host_identifier()
        assert identifier is not None
        assert re.fullmatch(r"[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}", identifier)

    @pytest.mark.skipif(sys.platform != "linux", reason="Linux machine-id format")
    def test_linux_identifier_has_no_trailing_newline(self) -> None:
        identifier = host_identifier()
        assert identifier is not None
        assert identifier == identifier.strip()


class TestLocalHostFingerprint:
    def test_is_a_sha256_digest(self) -> None:
        fingerprint = local_host_fingerprint()
        assert fingerprint is not None
        assert SHA256_HEX.fullmatch(fingerprint)

    def test_is_the_domain_separated_hash_of_the_identifier(self) -> None:
        identifier = host_identifier()
        assert identifier is not None
        expected = hashlib.sha256(
            ("beebium-host-v1:" + identifier).encode("utf-8")
        ).hexdigest()
        assert local_host_fingerprint() == expected

    def test_is_stable(self) -> None:
        assert local_host_fingerprint() == local_host_fingerprint()


class TestIsLocalHost:
    def test_this_host_matches_itself(self) -> None:
        fingerprint = local_host_fingerprint()
        assert fingerprint is not None
        assert is_local_host(fingerprint)

    def test_another_host_does_not_match(self) -> None:
        assert not is_local_host("a" * 64)

    def test_unknown_is_not_a_match(self) -> None:
        # Two unknowns are not equal: reading them as equal would licence
        # precisely the path exchanges that break.
        assert not is_local_host("")


@pytest.fixture
def bbc_plain(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Iterator[Beebium]:
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            startup_timeout=20.0,
        ) as bbc:
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_client_and_server_agree_on_this_host(bbc_plain: Beebium) -> None:
    """The two derivations meet.

    This is the test that matters: the C++ and Python recipes are written
    separately and must produce the same string, or every server would look
    as though it were somewhere else.
    """
    assert bbc_plain.system.host_fingerprint == local_host_fingerprint()


def test_a_locally_launched_server_is_local(bbc_plain: Beebium) -> None:
    assert bbc_plain.system.is_local
