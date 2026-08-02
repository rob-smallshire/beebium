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

"""Configuration loading."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium_icon.config import (
    SQUIRCLE,
    SYMBOL,
    TILE,
    ConfigError,
    config_from_dict,
    font_for_size,
    load_config,
)

from conftest import CONFIGS_DIRPATH, DEFAULT_CONFIG_FILEPATH


def test_default_config_loads(config):
    assert config.name == "beebium"
    assert config.content.symbol == "Bb"
    assert config.geometry.style == SQUIRCLE


def test_every_role_resolves_to_an_existing_font(config):
    for role, spec in config.fonts.items():
        assert spec.filepath.is_absolute(), role
        assert spec.filepath.exists(), role


def test_unspecified_values_take_their_defaults():
    config = config_from_dict({"name": "sparse"}, base_dirpath=CONFIGS_DIRPATH)
    assert config.name == "sparse"
    assert config.content.symbol == "Bb"
    assert config.palette.ink.startswith("#")


def test_unknown_section_is_rejected():
    with pytest.raises(ConfigError, match="unknown section 'colours'"):
        config_from_dict({"colours": {}}, base_dirpath=CONFIGS_DIRPATH)


def test_unknown_key_is_rejected():
    with pytest.raises(ConfigError, match="unknown key"):
        config_from_dict({"content": {"symbl": "Bb"}}, base_dirpath=CONFIGS_DIRPATH)


def test_unknown_font_role_is_rejected():
    with pytest.raises(ConfigError, match="unknown role"):
        config_from_dict({"fonts": {"heading": {}}}, base_dirpath=CONFIGS_DIRPATH)


def test_missing_font_file_is_reported_with_its_path():
    with pytest.raises(ConfigError, match="no such font file"):
        config_from_dict(
            {"fonts": {"symbol": {"filepath": "nowhere/Absent.ttf"}}},
            base_dirpath=CONFIGS_DIRPATH,
        )


def test_wrongly_typed_value_is_rejected():
    with pytest.raises(ConfigError, match="expected a number"):
        config_from_dict(
            {"geometry": {"corner_ratio": "quite round"}}, base_dirpath=CONFIGS_DIRPATH
        )


def test_bad_frame_style_is_rejected():
    with pytest.raises(ConfigError, match="geometry.style"):
        config_from_dict({"geometry": {"style": "hexagon"}}, base_dirpath=CONFIGS_DIRPATH)


def test_with_style_returns_a_restyled_copy(config):
    tiled = config.with_style(TILE)
    assert tiled.geometry.style == TILE
    assert config.geometry.style == SQUIRCLE
    assert tiled.content == config.content


def test_with_unknown_style_is_rejected(config):
    with pytest.raises(ConfigError):
        config.with_style("hexagon")


def test_small_sizes_take_the_small_variations(config):
    small = font_for_size(config, SYMBOL, config.lod.small_size)
    large = font_for_size(config, SYMBOL, config.lod.small_size + 1)
    assert small.variations == config.fonts[SYMBOL].small_variations
    assert large.variations == config.fonts[SYMBOL].variations
    assert small.variations != large.variations


def test_a_role_without_small_variations_keeps_its_weight(config):
    spec = font_for_size(config, "technical", 16)
    assert spec == config.fonts["technical"]


def test_malformed_toml_names_the_file(tmp_path: Path):
    filepath = tmp_path / "broken.toml"
    filepath.write_text("name = [unterminated\n", encoding="utf-8")
    with pytest.raises(ConfigError, match="broken.toml"):
        load_config(filepath)


def test_system_font_config_is_wellformed():
    """The Avenir/DIN configuration parses even where those fonts are absent."""
    filepath = CONFIGS_DIRPATH / "beebium-avenir-din.toml"
    if not Path("/System/Library/Fonts/Avenir Next.ttc").exists():
        pytest.skip("system fonts are macOS-only")
    config = load_config(filepath)
    assert config.fonts[SYMBOL].filepath.name == "Avenir Next.ttc"


def test_default_config_filepath_is_the_one_the_cli_uses():
    from beebium_icon.cli import DEFAULT_CONFIG_FILEPATH as cli_default

    assert cli_default == DEFAULT_CONFIG_FILEPATH
