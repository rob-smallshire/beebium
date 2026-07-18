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

"""Protocol fingerprint handshake tests."""

from __future__ import annotations

import os
from pathlib import Path

import pytest

import beebium.client.client
from beebium.client import (
    PROTOCOL_FINGERPRINT,
    Beebium,
    ProtocolMismatchError,
    ServerNotFoundError,
)


def test_server_reports_matching_fingerprint(bbc: Beebium) -> None:
    """The server reports the same protocol fingerprint the client carries."""
    assert bbc.system.protocol_fingerprint == PROTOCOL_FINGERPRINT


def test_mismatched_client_is_rejected_at_connect(
    monkeypatch: pytest.MonkeyPatch,
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> None:
    """A client whose fingerprint differs from the server's is refused."""
    # Patch the name in the module that reads it. _verify_protocol compares
    # against client.py's own global, bound by its `from ... import` at import
    # time, so rebinding the re-export on the beebium.client package leaves the
    # comparison untouched.
    monkeypatch.setattr(
        beebium.client.client, "PROTOCOL_FINGERPRINT", "mismatched-fingerprint"
    )
    # This needs a real server (like the other integration tests); skip when one
    # is not available, e.g. in the unit-test job that builds no server.
    try:
        with pytest.raises(ProtocolMismatchError):
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server_filepath=beebium_server_filepath,
            ):
                pass
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_server_reports_its_executable_path(bbc: Beebium) -> None:
    """The server names its own binary, so a mismatch can be diagnosed."""
    executable = bbc.system.executable_path
    assert executable, "server did not report an executable path"
    assert Path(executable).is_file()
    assert Path(executable).name.startswith("beebium-model-b")


def test_mismatch_message_names_the_server_binary(
    monkeypatch: pytest.MonkeyPatch,
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> None:
    """The refusal says which binary it reached, not merely two hashes.

    Comparing hex strings cannot distinguish a stale server left running, an
    installed server shadowing a development build, or one variant of a
    multi-server bundle rebuilt while its siblings were not. The path can.
    """
    monkeypatch.setattr(
        beebium.client.client, "PROTOCOL_FINGERPRINT", "mismatched-fingerprint"
    )
    try:
        with pytest.raises(ProtocolMismatchError) as excinfo:
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server_filepath=beebium_server_filepath,
            ):
                pass
    except ServerNotFoundError as e:
        pytest.skip(str(e))

    message = str(excinfo.value)
    assert "beebium-model-b" in message
    # The path, not just the executable name.
    assert os.sep in message
