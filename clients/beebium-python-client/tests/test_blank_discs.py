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

"""Blank discs from the server's templates (#25), against a real server.

The client lists the templates, has the server copy one into place, inserts
the copy, and the guest's own *CAT shows an empty disc.
"""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.disc import DiscError, DiscErrorKind
from beebium.client.exceptions import ServerNotFoundError

EXPECTED_LABELS = [
    "Acorn ADFS L 640K",
    "Acorn ADFS M 320K",
    "Acorn ADFS S 160K",
    "Acorn DFS 40T DS 200K",
    "Acorn DFS 40T SS 100K",
    "Acorn DFS 80T DS 400K",
    "Acorn DFS 80T SS 200K",
]


@pytest.fixture
def bbc_disc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Iterator[Beebium]:
    """A Model B with a 1770 disc controller, DFS 2.26 and empty drives."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _catalogue_lines(screen: str) -> list[str]:
    """The non-blank lines from the *CAT command to the prompt after it."""
    lines = [line.rstrip() for line in screen.splitlines() if line.strip()]
    start = max(i for i, line in enumerate(lines) if line.strip() == ">*CAT")
    return lines[start + 1:]


def test_a_blank_disc_from_a_template_catalogues_as_empty(
    bbc_disc: Beebium, tmp_path: Path
) -> None:
    blank_discs = bbc_disc.disc.list_blank_discs()
    assert [disc.label for disc in blank_discs] == EXPECTED_LABELS
    template = next(d for d in blank_discs if d.label == "Acorn DFS 80T SS 200K")
    assert template.filename == "Acorn DFS 80T SS 200K.ssd"
    assert template.kind == "floppy"
    assert template.size_bytes == 204800

    created = bbc_disc.disc.create_disc(template.label, tmp_path / "blank.ssd")
    assert created == tmp_path / "blank.ssd"
    assert created.stat().st_size == template.size_bytes

    metadata = bbc_disc.disc.drive(0).insert(created)
    assert not metadata.write_protected

    bbc_disc.wait_until_ready()
    bbc_disc.keyboard.type("*CAT\r")
    bbc_disc.expect("Lib.", timeout=20.0)
    # The catalogue's header, then the prompt: no files.
    lines = _catalogue_lines(bbc_disc.video.screen_text().text)
    assert "Option 0" in "\n".join(lines), lines
    assert lines[-1].strip() == ">", lines
    assert any("Lib." in line for line in lines[-2:]), lines


def test_create_disc_refuses_an_existing_destination_unless_overwrite(
    bbc_disc: Beebium, tmp_path: Path
) -> None:
    destination = tmp_path / "existing.ads"
    destination.write_bytes(b"keep")

    with pytest.raises(DiscError) as excinfo:
        bbc_disc.disc.create_disc("Acorn ADFS S 160K", destination)
    assert excinfo.value.kind == DiscErrorKind.DESTINATION_EXISTS
    assert destination.read_bytes() == b"keep"

    bbc_disc.disc.create_disc("Acorn ADFS S 160K", destination, overwrite=True)
    assert destination.stat().st_size == 163840


def test_create_disc_reports_an_unknown_label(bbc_disc: Beebium, tmp_path: Path) -> None:
    with pytest.raises(DiscError) as excinfo:
        bbc_disc.disc.create_disc("Acorn DFS 81T", tmp_path / "new.ssd")
    assert excinfo.value.kind == DiscErrorKind.UNKNOWN_LABEL
    assert "no blank disc named 'Acorn DFS 81T'" in str(excinfo.value)


def test_create_disc_adds_the_template_extension_to_a_bare_destination(
    bbc_disc: Beebium, tmp_path: Path
) -> None:
    created = bbc_disc.disc.create_disc("Acorn DFS 80T DS 400K", tmp_path / "Work")
    assert created == tmp_path / "Work.dsd"
    assert created.stat().st_size == 409600
    assert not (tmp_path / "Work").exists()


def test_create_disc_refuses_another_formats_extension(bbc_disc: Beebium, tmp_path: Path) -> None:
    """A double-sided DFS template copied to a .ssd would not load, so it is refused."""
    destination = tmp_path / "blank-Acorn-DFS-80T-DS-400K.ssd"
    with pytest.raises(DiscError) as excinfo:
        bbc_disc.disc.create_disc("Acorn DFS 80T DS 400K", destination, overwrite=True)
    assert excinfo.value.kind == DiscErrorKind.WRONG_EXTENSION
    assert f"destination must end in .dsd: {destination}" in str(excinfo.value)
    assert not destination.exists()
