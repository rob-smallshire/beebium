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

"""Disc-insert outcomes surfaced to the client, against a real server.

The durable guard for the public bug report (Stardot): a disc image whose path
contains a space failed to open because the client sends a file:// URL with the
space percent-encoded (%20) and the server did not decode it. These drive the
real gRPC path so the exact failure the front end shows -- and, crucially, that
the server's reason travels back intact -- is pinned.

Fixtures are created in a tmp directory whose own name contains a space, so the
space is in the PATH, never in a committed filename.
"""

from __future__ import annotations

import shutil
import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.disc import DiscError
from beebium.client.exceptions import ServerNotFoundError


@pytest.fixture(scope="module")
def genuine_ssd_filepath() -> Path:
    """A committed 200K SSD; its contents do not matter, only that it loads."""
    repo_root = Path(__file__).parent.parent.parent.parent
    path = repo_root / "tests" / "assets" / "discs" / "Disc001-CylonAttackAFSTD.ssd"
    if not path.exists():
        pytest.skip(f"Test disc image not found: {path}")
    return path


@pytest.fixture
def spaced_dirpath(tmp_path: Path) -> Path:
    """A directory whose name contains a space, for building spaced paths."""
    d = tmp_path / "disc images with spaces"
    d.mkdir()
    return d


@pytest.fixture
def bbc_disc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Iterator[Beebium]:
    """A Model B with a 1770 disc controller and empty drives."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _wait_until_loaded(bbc: Beebium, drive: int, timeout: float = 5.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if bbc.disc.drive(drive).is_loaded:
            return True
        time.sleep(0.05)
    return False


def test_disc_at_a_spaced_path_loads(
    bbc_disc: Beebium, spaced_dirpath: Path, genuine_ssd_filepath: Path
) -> None:
    """The Stardot regression: a real disc in a spaced path must load.

    The client sends file:///.../My%20Disc.ssd; before the fix the server did
    not decode %20 and reported "Cannot open".
    """
    spaced = spaced_dirpath / "My Disc.ssd"
    shutil.copyfile(genuine_ssd_filepath, spaced)

    metadata = bbc_disc.disc.drive(0).insert(spaced)

    assert _wait_until_loaded(bbc_disc, 0)
    assert bbc_disc.disc.drive(0).name  # a non-empty disc name is reported
    assert metadata is not None


def test_non_disc_file_reports_unrecognised_with_size(
    bbc_disc: Beebium, spaced_dirpath: Path
) -> None:
    """A non-disc file yields the "Unrecognised" reason, carrying size=, over gRPC.

    Pins that the server's reason (which the UI now shows) travels back intact.
    """
    bogus = spaced_dirpath / "not a disc.ssd"
    # 3000 bytes: non-empty but not a 256-byte multiple, so no handler detects it.
    bogus.write_bytes(bytes([0x89]) + b"\xee" * 2999)

    with pytest.raises(DiscError) as excinfo:
        bbc_disc.disc.drive(0).insert(bogus)

    message = str(excinfo.value)
    assert "Unrecognised disc image format" in message
    assert "size=" in message


def test_missing_path_reports_cannot_open(
    bbc_disc: Beebium, spaced_dirpath: Path
) -> None:
    """A path that does not exist yields "Cannot open disc image"."""
    missing = spaced_dirpath / "no such disc.ssd"  # never created

    with pytest.raises(DiscError) as excinfo:
        bbc_disc.disc.drive(0).insert(missing)

    assert "Cannot open disc image" in str(excinfo.value)


def test_empty_disc_reports_empty(
    bbc_disc: Beebium, spaced_dirpath: Path
) -> None:
    """A zero-byte disc image yields "Empty disc image"."""
    empty = spaced_dirpath / "empty.ssd"
    empty.write_bytes(b"")

    with pytest.raises(DiscError) as excinfo:
        bbc_disc.disc.drive(0).insert(empty)

    assert "Empty disc image" in str(excinfo.value)
