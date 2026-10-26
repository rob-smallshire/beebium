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

"""Integration test for the Hybrid Music 5000 (hybrid-music-5000 extension).

A real Model B launched with ``--hybrid-music-5000`` runs BASIC that writes the
synthesiser's JIM paging register (&FCFF) and RAM (&FD00-&FDFF) as AMPLE would:
a square wave into wave 0 and one channel at 440 Hz. The test captures the
machine's audio and finds the tone in the Music 5000's frame field. Frequencies
are measured from the 48 kHz sample stream itself, which is emulated time;
every wait polls the guest's screen, never the wall clock alone.
"""

from __future__ import annotations

import struct
import time

import pytest

from beebium.client import Beebium
from beebium.client.audio import SourceEncoding

TIMEOUT = 30.0
SAMPLE_RATE = 48000

# 440 Hz: freq = 440 x 2^24 / 46875 = 157482.5 -> 157482 (bit 0 clear, so an
# add, not a phase-set): 157482 x 46875 / 2^24 = 439.9986 Hz.
FREQ = 157482
EXPECTED_HZ = FREQ * 46875 / 2**24

MARKER_EXPR = '"DO"+"NE"'
MARKER = "DONE"


def _screen(bbc: Beebium) -> str:
    text = bbc.video.screen_text().text
    return "\n".join(text) if isinstance(text, list) else text


def _wait_until(condition, description: str, timeout: float = TIMEOUT) -> None:
    """Poll `condition` until it holds, rather than sleeping blind."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.05)
    pytest.fail(f"timed out waiting for {description}")


def _type_line(bbc: Beebium, line: str) -> None:
    bbc.keyboard.type(line + "\r")
    bbc.keyboard.wait_until_typing_complete()


def _set_up_tone(bbc: Beebium, page_nibble: int) -> None:
    """Load a square wave into wave 0 and play it on column 0 at 440 Hz.

    Wave bytes are sign (bit 7, 1 positive) and log magnitude: &FF is full
    positive, &7F full negative. Column 0's registers are in page &x6/&x7 at
    rows &00/&10/&20 (frequency), &50 (wave select), &60 (amplitude &80: the
    DAC code is the wave byte's magnitude) and &70 (control 0: right side).
    """
    base = page_nibble << 4
    _type_line(bbc, f"?&FCFF=&{base:02X}:FORI%=0TO127:I%?&FD00=&FF+(I%>63)*&80:NEXT")
    _type_line(
        bbc,
        f"?&FCFF=&{base | 0x0E:02X}:?&FD50=0:?&FD60=&80:?&FD70=0:"
        f"?&FD00=&{FREQ & 0xFF:02X}:?&FD10=&{(FREQ >> 8) & 0xFF:02X}:"
        f"?&FD20=&{FREQ >> 16:02X}:PRINT {MARKER_EXPR}",
    )
    _wait_until(lambda: MARKER in _screen(bbc), "the registers to be written")


def _capture(bbc: Beebium, sample_count: int) -> tuple[list[tuple[int, ...]], int]:
    """Collect `sample_count` frames: per frame, each field's (lo, hi) int16 pair."""
    fmt = bbc.audio.format
    stride = fmt.source_count * 4
    frames: list[tuple[int, ...]] = []
    for chunk in bbc.audio.subscribe(chunk_size=1024):
        for i in range(chunk.sample_count):
            frames.append(struct.unpack_from(f"<{fmt.source_count * 2}h", chunk.samples, i * stride))
        if len(frames) >= sample_count:
            break
    return frames[:sample_count], fmt.source_count


def _frequency(signal: list[int]) -> float:
    """Frequency from upward zero crossings, interpolated between samples."""
    crossings: list[float] = []
    for n in range(1, len(signal)):
        a, b = signal[n - 1], signal[n]
        if a < 0 <= b:
            crossings.append(n - 1 + (-a) / (b - a))
    assert len(crossings) >= 3, "no tone found"
    return (len(crossings) - 1) * SAMPLE_RATE / (crossings[-1] - crossings[0])


def _field_channels(frames: list[tuple[int, ...]], field: int) -> tuple[list[int], list[int]]:
    return [f[2 * field] for f in frames], [f[2 * field + 1] for f in frames]


def test_audio_format_describes_the_music_5000(launch_bbc) -> None:
    bbc = launch_bbc(extra_args=["--hybrid-music-5000"])
    bbc.wait_until_ready()
    fmt = bbc.audio.format

    sources = {s.source_index: s for s in fmt.sources}
    assert sources[0].source_name == "SN76489"
    assert sources[1].source_name == "SN76489"
    assert not sources[0].bipolar
    music5000 = sources[2]
    assert music5000.source_name == "Music5000"
    assert music5000.encoding is SourceEncoding.ENCODING_2X16BIT_SIGNED
    assert music5000.channel_names == ("Left", "Right")
    assert music5000.bipolar
    assert sources[3].encoding is SourceEncoding.ENCODING_SILENCE
    assert sources[3].source_name == ""
    group_names = {g.group_id: g.group_name for g in fmt.groups}
    assert group_names[music5000.group_id] == "Music 5000"


def test_a_tone_written_through_jim_plays_in_field_2(launch_bbc) -> None:
    bbc = launch_bbc(extra_args=["--hybrid-music-5000"])
    bbc.wait_until_ready()
    _set_up_tone(bbc, 0x3)

    # Up to a second of audio from before the tone may still be buffered:
    # take 2.5 s and analyse the last second.
    frames, _ = _capture(bbc, int(2.5 * SAMPLE_RATE))
    tail = frames[-SAMPLE_RATE:]

    left, right = _field_channels(tail, 2)
    assert max(right) > 1000, "no tone on the right of field 2"
    assert all(v == 0 for v in left), "control 0 puts the channel on the right only"
    assert abs(_frequency(right) - EXPECTED_HZ) < 1.0

    # The SN76489 is silent at the BASIC prompt, and the Music 5000 does not
    # touch its fields.
    for field in (0, 1):
        lo, hi = _field_channels(tail, field)
        assert all(v == 0 for v in lo + hi), f"SN76489 field {field} is not silent"
    lo, hi = _field_channels(tail, 3)
    assert all(v == 0 for v in lo + hi), "field 3 has no source"


def test_a_music_3000_plays_in_field_3(launch_bbc) -> None:
    bbc = launch_bbc(extra_args=["--hybrid-music-5000", "--hybrid-music-5000", "unit=music-3000"])
    bbc.wait_until_ready()

    sources = {s.source_index: s for s in bbc.audio.format.sources}
    assert sources[2].source_name == "Music5000"
    assert sources[3].source_name == "Music3000"
    assert sources[3].bipolar

    # The tone goes to the Music 3000's pages (&5x); the Music 5000 is left
    # silent.
    _set_up_tone(bbc, 0x5)
    frames, _ = _capture(bbc, int(2.5 * SAMPLE_RATE))
    tail = frames[-SAMPLE_RATE:]

    _, right3 = _field_channels(tail, 3)
    assert max(right3) > 1000, "no tone in field 3"
    assert abs(_frequency(right3) - EXPECTED_HZ) < 1.0
    left2, right2 = _field_channels(tail, 2)
    assert all(v == 0 for v in left2 + right2), "the Music 5000 was not written"
