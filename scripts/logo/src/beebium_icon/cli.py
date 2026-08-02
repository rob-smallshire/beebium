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

"""Command line for the icon generator."""

from __future__ import annotations

import argparse
import sys
from dataclasses import replace
from pathlib import Path

from beebium_icon.bundle import TARGETS, build_target, install_appiconset
from beebium_icon.config import FRAME_STYLES, ConfigError, IconConfig, load_config
from beebium_icon.raster import render_png
from beebium_icon.sheet import build_sheet
from beebium_icon.svg import render_svg
from beebium_icon.typography import TypographyError

DEFAULT_CONFIG_FILEPATH = Path(__file__).resolve().parents[2] / "configs" / "beebium.toml"
DEFAULT_OUT_DIRPATH = Path(__file__).resolve().parents[2] / "out"
SHEET_SIZES = (16, 32, 64, 128, 256, 512)


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    arguments = parser.parse_args(argv)
    try:
        config = load_config(arguments.config)
        if arguments.style:
            config = config.with_style(arguments.style)
        if arguments.no_shadow:
            config = replace(
                config, geometry=replace(config.geometry, shadow_opacity=0.0)
            )
        return arguments.handler(arguments, config)
    except (ConfigError, TypographyError, FileNotFoundError, ValueError) as error:
        print(f"beebium-icon: {error}", file=sys.stderr)
        return 1


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="beebium-icon",
        description="Generate the Beebium application icon at every size a "
        "platform asks for.",
    )
    parser.add_argument(
        "-c",
        "--config",
        type=Path,
        default=DEFAULT_CONFIG_FILEPATH,
        help="icon configuration TOML (default: %(default)s)",
    )
    parser.add_argument(
        "--style",
        choices=FRAME_STYLES,
        help="override the frame style the configuration or target selects",
    )
    parser.add_argument(
        "--no-shadow",
        action="store_true",
        help="drop the shadow, which a document or a web page does not want "
        "and which is the least portable part of an SVG",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    svg = subparsers.add_parser("svg", help="write one SVG")
    svg.add_argument("--size", type=int, default=1024, help="size in pixels")
    svg.add_argument("-o", "--output", type=Path, help="output file (default: stdout)")
    svg.set_defaults(handler=_command_svg)

    png = subparsers.add_parser("png", help="write one PNG")
    png.add_argument("--size", type=int, default=1024, help="size in pixels")
    png.add_argument("-o", "--output", type=Path, required=True, help="output file")
    png.set_defaults(handler=_command_png)

    sheet = subparsers.add_parser(
        "sheet", help="write a contact sheet of the level-of-detail ladder"
    )
    sheet.add_argument(
        "--sizes",
        type=_size_list,
        default=SHEET_SIZES,
        help="comma-separated sizes (default: %(default)s)",
    )
    sheet.add_argument("-o", "--output", type=Path, required=True, help="output PNG")
    sheet.set_defaults(handler=_command_sheet)

    build = subparsers.add_parser("build", help="write platform icon bundles")
    build.add_argument(
        "targets",
        nargs="*",
        default=None,
        help=f"platforms to build: {', '.join(TARGETS)} (default: all)",
    )
    build.add_argument(
        "-o",
        "--out",
        type=Path,
        default=DEFAULT_OUT_DIRPATH,
        help="output directory (default: %(default)s)",
    )
    build.add_argument(
        "--install-appiconset",
        type=Path,
        metavar="DIRPATH",
        help="also copy the generated asset catalogue into this "
        "AppIcon.appiconset directory",
    )
    build.set_defaults(handler=_command_build)

    return parser


def _size_list(text: str) -> tuple[int, ...]:
    try:
        sizes = tuple(int(part) for part in text.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError(f"not a comma-separated size list: {text!r}")
    if not sizes or any(size <= 0 for size in sizes):
        raise argparse.ArgumentTypeError("sizes must be positive")
    return sizes


def _command_svg(arguments: argparse.Namespace, config: IconConfig) -> int:
    svg = render_svg(config, arguments.size)
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(svg, encoding="utf-8")
        print(f"wrote {arguments.output}")
    else:
        sys.stdout.write(svg)
    return 0


def _command_png(arguments: argparse.Namespace, config: IconConfig) -> int:
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(render_png(config, arguments.size))
    print(f"wrote {arguments.output}")
    return 0


def _command_sheet(arguments: argparse.Namespace, config: IconConfig) -> int:
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(build_sheet(config, tuple(arguments.sizes)))
    print(f"wrote {arguments.output}")
    return 0


def _command_build(arguments: argparse.Namespace, config: IconConfig) -> int:
    names = list(arguments.targets or ["all"])
    unknown = sorted(set(names) - set(TARGETS) - {"all"})
    if unknown:
        raise ValueError(
            f"unknown target(s) {unknown}; expected any of {[*TARGETS, 'all']}"
        )
    if "all" in names:
        names = list(TARGETS)
    for name in names:
        target = TARGETS[name]
        if arguments.style:
            target = _restyle(target, arguments.style)
        written = build_target(config, target, arguments.out)
        print(
            f"{name}: {target.description}, {len(written)} file(s) "
            f"-> {arguments.out / name}"
        )
    if arguments.install_appiconset:
        installed = install_appiconset(arguments.out, arguments.install_appiconset)
        print(f"installed {len(installed)} file(s) -> {arguments.install_appiconset}")
    return 0


def _restyle(target, style: str):
    return replace(target, style=style)


if __name__ == "__main__":
    raise SystemExit(main())
