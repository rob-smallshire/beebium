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

"""Frame display_width across standard and custom-width modes.

display_width is the frame's physical width in 16MHz pixel clocks -- the width
the client stretches every band to. A full-width mode reports 640 whether its
80 columns are clocked at 2MHz or its 40 at 1MHz; a custom narrow mode reports
its own width. Elite programs a 32-column screen (CRTC R1=32), which is 512
clocks wide, not 640 -- the bug that made its space view render too wide.

Requirements:
    - Beebium server executable (auto-detected or via BEEBIUM_SERVER)
    - MOS 1.20 ROM and BASIC 2 ROM (via BEEBIUM_ROM_DIR)
    - DFS 1770 ROM (auto-detected in ROM directory)
    - Elite second-processor disc at tests/assets/discs/Disc999-EliteSNG45.ssd
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains
from beebium.client.video import Frame

ELITE_DISC_FILENAME = "Disc999-EliteSNG45.ssd"
BOFFIN_DISC_FILENAME = "Disc016-Boffin.ssd"


@pytest.fixture(scope="module")
def elite_disc_filepath() -> Path:
    """Path to the second-processor Elite disc image."""
    repo_root = Path(__file__).parent.parent.parent.parent
    path = repo_root / "tests" / "assets" / "discs" / ELITE_DISC_FILENAME
    if not path.exists():
        pytest.skip(f"Elite disc image not found: {path}")
    return path


@pytest.fixture(scope="module")
def boffin_disc_filepath() -> Path:
    """Path to the Boffin disc image.

    Committed under tests/assets/discs like the other game fixtures, but a
    developer's working copy under discs/games is used in preference.
    """
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [
        repo_root / "discs" / "games" / BOFFIN_DISC_FILENAME,
        repo_root / "tests" / "assets" / "discs" / BOFFIN_DISC_FILENAME,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    pytest.skip(f"Boffin disc image not found: {BOFFIN_DISC_FILENAME}")


class TestStandardModeDisplayWidth:
    """Every full-width mode reports 640, regardless of its character clock."""

    @pytest.mark.parametrize("mode", [0, 1, 2, 4, 5])
    def test_bitmap_mode_reports_640(self, bbc: Beebium, mode: int) -> None:
        bbc.debugger.stop()
        # Boot to the BASIC prompt, then switch to the target mode.
        assert bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"), emulated_seconds=10.0
        ), "BASIC prompt never appeared"
        bbc.keyboard.type(f"MODE {mode}\r")
        bbc.run_for_emulated_seconds(1.0)

        frame = bbc.video.capture_frame()
        assert frame.display_width == 640, (
            f"MODE {mode}: display_width {frame.display_width} != 640"
        )

    def test_mode7_reports_640(self, bbc: Beebium) -> None:
        # Mode 7 is the power-on default; its two half-character batches per
        # 1MHz character period keep it 640 wide like the bitmap modes.
        bbc.debugger.stop()
        assert bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"), emulated_seconds=10.0
        ), "BASIC prompt never appeared"
        frame = bbc.video.capture_frame()
        assert frame.display_width == 640, (
            f"MODE 7: display_width {frame.display_width} != 640"
        )


# The game-boot display-width tests boot a disc and wait in real time for a
# game screen to appear. On the shared Windows and macOS CI runners that boot is
# timing-sensitive and intermittently overruns the wait (Elite through its Tube
# second processor; Boffin loading its game screen), so the tests flake. Issue
# #76 tracks diagnosing that. The standard-mode display-width tests below do not
# boot a game and run everywhere. Linux CI is reliable and keeps the coverage;
# local runs are unaffected (the guard needs CI=true).
_skip_game_boot_ci = pytest.mark.skipif(
    sys.platform in ("win32", "darwin") and os.environ.get("CI") == "true",
    reason="Game boot too timing-sensitive for Windows/macOS CI runners (issue #76)",
)


@_skip_game_boot_ci
class TestEliteDisplayWidth:
    """Elite's 32-column split screen reports 512, not the old hardcoded 640."""

    @pytest.fixture
    def bbc_elite(
        self,
        mos_filepath: Path,
        basic_filepath: Path | None,
        beebium_server_filepath: Path | None,
        dfs_1770_rom_filepath: Path,
        elite_disc_filepath: Path,
    ) -> Beebium:
        """A BBC Micro with a 65C102 co-processor running second-processor Elite.

        Elite here is the Tube build, so it needs the co-processor; it is
        started with *RUN !BOOT rather than an auto-boot link, matching the
        disc's own loader.
        """
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
                    "--tube-65c02",
                ],
                startup_timeout=20.0,
            ) as bbc:
                bbc.disc.drive(0).insert(elite_disc_filepath)
                bbc.keyboard.type("*RUN !BOOT\r")
                yield bbc
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    def test_split_screen_reports_512_with_256_128_bands(
        self, bbc_elite: Beebium
    ) -> None:
        bbc = bbc_elite

        # Run until Elite has reached its 32-column screen: the MODE 4 view
        # (256 logical) above the MODE 5 dashboard (128 logical). The loader
        # passes through other split layouts first (a 320-wide loading screen),
        # so the dashboard's 128-wide band is what marks the screen we want.
        # Streaming drives the machine, so the loader runs as the frames go by.
        split = None
        last_multi = None
        with bbc.debugger.running():
            for frame in bbc.video.stream_frames(max_frames=3000):
                widths = {r.pixel_width for r in frame.regions}
                if len(frame.regions) > 1:
                    last_multi = frame
                if 128 in widths and 256 in widths:
                    split = frame
                    break

        assert split is not None, (
            "Elite's 32-column dashboard split never appeared; last multi-band "
            f"frame had bands "
            f"{sorted(r.pixel_width for r in last_multi.regions) if last_multi else None}"
        )

        band_widths = sorted(r.pixel_width for r in split.regions)
        # Both bands are physically 512 clocks wide (32 chars * 16 clocks), so
        # the frame reports 512 -- not the old hardcoded 640.
        assert split.display_width == 512, (
            f"Elite display_width {split.display_width} != 512 "
            f"(bands {band_widths})"
        )


