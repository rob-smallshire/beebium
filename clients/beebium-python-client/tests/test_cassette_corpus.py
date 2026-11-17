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

"""Commercial tapes that stress the cassette path, loaded with boot_tape (#208).

Two collections share the directory BEEBIUM_TAPE_CORPUS_DIRPATH names: UEF and
CSW images identified by SHA-256 (below), and a corpus of recordings supplied for
the project as WAV files, named by file and each with a TOML beside it saying
which channel carries the signal (the WAV cases at the end).

The tapes are third-party images, never committed. They are read from the
directory BEEBIUM_TAPE_CORPUS_DIRPATH names (beebjit's test/tapes is one such
collection) and identified by SHA-256, so a file of the same name with other
contents is not mistaken for the tape. Each test SKIPs when the directory or the
tape is absent.

Each tape is loaded through MOS 1.20 as a test vector and run, in emulated time
stepped by the debugger, until its first screen of play shows a string read from
the screen. The strings were recorded by watching each tape load once. Most of
these games draw in bitmap modes, which is why the test reads the screen through
the server's screen text rather than the teletext grid.
"""

from __future__ import annotations

import hashlib
import time
import os
import threading
import tomllib
import wave
from dataclasses import dataclass
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

_CORPUS_ENV = "BEEBIUM_TAPE_CORPUS_DIRPATH"


@dataclass(frozen=True)
class CorpusTape:
    title: str
    sha256: str
    command: str          # typed after *TAPE
    screen_text: str      # shown on the first screen of play
    seconds: int          # emulated seconds allowed to reach it
    stresses: str


_TAPES = [
    CorpusTape("Caveman Capers", "82d9a56d964ee0d2f8840fdb07232d9d6e15e20c7e9304b28070c2547a74f4f7",
               'CHAIN""', "Press 'SPACE' To Start", 420,
               "the 6850's /DCD latch and its clearing"),
    CorpusTape("Star Drifter", "648b11f0dabc9d5502c8fd78fbebb07090363d66a03f4dc47b215bffa2ad0ba4",
               'CHAIN""', "PLEASE SELECT YOUR KEYS", 420,
               "/DCD after very short carrier tones"),
    CorpusTape("Atic Atac", "4d0d02fde0334503eff8df956dcfc1644ea68235ab2dec057420cb3272ab7fc1",
               "*RUN", "SELECTION SHEET", 540,
               "the motor toggled during carrier"),
    CorpusTape("Uridium (CSW)", "6db5af3d6cdd4fd154c90cbdb2b53e103eb765e171aa83df8889cf026d10969f",
               'CHAIN""', "2. Keyboard", 180,
               "tone, data, tone, data with no gap: /DCD must rise again"),
    CorpusTape("Frak", "096c755e69c9929033e0f80e16dde08758fe390f8940d5dde8f6e746efef1bdc",
               'CHAIN""', "Grandmaster Frak", 460,
               "the 6850's master reset clearing its control register"),
    # The game's own font is not read as text; the ropes of the ring are.
    CorpusTape("Pro Boxing Simulator", "87136038981e361fee5b7fcaa9c4b0640a09f4a7258dbc645bcc79515f590b70",
               'CHAIN""', "_______  _______", 460,
               "bytes written to the 6850 during and after the load"),
]


def _corpus_dirpath() -> Path:
    value = os.environ.get(_CORPUS_ENV)
    if not value:
        pytest.skip(f"{_CORPUS_ENV} is not set, so the commercial tape corpus is not available.")
    dirpath = Path(value)
    if not dirpath.is_dir():
        pytest.skip(f"{_CORPUS_ENV} names {dirpath}, which is not a directory.")
    return dirpath


def _find_tape(dirpath: Path, sha256: str) -> Path | None:
    for filepath in sorted(dirpath.iterdir()):
        if filepath.is_file() and hashlib.sha256(filepath.read_bytes()).hexdigest() == sha256:
            return filepath
    return None


