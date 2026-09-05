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

"""Rasterising, ICNS and ICO."""

from __future__ import annotations

import io
import struct

import pytest
from PIL import Image

from beebium_icon.raster import (
    ICNS_TYPES,
    Raster,
    build_icns,
    build_ico,
    render_png,
    render_rasters,
)

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


@pytest.fixture(scope="module")
def rasters(config):
    return render_rasters(config, (16, 32, 64, 128, 256))


def icns_entries(archive: bytes) -> list[tuple[str, int]]:
    assert archive[:4] == b"icns"
    assert struct.unpack(">I", archive[4:8])[0] == len(archive)
    entries = []
    offset = 8
    while offset < len(archive):
        entry_type = archive[offset : offset + 4].decode("ascii")
        length = struct.unpack(">I", archive[offset + 4 : offset + 8])[0]
        assert length >= 8
        entries.append((entry_type, length))
        offset += length
    assert offset == len(archive)
    return entries


def test_a_rendered_png_has_the_size_asked_for(config):
    png = render_png(config, 128)
    assert png.startswith(PNG_MAGIC)
    assert Image.open(io.BytesIO(png)).size == (128, 128)


def test_a_rendered_png_has_transparent_corners(config):
    """The icon is rounded; the canvas corners must not be painted."""
    image = Image.open(io.BytesIO(render_png(config, 512))).convert("RGBA")
    assert image.getpixel((0, 0))[3] == 0


def test_the_icon_is_painted_in_the_middle(config):
    image = Image.open(io.BytesIO(render_png(config, 512))).convert("RGBA")
    assert image.getpixel((256, 256))[3] == 255


def test_rasters_come_back_sorted_and_deduplicated(config):
    rendered = render_rasters(config, (64, 16, 64))
    assert [raster.size for raster in rendered] == [16, 64]


def test_icns_holds_every_size_it_was_given(rasters):
    entries = icns_entries(build_icns(rasters))
    expected = sum(len(ICNS_TYPES[raster.size]) for raster in rasters)
    assert len(entries) == expected


def test_icns_lengths_include_the_entry_header(rasters):
    archive = build_icns(rasters)
    by_type = dict(icns_entries(archive))
    for raster in rasters:
        for entry_type in ICNS_TYPES[raster.size]:
            assert by_type[entry_type] == len(raster.png) + 8


def test_icns_uses_no_type_that_macos_reads_back_at_another_size():
    """icp6 is read back as 48x48, so 64px must travel as ic12 alone."""
    assert "icp6" not in {name for names in ICNS_TYPES.values() for name in names}


def test_icns_without_a_usable_size_is_refused(config):
    with pytest.raises(ValueError, match="no ICNS-compatible sizes"):
        build_icns(render_rasters(config, (24,)))


def test_ico_is_readable_and_carries_every_size(rasters):
    data = build_ico(rasters)
    image = Image.open(io.BytesIO(data))
    assert image.format == "ICO"
    assert {size[0] for size in image.info["sizes"]} == {r.size for r in rasters}


def test_ico_directory_offsets_point_at_the_images(rasters):
    data = build_ico(rasters)
    reserved, kind, count = struct.unpack("<HHH", data[:6])
    assert (reserved, kind) == (0, 1)
    assert count == len(rasters)
    for index in range(count):
        entry = data[6 + index * 16 : 22 + index * 16]
        length, offset = struct.unpack("<II", entry[8:16])
        assert data[offset : offset + 8] == PNG_MAGIC
        assert len(data[offset : offset + length]) == length


def test_ico_records_256_as_zero(config):
    data = build_ico(render_rasters(config, (256,)))
    assert data[6] == 0 and data[7] == 0


def test_ico_refuses_an_oversized_image(config):
    with pytest.raises(ValueError, match="at most 256px"):
        build_ico(render_rasters(config, (512,)))


def test_ico_refuses_to_be_empty():
    with pytest.raises(ValueError, match="at least one image"):
        build_ico(())


def test_each_size_is_rendered_rather_than_downsampled(config):
    """A 16px icon is resampled from the artwork, not from the 512px icon."""
    small = Raster(16, render_png(config, 16))
    large = Raster(512, render_png(config, 512))
    small_image = Image.open(io.BytesIO(small.png)).convert("RGBA")
    downsampled = (
        Image.open(io.BytesIO(large.png)).convert("RGBA").resize((16, 16), Image.LANCZOS)
    )
    assert small_image.tobytes() != downsampled.tobytes()