def _wait_for_frame(bbc: Beebium, predicate, timeout: float = 30.0) -> Frame:
    """Poll captured frames until one satisfies ``predicate`` (machine running)."""
    deadline = time.monotonic() + timeout
    frame = bbc.video.capture_frame()
    while True:
        if predicate(frame):
            return frame
        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"no frame matched within {timeout:g}s; last was "
                f"{frame.width}x{frame.height} display={frame.display_width} "
                f"field_order={frame.field_order} "
                f"regions={[r.pixel_width for r in frame.regions]}"
            )
        frame = bbc.video.capture_frame()


@_skip_game_boot_ci
class TestBoffinDisplayWidth:
    """Boffin's 92-column game screen is wider than 640: the opposite of Elite.

    Where Elite's 32-column screen was stretched to 640, Boffin's 92-column
    MODE 1 screen was squashed to 640. Its true physical width is 92 chars * 8
    clocks = 736 -- exactly b2's visible raster -- so it should report 736.
    """

    @pytest.fixture
    def bbc_boffin(
        self,
        mos_filepath: Path,
        basic_filepath: Path | None,
        beebium_server_filepath: Path | None,
        dfs_1770_rom_filepath: Path,
        boffin_disc_filepath: Path,
    ) -> Beebium:
        """A standard Model B (no Tube) that *EXECs Boffin's boot file."""
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
                bbc.disc.drive(0).insert(boffin_disc_filepath)
                bbc.keyboard.type("*EXEC !BOOT\r")
                bbc.debugger.ensure_running()
                yield bbc
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    def test_game_screen_reports_736_and_splash_stays_640(
        self, bbc_boffin: Beebium
    ) -> None:
        bbc = bbc_boffin

        # Page through the three instruction screens.
        for heading in (
            "The aim of Professor Boffin",
            "Points are gained",
            "Use the umbrella",
        ):
            bbc.expect(heading, timeout=60.0)
            bbc.keyboard.type(" ")

        # The full-width MODE 1 splash: a plain 320-logical progressive screen
        # that must STAY 640 -- the regression guard for the wide-mode fix.
        bbc.expect("Game designed and written by", timeout=60.0)
        splash = _wait_for_frame(bbc, lambda f: f.field_order == 0 and f.height >= 200)
        assert splash.display_width == 640, (
            f"Boffin splash display_width {splash.display_width} != 640 "
            f"(width {splash.width})"
        )

        bbc.expect("Brilliant Bouncing Boffins", timeout=60.0)
        bbc.keyboard.type(" ")

        # A brief MODE 7 "CAVE 1" card (interlaced) precedes the game screen.
        # Wait through it, then for the progressive game screen to return.
        _wait_for_frame(bbc, lambda f: f.field_order != 0, timeout=60.0)
        _wait_for_frame(bbc, lambda f: f.field_order == 0 and f.width < 640, timeout=60.0)
        bbc.run_for_emulated_seconds(2.0)  # let the game screen settle

        game = bbc.video.capture_frame()
        band_widths = [r.pixel_width for r in game.regions]

        # 92 columns of MODE 1 (4 pixels/char at 2MHz) -> 368 logical, and
        # 92 * 8 clocks = 736 physical: wider than 640, not squashed to it.
        assert game.width == 368, f"Boffin game width {game.width} != 368"
        assert game.height == 144, f"Boffin game height {game.height} != 144"
        assert band_widths == [368], f"Boffin game regions {band_widths} != [368]"
        assert game.display_width == 736, (
            f"Boffin game display_width {game.display_width} != 736"
        )
        # Borders now share the physical 16MHz grid: the 2MHz line is 1024 clocks
        # of left border + active display + right border. Pre-fix the right
        # border was computed against the logical width and came out at 456.
        assert game.left_border + game.display_width + game.right_border == 1024, (
            f"Boffin borders L{game.left_border} + display {game.display_width} + "
            f"R{game.right_border} != 1024 (a 2MHz line)"
        )
        assert game.right_border < 200, (
            f"Boffin right_border {game.right_border} still in the wrong units"
        )
