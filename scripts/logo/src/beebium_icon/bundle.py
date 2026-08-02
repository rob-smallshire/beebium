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

"""Platform icon bundles.

One target per platform, each naming the sizes that platform asks for, the
frame style that looks native there, and the layout of files on disk.  Every
size in a bundle is rendered from the configuration at that size, so the level
of detail is chosen per size rather than smeared out of one large image.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from beebium_icon.config import SQUIRCLE, TILE, IconConfig
from beebium_icon.raster import Raster, build_icns, build_ico, render_rasters
from beebium_icon.svg import render_svg


@dataclass(frozen=True)
class Target:
    """A platform's icon requirements, and how to lay them out on disk."""

    name: str
    style: str
    sizes: tuple[int, ...]
    description: str
    builder: Callable[[IconConfig, tuple[Raster, ...], Path], list[Path]]



# The Xcode asset catalogue names each slot by nominal size and scale.
APPICONSET_SLOTS: tuple[tuple[int, int], ...] = (
    (16, 1),
    (16, 2),
    (32, 1),
    (32, 2),
    (128, 1),
    (128, 2),
    (256, 1),
    (256, 2),
    (512, 1),
    (512, 2),
)

# The nominal SVG size: large enough that rounding in the layout is invisible
# when the document is scaled.
SCALABLE_SIZE = 1024


def build_target(config: IconConfig, target: Target, out_dirpath: Path) -> list[Path]:
    """Render one platform's icons beneath out_dirpath, returning what was written."""
    styled = config.with_style(target.style)
    rasters = render_rasters(styled, target.sizes)
    target_dirpath = out_dirpath / target.name
    target_dirpath.mkdir(parents=True, exist_ok=True)
    return target.builder(styled, rasters, target_dirpath)


def _write(filepath: Path, data: bytes) -> Path:
    filepath.parent.mkdir(parents=True, exist_ok=True)
    filepath.write_bytes(data)
    return filepath


def _by_size(rasters: tuple[Raster, ...]) -> dict[int, Raster]:
    return {raster.size: raster for raster in rasters}


def _build_macos(
    config: IconConfig, rasters: tuple[Raster, ...], target_dirpath: Path
) -> list[Path]:
    by_size = _by_size(rasters)
    written = [
        _write(
            target_dirpath / f"{config.name.capitalize()}.icns",
            build_icns(rasters),
        )
    ]
    appiconset_dirpath = target_dirpath / "AppIcon.appiconset"
    images = []
    for nominal, scale in APPICONSET_SLOTS:
        pixels = nominal * scale
        suffix = "" if scale == 1 else "@2x"
        filename = f"icon_{nominal}x{nominal}{suffix}.png"
        written.append(_write(appiconset_dirpath / filename, by_size[pixels].png))
        images.append(
            {
                "filename": filename,
                "idiom": "mac",
                "scale": f"{scale}x",
                "size": f"{nominal}x{nominal}",
            }
        )
    contents = {"images": images, "info": {"author": "beebium-icon", "version": 1}}
    written.append(
        _write(
            appiconset_dirpath / "Contents.json",
            (json.dumps(contents, indent=2) + "\n").encode("utf-8"),
        )
    )
    return written


def _build_windows(
    config: IconConfig, rasters: tuple[Raster, ...], target_dirpath: Path
) -> list[Path]:
    written = [_write(target_dirpath / f"{config.name}.ico", build_ico(rasters))]
    for raster in rasters:
        written.append(
            _write(target_dirpath / "png" / f"{config.name}-{raster.size}.png", raster.png)
        )
    return written


def _build_linux(
    config: IconConfig, rasters: tuple[Raster, ...], target_dirpath: Path
) -> list[Path]:
    written = []
    for raster in rasters:
        written.append(
            _write(
                target_dirpath
                / "hicolor"
                / f"{raster.size}x{raster.size}"
                / "apps"
                / f"{config.name}.png",
                raster.png,
            )
        )
    written.append(
        _write(
            target_dirpath / "hicolor" / "scalable" / "apps" / f"{config.name}.svg",
            render_svg(config, SCALABLE_SIZE).encode("utf-8"),
        )
    )
    return written


def _build_web(
    config: IconConfig, rasters: tuple[Raster, ...], target_dirpath: Path
) -> list[Path]:
    by_size = _by_size(rasters)
    written = [
        _write(
            target_dirpath / "favicon.ico",
            build_ico(tuple(by_size[size] for size in (16, 32, 48))),
        ),
        _write(target_dirpath / "apple-touch-icon.png", by_size[180].png),
        _write(
            target_dirpath / f"{config.name}.svg",
            render_svg(config, SCALABLE_SIZE).encode("utf-8"),
        ),
    ]
    for size in (192, 512):
        written.append(_write(target_dirpath / f"icon-{size}.png", by_size[size].png))
    return written


def install_appiconset(built_dirpath: Path, appiconset_dirpath: Path) -> list[Path]:
    """Copy a generated asset catalogue over the one in the macOS front end."""
    source_dirpath = built_dirpath / "macos" / "AppIcon.appiconset"
    if not source_dirpath.is_dir():
        raise FileNotFoundError(f"no generated asset catalogue at {source_dirpath}")
    appiconset_dirpath.mkdir(parents=True, exist_ok=True)
    for stale_filepath in appiconset_dirpath.glob("icon_*.png"):
        stale_filepath.unlink()
    written = []
    for source_filepath in sorted(source_dirpath.iterdir()):
        destination_filepath = appiconset_dirpath / source_filepath.name
        destination_filepath.write_bytes(source_filepath.read_bytes())
        written.append(destination_filepath)
    return written


TARGETS: dict[str, Target] = {
    "macos": Target(
        "macos",
        SQUIRCLE,
        (16, 32, 64, 128, 256, 512, 1024),
        "Xcode asset catalogue and an ICNS archive",
        _build_macos,
    ),
    "windows": Target(
        "windows",
        TILE,
        (16, 24, 32, 48, 64, 128, 256),
        "multi-resolution ICO",
        _build_windows,
    ),
    "linux": Target(
        "linux",
        TILE,
        (16, 22, 24, 32, 48, 64, 128, 256, 512),
        "hicolor icon theme tree",
        _build_linux,
    ),
    "web": Target(
        "web",
        TILE,
        (16, 32, 48, 180, 192, 512),
        "favicon, touch icon and scalable SVG",
        _build_web,
    ),
}
