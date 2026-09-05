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
    ROUNDED,
    SQUIRCLE,
    ConfigError,
    config_from_dict,
    load_config,
)

from conftest import ARTWORK_FILEPATH, CONFIGS_DIRPATH, DEFAULT_CONFIG_FILEPATH


def test_default_config_loads(config):
    assert config.name == "beebium"
    assert config.shape == SQUIRCLE


def test_the_artwork_resolves_to_an_existing_image(config):
    assert config.artwork.filepath.is_absolute()
    assert config.artwork.filepath == ARTWORK_FILEPATH


def test_unspecified_values_take_their_defaults():
    config = config_from_dict({"name": "sparse"}, base_dirpath=CONFIGS_DIRPATH)
    assert config.name == "sparse"
    assert config.squircle.exponent == 5.0
    assert config.shadow.min_size == 64


def test_unknown_section_is_rejected():
    with pytest.raises(ConfigError, match="unknown section 'palette'"):
        config_from_dict({"palette": {}}, base_dirpath=CONFIGS_DIRPATH)


def test_unknown_key_is_rejected():
    with pytest.raises(ConfigError, match="unknown key"):
        config_from_dict({"shadow": {"opacity_": 1}}, base_dirpath=CONFIGS_DIRPATH)


def test_missing_artwork_is_reported_with_its_path():
    with pytest.raises(ConfigError, match="no such image"):
        config_from_dict(
            {"artwork": {"filepath": "nowhere/absent.png"}},
            base_dirpath=CONFIGS_DIRPATH,
        )


def test_a_relative_artwork_path_resolves_against_the_config():
    config = config_from_dict(
        {"artwork": {"filepath": "../the-shape-of-beebium.png"}},
        base_dirpath=CONFIGS_DIRPATH,
    )
    assert config.artwork.filepath == ARTWORK_FILEPATH


def test_wrongly_typed_value_is_rejected():
    with pytest.raises(ConfigError, match="expected a number"):
        config_from_dict(
            {"squircle": {"corner_ratio": "quite round"}}, base_dirpath=CONFIGS_DIRPATH
        )


def test_bad_shape_is_rejected():
    with pytest.raises(ConfigError, match="shape"):
        config_from_dict({"shape": "hexagon"}, base_dirpath=CONFIGS_DIRPATH)


def test_with_shape_returns_a_reshaped_copy(config):
    rounded = config.with_shape(ROUNDED)
    assert rounded.shape == ROUNDED
    assert config.shape == SQUIRCLE
    assert rounded.artwork == config.artwork


def test_with_unknown_shape_is_rejected(config):
    with pytest.raises(ConfigError):
        config.with_shape("hexagon")


def test_malformed_toml_names_the_file(tmp_path: Path):
    filepath = tmp_path / "broken.toml"
    filepath.write_text("name = [unterminated\n", encoding="utf-8")
    with pytest.raises(ConfigError, match="broken.toml"):
        load_config(filepath)


def test_default_config_filepath_is_the_one_the_cli_uses():
    from beebium_icon.cli import DEFAULT_CONFIG_FILEPATH as cli_default

    assert cli_default == DEFAULT_CONFIG_FILEPATH
