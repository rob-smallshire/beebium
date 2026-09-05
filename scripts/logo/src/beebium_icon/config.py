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

"""Declarative configuration for the icon.

A configuration is a TOML file naming the source artwork and the proportions
of the two shapes it is cut to.  Everything the packager does comes from here,
so a change of artwork or of corner radius is an edit to a file rather than to
the code.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field, fields, replace
from pathlib import Path
from typing import Any

# The shapes the artwork is cut to.  SQUIRCLE is Apple's continuous-curvature
# body on its icon grid; ROUNDED is the gentler radius that Windows and Linux
# expect, and a corner_ratio of zero makes it a plain square.
SQUIRCLE = "squircle"
ROUNDED = "rounded"
SHAPES = (SQUIRCLE, ROUNDED)


class ConfigError(ValueError):
    """Raised when a configuration file is malformed."""


DEFAULT_ARTWORK_FILEPATH = (
    Path(__file__).resolve().parents[2] / "the-shape-of-beebium.png"
)


@dataclass(frozen=True)
class Artwork:
    """The source image, and how it is resampled."""

    filepath: Path = DEFAULT_ARTWORK_FILEPATH
    # Lanczos softens a photograph as it shrinks it.  Below this reduction the
    # loss is worth correcting; above it, sharpening only adds crunch.
    sharpen_below_scale: float = 0.5
    sharpen_percent: int = 90


@dataclass(frozen=True)
class Squircle:
    """Apple's icon grid: a superellipse body inset to leave room for a shadow."""

    body_ratio: float = 0.805
    # With no shadow to hold, the margin is wasted, and at 16px every pixel of
    # artwork counts.
    unshadowed_body_ratio: float = 0.94
    corner_ratio: float = 0.225
    # 2 is a circle-cornered rounded rectangle; higher is squarer with
    # continuous curvature.
    exponent: float = 5.0


@dataclass(frozen=True)
class Rounded:
    """A conventional rounded square, filling the canvas."""

    body_ratio: float = 1.0
    corner_ratio: float = 0.18


@dataclass(frozen=True)
class Shadow:
    """The drop shadow beneath the squircle body.

    Only the squircle wears one: it is a property of Apple's icon grid, which
    reserves the margin for it.  A Windows or Linux icon fills its canvas and
    has nowhere to put it.
    """

    colour: str = "#241A06"
    opacity: float = 0.30
    dy: float = 0.020
    blur: float = 0.018
    # Below this size the shadow costs more pixels than it earns.
    min_size: int = 64


@dataclass(frozen=True)
class IconConfig:
    """A complete icon description."""

    name: str = "beebium"
    shape: str = SQUIRCLE
    artwork: Artwork = field(default_factory=Artwork)
    squircle: Squircle = field(default_factory=Squircle)
    rounded: Rounded = field(default_factory=Rounded)
    shadow: Shadow = field(default_factory=Shadow)

    def with_shape(self, shape: str) -> IconConfig:
        """Return a copy of this configuration cut to a different shape."""
        if shape not in SHAPES:
            raise ConfigError(
                f"unknown shape {shape!r}; expected one of {list(SHAPES)}"
            )
        return replace(self, shape=shape)


def _coerce(target: type, value: Any, where: str) -> Any:
    """Convert a TOML scalar to the type a dataclass field declares."""
    if target is Path:
        return Path(str(value))
    if target is bool:
        if not isinstance(value, bool):
            raise ConfigError(f"{where}: expected true or false, got {value!r}")
        return value
    if target is float:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ConfigError(f"{where}: expected a number, got {value!r}")
        return float(value)
    if target is int:
        if isinstance(value, bool) or not isinstance(value, int):
            raise ConfigError(f"{where}: expected an integer, got {value!r}")
        return value
    if target is str:
        if not isinstance(value, str):
            raise ConfigError(f"{where}: expected a string, got {value!r}")
        return value
    return value


_TYPES = {"Path": Path, "float": float, "int": int, "str": str, "bool": bool}


def _build(cls: type, data: dict[str, Any], where: str) -> Any:
    """Instantiate a dataclass from a TOML table, rejecting unknown keys."""
    known = {f.name: f for f in fields(cls)}
    unknown = sorted(set(data) - set(known))
    if unknown:
        raise ConfigError(
            f"{where}: unknown key(s) {unknown}; expected any of {sorted(known)}"
        )
    return cls(
        **{
            key: _coerce(_TYPES.get(str(known[key].type), object), value, f"{where}.{key}")
            for key, value in data.items()
        }
    )


def load_config(config_filepath: Path) -> IconConfig:
    """Load a configuration, filling unspecified values from the defaults."""
    config_filepath = Path(config_filepath)
    try:
        data = tomllib.loads(config_filepath.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as error:
        raise ConfigError(f"{config_filepath}: {error}") from error
    return config_from_dict(data, base_dirpath=config_filepath.parent)


def config_from_dict(data: dict[str, Any], base_dirpath: Path) -> IconConfig:
    """Build a configuration from an already-parsed TOML mapping."""
    sections = {
        "artwork": Artwork,
        "squircle": Squircle,
        "rounded": Rounded,
        "shadow": Shadow,
    }
    kwargs: dict[str, Any] = {}
    for key, value in data.items():
        if key in ("name", "shape"):
            kwargs[key] = _coerce(str, value, key)
        elif key in sections:
            if not isinstance(value, dict):
                raise ConfigError(f"{key}: expected a table")
            kwargs[key] = _build(sections[key], value, key)
        else:
            raise ConfigError(
                f"unknown section {key!r}; expected any of "
                f"{['name', 'shape', *sections]}"
            )

    artwork = kwargs.get("artwork", Artwork())
    filepath = artwork.filepath
    if not filepath.is_absolute():
        filepath = (base_dirpath / filepath).resolve()
    if not filepath.exists():
        raise ConfigError(f"artwork.filepath: no such image: {filepath}")
    kwargs["artwork"] = replace(artwork, filepath=filepath)

    config = IconConfig(**kwargs)
    if config.shape not in SHAPES:
        raise ConfigError(
            f"shape: expected one of {list(SHAPES)}, got {config.shape!r}"
        )
    return config
