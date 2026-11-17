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

"""bbc.cassette.stream_audio and stream_wav: the live tape (#208, round 8)."""

from __future__ import annotations

import math
import threading
import time
from array import array
from pathlib import Path

import pytest
import tape_builder

from beebium.client import Beebium, CassetteError
from beebium.client.cassette import CassetteErrorKind, TapeTransport
from beebium.client.exceptions import ServerNotFoundError

ASSETS_DIRPATH = Path(__file__).resolve().parents[3] / "tests" / "assets"


@pytest.fixture
def bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    try:
        with Beebium.launch(mos_filepath=mos_filepath, basic_filepath=basic_filepath,
                            server=beebium_server_filepath) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _tone(seconds: float, rate: int = 48000) -> list[array]:
    """A 2400 Hz cosine, amplitude 0.5, as float32 chunks of 10 ms."""
    chunks = []
    per = rate // 100
    for c in range(int(seconds * 100)):
        chunks.append(array("f", (0.5 * math.cos(2 * math.pi * 2400 * (c * per + i) / rate) for i in range(per))))
    return chunks


def _wait_for(predicate, seconds: float = 5.0) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.02)
    return False


def test_stream_audio_plays_a_live_tape_and_summarises(bbc):
    chunks = _tone(0.5)
    summary_box = {}
    thread = threading.Thread(target=lambda: summary_box.update(
        summary=bbc.cassette.stream_audio(chunks, 48000, device_name="Test line in")))
    thread.start()
    thread.join(10)
    summary = summary_box["summary"]
    assert summary.samples_received == 24000
    assert summary.samples_dropped == 0
    assert bbc.cassette.status().tape is None   # the stream ended: the live tape went


def test_a_live_tape_refuses_what_it_cannot_do(bbc):
    gate = threading.Event()

    def slow_chunks():
        for chunk in _tone(2.0):
            if gate.is_set():
                return
            yield chunk
            time.sleep(0.01)

    thread = threading.Thread(target=lambda: bbc.cassette.stream_audio(slow_chunks(), 48000, device_name="Line in"))
    thread.start()
    try:
        assert _wait_for(lambda: bbc.cassette.status().tape is not None)
        status = bbc.cassette.status()
        assert status.tape.format == "LIVE" and status.tape.name == "Line in"
        assert status.tape.sample_rate == 48000
        assert status.transport == TapeTransport.STOPPED
        for operation in (lambda: bbc.cassette.seek(1.0), bbc.cassette.rewind, bbc.cassette.fast_forward,
                          bbc.cassette.record):
            with pytest.raises(CassetteError) as refused:
                operation()
            assert refused.value.kind == CassetteErrorKind.LIVE_SOURCE
        bbc.cassette.play()
        assert _wait_for(lambda: bbc.cassette.status().position_seconds > 0.3)
    finally:
        gate.set()
        thread.join(10)


def test_stream_audio_is_refused_with_a_tape_in(bbc, tmp_path: Path):
    bbc.cassette.insert(tape_builder.write(tmp_path / "tone.uef", tape_builder.uef_of_carrier(1.0)))
    with pytest.raises(CassetteError) as occupied:
        bbc.cassette.stream_audio(_tone(0.1), 48000)
    assert occupied.value.kind == CassetteErrorKind.DECK_OCCUPIED
    bbc.cassette.eject()
    with pytest.raises(CassetteError) as bad:
        bbc.cassette.stream_audio(_tone(0.1), 1000)
    assert bad.value.kind == CassetteErrorKind.INVALID_FORMAT


def _tokenised(tmp_path: Path) -> bytes:
    import subprocess
    tok = tmp_path / "tape_ok.tok"
    result = subprocess.run(
        ["uvx", "oaknut-basic", "tokenise", str(ASSETS_DIRPATH / "cassette" / "tape_ok.bas"), str(tok)],
        capture_output=True, text=True)
    if result.returncode != 0 or not tok.exists():
        pytest.skip("oaknut-basic is not available through uvx, so the test program cannot be tokenised.")
    return tok.read_bytes()


def test_stream_wav_in_real_time_loads_a_program(bbc, tmp_path: Path):
    wav = tmp_path / "tape_ok.wav"
    wav.write_bytes(tape_builder.wav_of_mos_file("TAPEOK", 0xFFFF0E00, 0xFFFF8023, _tokenised(tmp_path)))
    bbc.keyboard.type('*TAPE\rCHAIN""\r')
    result = {}
    thread = threading.Thread(target=lambda: result.update(summary=bbc.cassette.stream_wav(wav)))
    thread.start()
    try:
        assert _wait_for(lambda: bbc.cassette.status().tape is not None)
        bbc.cassette.play()
        assert _wait_for(lambda: bbc.memory.address.peek[0x70] == 42, 40.0)
    finally:
        thread.join(60)
    assert result["summary"].samples_dropped == 0