@pytest.mark.parametrize("tape", _TAPES, ids=[t.title for t in _TAPES])
def test_corpus_tape_reaches_its_first_screen(tape: CorpusTape, mos_filepath, basic_filepath,
                                              beebium_server_filepath):
    tape_filepath = _find_tape(_corpus_dirpath(), tape.sha256)
    if tape_filepath is None:
        pytest.skip(f"The corpus has no copy of {tape.title} with the expected SHA-256.")
    try:
        with Beebium.launch(mos_filepath=mos_filepath, basic_filepath=basic_filepath,
                            server=beebium_server_filepath) as bbc:
            bbc.boot_tape(tape_filepath, command=tape.command)
            bbc.debugger.stop()
            text = ""
            for _ in range(tape.seconds):
                bbc.debugger.step_cycles(2_000_000)
                text = bbc.video.screen_text().text
                if tape.screen_text in text:
                    break
            assert tape.screen_text in text, (
                f"{tape.title} ({tape.stresses}) did not reach its first screen in "
                f"{tape.seconds} emulated seconds; the screen reads:\n{text}")
    except ServerNotFoundError as e:
        pytest.skip(str(e))


# =============================================================================
# The WAV corpus: recordings of real tapes, played through the front end
# =============================================================================


def _wav_take(name: str) -> tuple[Path, str]:
    """The take's WAV and the channel its TOML names, or SKIP."""
    dirpath = _corpus_dirpath()
    wav_filepath = dirpath / f"{name}.wav"
    toml_filepath = dirpath / f"{name}.toml"
    if not wav_filepath.is_file() or not toml_filepath.is_file():
        pytest.skip(f"The corpus has no {name}.wav with its TOML.")
    with toml_filepath.open("rb") as f:
        channel = tomllib.load(f).get("signal_channel", "left")
    return wav_filepath, channel


def _run_until(bbc: Beebium, text: str, seconds: int) -> str:
    """Step emulated time a second at a time until `text` is on the screen."""
    screen = ""
    for _ in range(seconds):
        bbc.debugger.step_cycles(2_000_000)
        screen = bbc.video.screen_text().text
        if text in screen:
            break
    return screen


@pytest.fixture
def corpus_bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    try:
        with Beebium.launch(mos_filepath=mos_filepath, basic_filepath=basic_filepath,
                            server=beebium_server_filepath) as bbc:
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


# Plan B2, side 1: one program, recorded at a normal level twice and at three
# set levels. Its title screen comes up about 320 s in.
_PLAN_B2_LOADING = ["plan-b2-side1-001", "plan-b2-side1-002", "plan-b2-side1-level-normal",
                    "plan-b2-side1-level-high"]


@pytest.mark.parametrize("take", _PLAN_B2_LOADING)
def test_plan_b2_take_loads_cleanly(take: str, corpus_bbc):
    wav_filepath, channel = _wav_take(take)
    corpus_bbc.boot_tape(wav_filepath, channel=channel)
    corpus_bbc.debugger.stop()
    screen = _run_until(corpus_bbc, "Press space to start", 380)
    assert "Press space to start" in screen, f"{take} did not reach its title screen; the screen reads:\n{screen}"
    assert "Data?" not in screen and "Block?" not in screen


def test_plan_b2_low_level_take_is_a_known_failure(corpus_bbc):
    """Known failure, not hidden: the low-level take does not load through the
    front end as modelled. With the comparator's hysteresis at a third of this
    take's filtered peak, the two tones' crossings are delayed differently and
    many blocks fail their CRC (261 Data? over 300 s when measured). Whether it
    loads on a real machine is not yet known. If a change makes it load, this
    test fails: update docs/cassette.md and turn this into a loading case."""
    wav_filepath, channel = _wav_take("plan-b2-side1-level-low")
    corpus_bbc.boot_tape(wav_filepath, channel=channel)
    corpus_bbc.debugger.stop()
    screen = _run_until(corpus_bbc, "Press space to start", 380)
    assert "Press space to start" not in screen
    assert "Data?" in screen


