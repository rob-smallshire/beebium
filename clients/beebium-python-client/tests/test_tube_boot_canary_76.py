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

"""Issue #76 canary: a Windows-CI Tube-boot flake recorder, not a gate.

The Tube Elite display-width scenario (test_display_width_geometry.py) is
bimodal on the GitHub Actions Windows lane: it usually reaches Elite's
256+128 split at emulated frame ~352 in ~11 s, but occasionally streams all
3000 frames with no multi-band frame at all -- the loader appears not to
progress. That test is now skipped on Windows CI (issue #76). This canary
boots Tube Elite exactly as it did, but instead of gating CI it records
evidence when the boot fails there:

    * On the Windows CI lane it is xfail(strict=False), so a stall is
      recorded as xfailed (never red) and a normal boot as xpassed.
    * Everywhere else it is a plain pass.

When it fails it writes a diagnostic snapshot -- to the job log and to a
sidecar file ($BEEBIUM_ARTIFACT_DIR or the CWD) -- capturing host and
parasite cycle counts before and after streaming (so zero emulated progress
can be told from a stream/transport disruption), the Tube ULA state with its
transfer counters and interrupts, the screen text, and both CPUs' PCs.

Not reproducible by host slowness alone: on the Windows dev machine (Slioch,
4 cores) 18 streamed attempts from idle (0.63x real) down to 0.04x real under
CPU load all reached the split at emulated frame 349-353 -- the emulated
frame count to the split is invariant of host speed, as a correct design
requires. The remaining suspect is the CI runner's bursty stall profile
interacting with the harness/transport, which this canary is meant to catch.

Requirements:
    - Beebium server executable (auto-detected or via BEEBIUM_SERVER)
    - MOS 1.20 ROM and BASIC 2 ROM (via BEEBIUM_ROM_DIR)
    - DFS 1770 ROM (auto-detected in ROM directory)
    - Elite second-processor disc at tests/assets/discs/Disc999-EliteSNG45.ssd
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
    dump_coprocessor_diagnostics,
    dump_diagnostics,
    run_until_or_timeout,
)

ELITE_DISC_FILENAME = "Disc999-EliteSNG45.ssd"
MAX_FRAMES = 3000
DIAG_FILENAME = "tube_boot_canary_76.diag.txt"

_on_windows_ci = sys.platform == "win32" and os.environ.get("CI") == "true"

_xfail_windows_ci = pytest.mark.xfail(
    _on_windows_ci,
    strict=False,
    reason="#76 canary: the Windows-CI Tube boot is bimodal; record evidence, never go red",
)


def _find_elite_disc() -> Path | None:
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [
        repo_root / "tests" / "assets" / "discs" / ELITE_DISC_FILENAME,
        repo_root / "discs" / "games" / ELITE_DISC_FILENAME,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


@pytest.fixture
def elite_disc_filepath() -> Path:
    path = _find_elite_disc()
    if path is None:
        pytest.skip(f"Elite disc image not found: {ELITE_DISC_FILENAME}")
    return path


def _cycle_count(view: Beebium) -> int | None:
    """The emulated cycle count of a host or coprocessor view, or None."""
    try:
        return view.debugger.get_state().cycle_count
    except Exception:  # noqa: BLE001 -- diagnostics must never mask the failure
        return None


def _emit(lines: list[str]) -> None:
    """Write the captured diagnostic to the job log and a sidecar artifact."""
    text = "\n".join(lines)
    print(text)
    artifact_dirpath = Path(os.environ.get("BEEBIUM_ARTIFACT_DIR", "."))
    try:
        artifact_dirpath.mkdir(parents=True, exist_ok=True)
        (artifact_dirpath / DIAG_FILENAME).write_text(text + "\n", encoding="ascii")
    except OSError as e:
        print(f"(could not write {DIAG_FILENAME}: {e})")


def _capture_failure(
    bbc: Beebium,
    *,
    frames: int,
    host_cycles_before: int | None,
    coprocessor_cycles_before: int | None,
    first_multi,
) -> None:
    """On a no-split boot, record enough to tell a stall from a stream glitch."""
    bbc.debugger.stop()
    host_cycles_after = _cycle_count(bbc)

    lines = [
        "=== #76 CANARY: Tube Elite boot produced no split ===",
        f"frames streamed: {frames} (budget {MAX_FRAMES}, = {frames / 50:.1f}s emulated)",
        f"first multi-band frame: {first_multi}",
        f"tube banner seen at boot: {screen_contains(bbc, 'Acorn TUBE')}",
        "",
        "-- emulated progress (the discriminator) --",
        f"host cycles:        before={host_cycles_before} after={host_cycles_after} "
        f"delta={_delta(host_cycles_before, host_cycles_after)}",
    ]

    coprocessor_cycles_after = None
    try:
        with bbc.coprocessor() as coprocessor:
            coprocessor.debugger.stop()
            coprocessor_cycles_after = _cycle_count(coprocessor)
            lines.append(
                f"coprocessor cycles: before={coprocessor_cycles_before} "
                f"after={coprocessor_cycles_after} "
                f"delta={_delta(coprocessor_cycles_before, coprocessor_cycles_after)}"
            )
            lines.append("")
            lines.append(
                "Interpretation: both deltas ~0 -> emulated machine stalled/reset "
                "(not a slow host); large deltas but no split -> the machine ran, so "
                "suspect a stream/transport disruption or a loader misfire."
            )
    except Exception as e:  # noqa: BLE001
        lines.append(f"coprocessor cycles: unavailable ({e!r})")

    lines.append("")
    lines.append("-- Tube ULA state (transfer counters show whether bytes moved) --")
    try:
        lines.append(str(bbc.tube_ula.state))
    except Exception as e:  # noqa: BLE001
        lines.append(f"tube_ula.state unavailable ({e!r})")

    lines.append("")
    lines.append("-- screen (mode-7 decode; meaningful if stuck pre-mode-switch) --")
    try:
        rows = read_mode7_screen(bbc)
        nonblank = [f"{i:2d}: {row}" for i, row in enumerate(rows) if row.strip()]
        lines.extend(nonblank or ["(all blank / not a mode-7 screen)"])
    except Exception as e:  # noqa: BLE001
        lines.append(f"screen read unavailable ({e!r})")

    _emit(lines)

    # The rich human-readable dumps go to the job log only (they are verbose).
    dump_diagnostics(bbc)
    try:
        with bbc.coprocessor() as coprocessor:
            dump_coprocessor_diagnostics(coprocessor)
    except Exception as e:  # noqa: BLE001
        print(f"coprocessor diagnostics unavailable ({e!r})")


def _delta(before: int | None, after: int | None) -> str:
    if before is None or after is None:
        return "?"
    return str(after - before)


class TestTubeBootCanary76:
    """Records, without gating, the bimodal Windows-CI Tube-boot failure (#76)."""

    @pytest.fixture
    def bbc_elite(
        self,
        mos_filepath: Path,
        basic_filepath: Path | None,
        beebium_server_filepath: Path | None,
        dfs_1770_rom_filepath: Path,
        elite_disc_filepath: Path,
        tmp_path: Path,
        monkeypatch: pytest.MonkeyPatch,
    ) -> Beebium:
        """A BBC Micro + 6502 second processor booting second-processor Elite.

        Its own disc work directory keeps it isolated; the server picks its own
        PID and an ephemeral port, so it is safe on a shared box.
        """
        monkeypatch.setenv("BEEBIUM_DISC_WORK_DIR", str(tmp_path / "disc_work"))
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
                yield bbc
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    @_xfail_windows_ci
    def test_elite_reaches_its_split(
        self, bbc_elite: Beebium, elite_disc_filepath: Path
    ) -> None:
        """Boot Tube Elite and confirm it reaches the 256+128 split screen.

        The same scenario as the display-width test, but on failure it records
        a diagnostic snapshot rather than gating CI (issue #76).
        """
        bbc = bbc_elite
        assert run_until_or_timeout(
            bbc, lambda: screen_contains(bbc, "Acorn TUBE"), emulated_seconds=15.0
        ), "Tube banner never appeared"

        host_cycles_before = _cycle_count(bbc)
        coprocessor_cycles_before = None
        try:
            with bbc.coprocessor() as coprocessor:
                coprocessor_cycles_before = _cycle_count(coprocessor)
        except Exception:  # noqa: BLE001
            pass

        bbc.disc.drive(0).insert(elite_disc_filepath)
        bbc.keyboard.type("*RUN !BOOT\r")

        split = None
        first_multi = None
        frames = 0
        with bbc.debugger.running():
            for frame in bbc.video.stream_frames(max_frames=MAX_FRAMES):
                frames += 1
                widths = {r.pixel_width for r in frame.regions}
                if len(frame.regions) > 1 and first_multi is None:
                    first_multi = (frames, sorted(widths))
                if 128 in widths and 256 in widths:
                    split = (frames, sorted(widths))
                    break

        if split is None:
            _capture_failure(
                bbc,
                frames=frames,
                host_cycles_before=host_cycles_before,
                coprocessor_cycles_before=coprocessor_cycles_before,
                first_multi=first_multi,
            )
            pytest.fail(
                f"Elite reached no 256+128 split in {frames} frames "
                f"({frames / 50:.1f}s emulated); see the #76 diagnostic above"
            )
