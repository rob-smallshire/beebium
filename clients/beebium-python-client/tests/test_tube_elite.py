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

"""Integration tests for booting 6502 Second Processor Elite via the Tube.

These tests launch a Beebium server with --tube-65c02, insert the
6502 Second Processor Elite disc, and attempt to boot it. The goal is to
exercise the full Tube data transfer pipeline under real workload.

Requirements:
    - Beebium server executable (auto-detected or via BEEBIUM_SERVER)
    - MOS 1.20 ROM and BASIC 2 ROM (via BEEBIUM_ROM_DIR)
    - Elite disc image at tests/assets/discs/Disc999-EliteSNG45.ssd
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import read_mode7_screen, screen_contains

from tube_test_helpers import (
    dump_diagnostics,
    run_until_or_timeout,
)

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)

ELITE_DISC_FILENAME = "Disc999-EliteSNG45.ssd"


def _find_elite_disc() -> Path | None:
    """Find the Elite disc image.

    Search order:
    1. Test assets directory (tests/assets/discs/)
    2. Development disc collection (discs/games/)
    """
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [
        repo_root / "tests" / "assets" / "discs" / ELITE_DISC_FILENAME,
        repo_root / "discs" / "games" / ELITE_DISC_FILENAME,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


@pytest.fixture(scope="module")
def elite_disc_filepath() -> Path:
    """Path to the Elite disc image."""
    path = _find_elite_disc()
    if path is None:
        pytest.skip(f"Elite disc image not found: {ELITE_DISC_FILENAME}")
    return path


@pytest.fixture
def bbc_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Beebium:
    """A fresh BBC Micro with Tube, booted and ready at the BASIC prompt.

    Each test gets its own emulator instance, booted from cold.
    The fixture waits for the Tube banner before yielding.

    Configures:
    - Tube with 65C02 3MHz parasite
    - Acorn 1770 disc controller
    - 1770 DFS ROM in sideways slot 14
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            found = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "Acorn TUBE"),
                emulated_seconds=30.0,
            )
            if not found:
                dump_diagnostics(bbc)
                pytest.fail("Tube banner not visible after boot")
            with bbc.debugger.running():
                yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@_skip_windows_ci
class TestTubeEliteBoot:
    """Test booting 6502 Second Processor Elite via the Tube."""

    def test_tube_enabled(self, bbc_tube: Beebium) -> None:
        """Verify Tube hardware is enabled.

        Only checks host-side status. In the single-process Tube
        architecture the parasite runs inside the same server, so
        parasite_connected / parasite_type / parasite_grpc_address
        fields (retained on GetTubeStatusResponse for compatibility)
        are no longer populated by the server. The test_tube_banner
        test below is the functional check that the parasite is
        actually running.
        """
        status = bbc_tube.tube.status
        assert status.has_tube_socket, "Machine should have a Tube socket"
        assert status.enabled, "Tube should be enabled"

    def test_tube_banner(self, bbc_tube: Beebium) -> None:
        """After boot, screen should show the Tube banner."""
        assert screen_contains(bbc_tube, "Acorn TUBE")

    def test_insert_elite_disc(self, bbc_tube: Beebium, elite_disc_filepath: Path) -> None:
        """Insert the Elite disc into drive 0."""
        metadata = bbc_tube.disc.drive(0).insert(elite_disc_filepath)
        assert metadata.name, "Disc should have a name"

        drive_status = bbc_tube.disc.drive(0).status
        assert drive_status.state.value == "loaded", f"Drive 0 should be loaded, got {drive_status.state.value}"

    def test_cat_disc(self, bbc_tube: Beebium, elite_disc_filepath: Path) -> None:
        """Type *. to catalog the disc and check output appears."""
        bbc_tube.disc.drive(0).insert(elite_disc_filepath)

        def _catalog_visible():
            rows = read_mode7_screen(bbc_tube)
            for row in rows:
                stripped = row.strip()
                if (
                    stripped
                    and stripped != ">"
                    and "BASIC" not in stripped
                    and "Acorn TUBE" not in stripped
                    and "Acorn 1770" not in stripped
                    and "*." not in stripped
                ):
                    return True
            return False

        bbc_tube.keyboard.type("*.\r")

        found = run_until_or_timeout(
            bbc_tube,
            _catalog_visible,
            emulated_seconds=30.0,
        )

        if not found:
            rows = read_mode7_screen(bbc_tube)
            print("\nScreen after *. command:")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)
            pytest.fail("Expected disc catalog output after *. command")

    def test_run_boot(self, bbc_tube: Beebium, elite_disc_filepath: Path) -> None:
        """Type *RUN !BOOT and check for Elite loading screen."""
        bbc_tube.disc.drive(0).insert(elite_disc_filepath)

        bbc_tube.keyboard.type("*RUN !BOOT\r")

        # Poll for Elite loading text. The !BOOT loader briefly displays
        # "6502 Second Processor ELITE" in Mode 7 before switching to a
        # graphics mode for the game.
        elite_banner = "6502 Second Processor ELITE"
        found = run_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube, elite_banner),
            emulated_seconds=60.0,
        )

        if not found:
            dump_diagnostics(bbc_tube)
            pytest.fail(f"Expected '{elite_banner}' on screen after *RUN !BOOT")