def test_cascade_50_chains_a_program_past_the_bad_patches(corpus_bbc):
    """Cascade 50, side 1, take 1: its TOML warns of bad patches, and the
    catalogue shows Data? in GAL and PLA while MAZ, SPA, LUN and STA read
    cleanly. LUN lies past GAL's bad patch; CHAIN "LUN" searches past it."""
    wav_filepath, channel = _wav_take("cascade50-side1-001")
    corpus_bbc.boot_tape(wav_filepath, command='CHAIN "LUN"', channel=channel)
    corpus_bbc.debugger.stop()
    screen = _run_until(corpus_bbc, "ANY KEY TO PLAY", 340)
    assert "LUNAR LANDER" in screen and "ANY KEY TO PLAY" in screen, screen


def _end_of_longest_toneless_stretch(wav_filepath: Path, channel: str) -> float:
    """Where the longest stretch without tone ends, in seconds: the point the
    recording was stopped and the tape rewound. Without tone is near silence,
    or what a deck puts out while it rewinds, a level that hardly crosses its
    own mean (the front end's DC block gives no edges for it either)."""
    with wave.open(str(wav_filepath), "rb") as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        assert width == 2
        index = 0 if channel == "left" or channels == 1 else 1
        window = rate // 10
        quiet_from, longest = None, (0.0, 0.0)
        t = 0.0
        while True:
            frames = w.readframes(window)
            if not frames:
                break
            samples = memoryview(frames).cast("h")[index::channels]
            mean = sum(samples) / len(samples)
            crossings = sum(1 for a, b in zip(samples[:-1], samples[1:], strict=True) if (a - mean) * (b - mean) < 0)
            peak = max(abs(s - mean) for s in samples) / 32768.0
            if crossings < 20 or peak < 0.01:
                if quiet_from is None:
                    quiet_from = t
            elif quiet_from is not None:
                if t - quiet_from > longest[1] - longest[0]:
                    longest = (quiet_from, t)
                quiet_from = None
            t += window / rate
    return longest[1]


def test_palace_of_magic_loads_after_seeking_past_its_rewind(corpus_bbc):
    """A partial take with a play-stop-rewind-play in it: the first pass loads
    the loader and part of the game, then the tape was stopped and rewound, so
    the replay begins again within the loader. Seek past the rewind (a few
    seconds without tone) and the loader loads from the replay, runs, and the game's blocks follow
    until the take ends."""
    wav_filepath, channel = _wav_take("palace-of-magic-side1-partial-rewind")
    resume = _end_of_longest_toneless_stretch(wav_filepath, channel)
    assert 90.0 < resume < 100.0, resume
    corpus_bbc.cassette.insert(wav_filepath, channel=channel)
    corpus_bbc.cassette.seek(resume)
    corpus_bbc.debugger.ensure_running()
    corpus_bbc.keyboard.type('*TAPE\rCHAIN""\r')
    corpus_bbc.cassette.play()
    corpus_bbc.debugger.stop()
    screen = _run_until(corpus_bbc, "POM        02", 40)
    assert "PALACE OF MAGIC" in screen and "POM        02" in screen, screen
    assert "Rewind tape" not in screen and "Data?" not in screen


@pytest.mark.slow
def test_plan_b2_take_loads_streamed_live(corpus_bbc):
    """The normal take as a live stream: the WAV read in blocks and paced to
    wall-clock time, as a deck's line-out arrives, through StreamAudio. It
    takes as long as the tape: about five and a half minutes."""
    wav_filepath, channel = _wav_take("plan-b2-side1-001")
    corpus_bbc.keyboard.type('*TAPE\rCHAIN""\r')
    result = {}
    thread = threading.Thread(
        target=lambda: result.update(summary=corpus_bbc.cassette.stream_wav(wav_filepath, channel=channel)))
    thread.start()
    try:
        deadline = time.monotonic() + 10
        while corpus_bbc.cassette.status().tape is None and time.monotonic() < deadline:
            time.sleep(0.05)
        corpus_bbc.cassette.play()
        corpus_bbc.expect("Press space to start", timeout=420, sample_interval_seconds=5.0)
        status = corpus_bbc.cassette.status()
        assert status.live_overruns == 0
    finally:
        corpus_bbc.cassette.eject()
        thread.join(30)
