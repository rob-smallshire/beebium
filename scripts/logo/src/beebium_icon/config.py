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

"""Declarative configuration for an icon.

A configuration is a TOML file describing the palette, the tile geometry, the
fonts used for each typographic role, the annotation content, and the
level-of-detail thresholds at which each annotation appears.  Everything the
generator draws comes from here, so a machine variant - a Z80 second
processor, say - is a new TOML file rather than new code.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field, fields, replace
from pathlib import Path
from typing import Any

# Typographic roles.  SYMBOL and NAME carry the identity of the element and
# take the display face; TECHNICAL carries the specifications and takes a
# second, more mechanical face, in the manner of a real periodic table.
SYMBOL = "symbol"
NAME = "name"
TECHNICAL = "technical"
ROLES = (SYMBOL, NAME, TECHNICAL)

# Frame styles.
SQUIRCLE = "squircle"
TILE = "tile"
FRAME_STYLES = (SQUIRCLE, TILE)

# How the isotope row is written.  UNIT sets the RAM size with its unit,
# 16K; NUCLIDE writes each one as a real nuclide, with the size as a raised
# mass number before the symbol, as chemistry writes an isotope.
ISOTOPE_UNIT = "unit"
ISOTOPE_NUCLIDE = "nuclide"
ISOTOPE_STYLES = (ISOTOPE_UNIT, ISOTOPE_NUCLIDE)

# Vertical fit modes for a text run.
FIT_INK = "ink"
FIT_CAP = "cap"
FIT_MODES = (FIT_INK, FIT_CAP)


class ConfigError(ValueError):
    """Raised when a configuration file is malformed."""


# The vendored open-licensed faces, used when a configuration names none.
FONTS_DIRPATH = Path(__file__).resolve().parents[2] / "fonts"
DISPLAY_FONT_FILEPATH = FONTS_DIRPATH / "NunitoSans.ttf"
TECHNICAL_FONT_FILEPATH = FONTS_DIRPATH / "Barlow-Medium.ttf"


@dataclass(frozen=True)
class FontRole:
    """A font, at a weight, for one typographic role."""

    filepath: Path = DISPLAY_FONT_FILEPATH
    face_index: int = 0
    # Variable-font axis settings, e.g. {"wght": 1000}.  Ignored for static faces.
    variations: dict[str, float] = field(default_factory=dict)
    # Axis settings used at and below LevelOfDetail.small_size.  A heavy
    # weight fills in its own counters once a glyph is a few pixels tall, so
    # the small end of the ladder wants a lighter cut of the same face.
    small_variations: dict[str, float] = field(default_factory=dict)
    # Extra space between glyphs, in em.
    tracking: float = 0.0
    # How a run of this role is scaled to fill its block.
    fit: str = FIT_INK


@dataclass(frozen=True)
class Palette:
    """BBC Microcomputer beige, with Acorn-green typography."""

    face_top: str = "#F6ECD2"
    face_bottom: str = "#E8DAB4"
    frame: str = "#FBF4E1"
    ink: str = "#2E6B35"
    shadow: str = "#4A3B14"


@dataclass(frozen=True)
class Geometry:
    """Proportions of the tile, all relative to the canvas or the tile body."""

    style: str = SQUIRCLE
    # Tile body side length as a fraction of the canvas.  The macOS default
    # matches Apple's icon grid, which insets the body to leave room for the
    # shadow; the free-standing tile is inset less.
    body_ratio: float = 0.805
    # The free-standing tile has no icon grid to obey and sits nearer the edge.
    tile_body_ratio: float = 0.935
    # Below the size at which the shadow is drawn there is nothing for the
    # margin to hold, and every pixel of tile is worth having.
    unshadowed_body_ratio: float = 0.94
    # Corner radius as a fraction of the body side.
    corner_ratio: float = 0.225
    # Superellipse exponent for the squircle style.  2 is a circle-cornered
    # rounded rectangle; higher is squarer with continuous curvature.
    squircle_exponent: float = 5.0
    # Tile style only: width of the lighter outer band, as a fraction of the body.
    frame_ratio: float = 0.045
    # Green keyline drawn inside the face.
    keyline_inset: float = 0.062
    keyline_width: float = 0.009
    keyline_corner_ratio: float = 0.04
    # Content is inset from the body edge by this fraction of the body side.
    content_inset: float = 0.132
    # Once the keyline drops out, the content no longer has to clear it and
    # can use nearly the whole face; without this the symbol shrinks to
    # nothing at the sizes where it is the only thing left.
    bare_content_inset: float = 0.10
    # Drop shadow, suppressed below shadow_min_size where it only muddies.
    shadow_opacity: float = 0.30
    shadow_dy: float = 0.020
    shadow_blur: float = 0.018
    shadow_min_size: int = 64


@dataclass(frozen=True)
class Layout:
    """The vertical stack inside the content box.

    The header (atomic number, ground state) is pinned to the top of the content
    box.  The remaining blocks flow in a stack that is centred in whatever
    space is left, with heights apportioned by weight.  Gaps and weights share
    the same arbitrary unit; only their ratios matter.
    """

    header_height: float = 0.075  # fraction of the body side
    header_gap: float = 0.55  # fraction of the header height
    symbol_weight: float = 4.0
    name_weight: float = 1.05
    name_gap: float = 0.40
    isotopes_weight: float = 0.80
    isotopes_gap: float = 0.85
    energies_weight: float = 0.80
    energies_gap: float = 0.85
    constituents_weight: float = 0.70
    constituents_gap: float = 0.75
    # The symbol never grows past this fraction of the body side, so that
    # sparse levels of detail do not blow it up to fill the tile.  When the
    # symbol is all that is left, it is allowed to grow further: at 16px it
    # is the whole icon.
    max_symbol_ratio: float = 0.42
    solo_symbol_ratio: float = 0.62
    # Horizontal rules above the isotope and energy rows.  A NIST-style tile
    # rules only under the name; the Beebium tile rules both spec rows.
    isotopes_rule: bool = True
    energies_rule: bool = True
    constituents_rule: bool = True
    rule_width: float = 0.006
    rule_inset: float = 0.0


@dataclass(frozen=True)
class Scripts:
    """How raised and lowered text is drawn, in em of the surrounding size."""

    scale: float = 0.62
    superscript_rise: float = 0.40
    subscript_drop: float = 0.12


@dataclass(frozen=True)
class Content:
    """The words and numbers on the tile."""

    symbol: str = "Bb"
    name: str = "Beebium"
    atomic_number: str = "6502"
    # The top-right slot, which a NIST tile gives to the ground state.
    # Beebium's ground state is its word size, which stays true across the
    # isotopes and distinguishes a second processor when one arrives.
    ground_state: str = "8-bit"
    isotopes: tuple[str, ...] = ("16", "32", "64", "128")
    isotope_style: str = ISOTOPE_NUCLIDE
    # Appended to each isotope without a space in the UNIT style, as a unit
    # belongs to its number: 16K.  The energy suffix is a unit for the row.
    isotope_suffix: str = "K"
    energies: tuple[str, ...] = ("2", "3", "4")
    energy_suffix: str = "MHz"
    # The chips the machine is built from - the tile's analogue of the
    # constituent elements of a compound.  Empty by default; it is the finest
    # detail on the tile and only earns its place at the largest sizes.
    constituents: tuple[str, ...] = ()
    separator: str = " · "


@dataclass(frozen=True)
class LevelOfDetail:
    """Smallest rendered size, in pixels, at which each element appears."""

    # At and below this size, roles fall back to their small_variations.
    small_size: int = 24
    symbol: int = 0
    keyline: int = 0
    atomic_number: int = 64
    name: int = 128
    isotopes: int = 256
    ground_state: int = 512
    energies: int = 512
    constituents: int = 1024


@dataclass(frozen=True)
class IconConfig:
    """A complete icon description."""

    name: str = "beebium"
    palette: Palette = field(default_factory=Palette)
    geometry: Geometry = field(default_factory=Geometry)
    layout: Layout = field(default_factory=Layout)
    content: Content = field(default_factory=Content)
    scripts: Scripts = field(default_factory=Scripts)
    lod: LevelOfDetail = field(default_factory=LevelOfDetail)
    fonts: dict[str, FontRole] = field(
        default_factory=lambda: {role: FontRole() for role in ROLES}
    )

    def with_style(self, style: str) -> IconConfig:
        """Return a copy of this configuration using a different frame style."""
        if style not in FRAME_STYLES:
            raise ConfigError(
                f"unknown frame style {style!r}; expected one of {list(FRAME_STYLES)}"
            )
        return replace(self, geometry=replace(self.geometry, style=style))


def font_for_size(config: IconConfig, role: str, size: int) -> FontRole:
    """The font a role uses at a given rendered size."""
    spec = config.fonts[role]
    if size <= config.lod.small_size and spec.small_variations:
        return replace(spec, variations=spec.small_variations)
    return spec


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


def _build(cls: type, data: dict[str, Any], where: str) -> Any:
    """Instantiate a dataclass from a TOML table, rejecting unknown keys."""
    known = {f.name: f for f in fields(cls)}
    unknown = sorted(set(data) - set(known))
    if unknown:
        raise ConfigError(
            f"{where}: unknown key(s) {unknown}; expected any of {sorted(known)}"
        )
    kwargs: dict[str, Any] = {}
    for key, value in data.items():
        annotation = known[key].type
        if annotation == "tuple[str, ...]":
            if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
                raise ConfigError(f"{where}.{key}: expected a list of strings")
            kwargs[key] = tuple(value)
        else:
            kwargs[key] = _coerce(
                {"Path": Path, "float": float, "int": int, "str": str, "bool": bool}.get(
                    str(annotation), object
                ),
                value,
                f"{where}.{key}",
            )
    return cls(**kwargs)


DEFAULT_ROLE_FONT_FILEPATHS = {
    SYMBOL: DISPLAY_FONT_FILEPATH,
    NAME: DISPLAY_FONT_FILEPATH,
    TECHNICAL: TECHNICAL_FONT_FILEPATH,
}


def _build_font(
    data: dict[str, Any], where: str, base_dirpath: Path, default_filepath: Path
) -> FontRole:
    """Instantiate a FontRole, resolving its file relative to the config."""
    role = _build(FontRole, {"filepath": default_filepath, **data}, where)
    if role.fit not in FIT_MODES:
        raise ConfigError(f"{where}.fit: expected one of {list(FIT_MODES)}")
    filepath = role.filepath
    if not filepath.is_absolute():
        filepath = (base_dirpath / filepath).resolve()
    if not filepath.exists():
        raise ConfigError(f"{where}.filepath: no such font file: {filepath}")
    return replace(role, filepath=filepath)


def load_config(config_filepath: Path) -> IconConfig:
    """Load an icon configuration, filling unspecified values from the defaults."""
    config_filepath = Path(config_filepath)
    try:
        data = tomllib.loads(config_filepath.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as error:
        raise ConfigError(f"{config_filepath}: {error}") from error
    return config_from_dict(data, base_dirpath=config_filepath.parent)


def config_from_dict(data: dict[str, Any], base_dirpath: Path) -> IconConfig:
    """Build a configuration from an already-parsed TOML mapping."""
    sections = {
        "palette": Palette,
        "geometry": Geometry,
        "layout": Layout,
        "content": Content,
        "scripts": Scripts,
        "lod": LevelOfDetail,
    }
    kwargs: dict[str, Any] = {}
    for key, value in data.items():
        if key == "name":
            kwargs["name"] = _coerce(str, value, "name")
        elif key == "fonts":
            if not isinstance(value, dict):
                raise ConfigError("fonts: expected a table of typographic roles")
            unknown = sorted(set(value) - set(ROLES))
            if unknown:
                raise ConfigError(f"fonts: unknown role(s) {unknown}; expected {list(ROLES)}")
            kwargs["fonts"] = {
                role: _build_font(
                    value.get(role, {}),
                    f"fonts.{role}",
                    base_dirpath,
                    DEFAULT_ROLE_FONT_FILEPATHS[role],
                )
                for role in ROLES
            }
        elif key in sections:
            if not isinstance(value, dict):
                raise ConfigError(f"{key}: expected a table")
            kwargs[key] = _build(sections[key], value, key)
        else:
            raise ConfigError(
                f"unknown section {key!r}; expected any of "
                f"{['name', 'fonts', *sections]}"
            )
    if "fonts" not in kwargs:
        kwargs["fonts"] = {
            role: _build_font(
                {}, f"fonts.{role}", base_dirpath, DEFAULT_ROLE_FONT_FILEPATHS[role]
            )
            for role in ROLES
        }
    config = IconConfig(**kwargs)
    if config.content.isotope_style not in ISOTOPE_STYLES:
        raise ConfigError(
            f"content.isotope_style: expected one of {list(ISOTOPE_STYLES)}, "
            f"got {config.content.isotope_style!r}"
        )
    if config.geometry.style not in FRAME_STYLES:
        raise ConfigError(
            f"geometry.style: expected one of {list(FRAME_STYLES)}, "
            f"got {config.geometry.style!r}"
        )
    return config
