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

"""Platform bundles and the command line."""

from __future__ import annotations

import io
import json
from dataclasses import replace
from pathlib import Path

import pytest
from PIL import Image

from beebium_icon.bundle import APPICONSET_SLOTS, TARGETS, build_target, install_appiconset
from beebium_icon.cli import main
from beebium_icon.config import SQUIRCLE, TILE


@pytest.fixture(scope="module")
def built(config, tmp_path_factory) -> Path:
    out_dirpath = tmp_path_factory.mktemp("out")
    for target in TARGETS.values():
        build_target(config, target, out_dirpath)
    return out_dirpath


def test_macos_writes_an_icns_and_an_asset_catalogue(built):
    assert (built / "macos" / "Beebium.icns").is_file()
    assert (built / "macos" / "AppIcon.appiconset" / "Contents.json").is_file()


def test_every_asset_catalogue_slot_is_filled(built):
    appiconset_dirpath = built / "macos" / "AppIcon.appiconset"
    contents = json.loads((appiconset_dirpath / "Contents.json").read_text())
    assert len(contents["images"]) == len(APPICONSET_SLOTS)
    for image in contents["images"]:
        filepath = appiconset_dirpath / image["filename"]
        assert filepath.is_file(), image["filename"]
        nominal = int(image["size"].split("x")[0])
        scale = int(image["scale"].rstrip("x"))
        assert Image.open(filepath).size == (nominal * scale, nominal * scale)


def test_asset_catalogue_slots_are_unique(built):
    contents = json.loads(
        (built / "macos" / "AppIcon.appiconset" / "Contents.json").read_text()
    )
    slots = {(image["size"], image["scale"]) for image in contents["images"]}
    assert len(slots) == len(contents["images"])


def test_windows_writes_a_multi_resolution_ico(built):
    filepath = built / "windows" / "beebium.ico"
    sizes = {size[0] for size in Image.open(filepath).info["sizes"]}
    assert sizes == set(TARGETS["windows"].sizes)


def test_linux_writes_a_hicolor_tree(built):
    hicolor_dirpath = built / "linux" / "hicolor"
    for size in TARGETS["linux"].sizes:
        assert (hicolor_dirpath / f"{size}x{size}" / "apps" / "beebium.png").is_file()
    assert (hicolor_dirpath / "scalable" / "apps" / "beebium.svg").is_file()


def test_web_writes_a_favicon_a_touch_icon_and_an_svg(built):
    web_dirpath = built / "web"
    assert Image.open(web_dirpath / "favicon.ico").format == "ICO"
    assert Image.open(web_dirpath / "apple-touch-icon.png").size == (180, 180)
    assert (web_dirpath / "beebium.svg").read_text().startswith("<svg")


def test_each_target_uses_the_frame_style_that_suits_it(config, tmp_path):
    """macOS gets Apple's continuous body; the other platforms get the tile."""
    assert TARGETS["macos"].style == SQUIRCLE
    assert {TARGETS[name].style for name in ("windows", "linux", "web")} == {TILE}

    build_target(config, TARGETS["macos"], tmp_path)
    build_target(config, replace(TARGETS["macos"], name="tiled", style=TILE), tmp_path)
    squircled = Image.open(tmp_path / "macos" / "AppIcon.appiconset" / "icon_512x512.png")
    tiled = Image.open(tmp_path / "tiled" / "AppIcon.appiconset" / "icon_512x512.png")
    assert squircled.tobytes() != tiled.tobytes()
    # Both shapes are rounded, so neither paints the canvas corner.
    assert squircled.convert("RGBA").getpixel((0, 0))[3] == 0
    assert tiled.convert("RGBA").getpixel((0, 0))[3] == 0


def test_installing_the_catalogue_replaces_what_was_there(built, tmp_path):
    appiconset_dirpath = tmp_path / "AppIcon.appiconset"
    appiconset_dirpath.mkdir()
    stale_filepath = appiconset_dirpath / "icon_99x99.png"
    stale_filepath.write_bytes(b"stale")
    installed = install_appiconset(built, appiconset_dirpath)
    assert not stale_filepath.exists()
    assert (appiconset_dirpath / "Contents.json").is_file()
    assert len(installed) == len(APPICONSET_SLOTS) + 1


def test_installing_from_an_unbuilt_tree_is_refused(tmp_path):
    with pytest.raises(FileNotFoundError, match="no generated asset catalogue"):
        install_appiconset(tmp_path, tmp_path / "AppIcon.appiconset")


def test_cli_writes_an_svg(tmp_path, capsys):
    filepath = tmp_path / "icon.svg"
    assert main(["svg", "--size", "256", "-o", str(filepath)]) == 0
    assert filepath.read_text().startswith("<svg")


def test_cli_writes_an_svg_to_stdout(capsys):
    assert main(["svg", "--size", "64"]) == 0
    assert capsys.readouterr().out.startswith("<svg")


def test_cli_writes_a_png(tmp_path):
    filepath = tmp_path / "icon.png"
    assert main(["png", "--size", "64", "-o", str(filepath)]) == 0
    assert Image.open(filepath).size == (64, 64)


def test_cli_writes_a_contact_sheet(tmp_path):
    filepath = tmp_path / "sheet.png"
    assert main(["sheet", "--sizes", "16,32", "-o", str(filepath)]) == 0
    assert Image.open(filepath).width > 32


def test_cli_builds_one_target(tmp_path):
    assert main(["build", "windows", "-o", str(tmp_path)]) == 0
    assert (tmp_path / "windows" / "beebium.ico").is_file()
    assert not (tmp_path / "macos").exists()


def test_cli_builds_every_target_by_default(tmp_path):
    assert main(["build", "-o", str(tmp_path)]) == 0
    for name in TARGETS:
        assert (tmp_path / name).is_dir()


def test_cli_style_override_reaches_the_bundle(tmp_path):
    assert main(["--style", "tile", "build", "macos", "-o", str(tmp_path)]) == 0
    squircle_dirpath = tmp_path / "squircle"
    assert main(["--style", "squircle", "build", "macos", "-o", str(squircle_dirpath)]) == 0
    tiled = (tmp_path / "macos" / "AppIcon.appiconset" / "icon_512x512.png").read_bytes()
    squircled = (
        squircle_dirpath / "macos" / "AppIcon.appiconset" / "icon_512x512.png"
    ).read_bytes()
    assert tiled != squircled


def test_cli_reports_an_unreadable_configuration(tmp_path, capsys):
    assert main(["-c", str(tmp_path / "absent.toml"), "svg"]) == 1
    assert "beebium-icon:" in capsys.readouterr().err


def test_cli_reports_an_unknown_target(tmp_path, capsys):
    assert main(["build", "amiga", "-o", str(tmp_path)]) == 1
    assert "unknown target" in capsys.readouterr().err


def test_cli_installs_into_an_asset_catalogue(tmp_path):
    appiconset_dirpath = tmp_path / "Assets.xcassets" / "AppIcon.appiconset"
    assert (
        main(
            [
                "build",
                "macos",
                "-o",
                str(tmp_path / "out"),
                "--install-appiconset",
                str(appiconset_dirpath),
            ]
        )
        == 0
    )
    assert (appiconset_dirpath / "icon_16x16.png").is_file()


def test_a_generated_png_is_not_blank(config):
    from beebium_icon.raster import render_png

    image = Image.open(io.BytesIO(render_png(config, 128))).convert("RGB")
    assert len(set(image.getcolors(maxcolors=1 << 16) or [])) > 2
