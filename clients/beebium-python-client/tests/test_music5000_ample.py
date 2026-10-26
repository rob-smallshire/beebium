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

"""The Music 5000 plays real software: AMPLE Studio 5000 playing a piece.

The model-b-plus-128k-music-5000 preset is a B+ 128K with the Hybrid Music 5000
on the 1 MHz bus, the AMPLE Nucleus ROM in sideways slot 4 and its Help File
ROM in slot 5 (both extracted from the Music 5000 System Disk), and the system
disc, bundled, in drive 0. Its !BOOT is AMPLE Studio's own runner script (see
packaging/discs/music5000-parts/PROVENANCE.md), so the power-on boot goes
straight into the Studio 5000 main menu; the disc's sideways RAM loader is not
used. Launching the preset tests the preset too.

The test boots unpaced to the menu, then at 1x opens AMPLE's command line
(TAB), runs PIECE with ``"pavane" LOAD RUN`` and records the Music 5000's
audio field. It asserts on that field alone: silent before the piece; while it
plays, loud (RMS), zero-centred, unclipped, and with almost all its power below
5 kHz, where the board's analogue output filter puts it.

Measured (2026-10-26, Apple M-series): field 2 RMS 126.1, mean 0.05, peak 657
of 32767, 0.025 % of the power above 5 kHz; the test runs in about 15 s.

The SN76489 is deliberately not asserted on. While the piece plays all four of
its channels are at volume 15 (off), but an 8 s capture starting at the
command saw field 0 (tone 0, tone 1) reach 0.335 of full scale before the
piece began: a transient from AMPLE or the command line, not a fault in the
Music 5000's field.
"""

from __future__ import annotations

import cmath
import math
import struct

from beebium.client import Beebium

PRESET = "model-b-plus-128k-music-5000"
VARIANT = "model-b-plus-128k"
STUDIO_MENU = "Studio 5000 Main Menu"
PIECE = "pavane"
PIECE_TITLE = "Pavane"
SAMPLE_RATE = 48000
FIELD = 2
TIMEOUT = 120.0


def _capture_field(bbc: Beebium, seconds: float) -> list[tuple[int, int]]:
    """`seconds` of the field's (left, right) int16 pairs, as the stream delivers them."""
    fmt = bbc.audio.format
    unpack = struct.Struct(f"<{fmt.source_count * 2}h").unpack_from
    stride = fmt.source_count * 4
    wanted = int(seconds * SAMPLE_RATE)
    pairs: list[tuple[int, int]] = []
    for chunk in bbc.audio.subscribe(chunk_size=1024):
        for i in range(chunk.sample_count):
            frame = unpack(chunk.samples, i * stride)
            pairs.append((frame[2 * FIELD], frame[2 * FIELD + 1]))
        if len(pairs) >= wanted:
            break
    return pairs[:wanted]


def _fft(values: list[complex]) -> list[complex]:
    """Radix-2 FFT; len(values) must be a power of two."""
    n = len(values)
    if n == 1:
        return values
    even = _fft(values[0::2])
    odd = _fft(values[1::2])
    twiddled = [cmath.exp(-2j * math.pi * k / n) * odd[k] for k in range(n // 2)]
    return [even[k] + twiddled[k] for k in range(n // 2)] + [even[k] - twiddled[k] for k in range(n // 2)]


def _fraction_above(signal: list[float], cutoff_hz: float, block: int = 4096) -> float:
    """Fraction of the signal's power above `cutoff_hz`, over whole blocks."""
    cutoff_bin = int(cutoff_hz * block / SAMPLE_RATE)
    above = total = 0.0
    for start in range(0, len(signal) - block + 1, block):
        spectrum = _fft([complex(v) for v in signal[start:start + block]])
        powers = [abs(x) ** 2 for x in spectrum[1:block // 2]]
        total += sum(powers)
        above += sum(powers[cutoff_bin:])
    return above / total if total else 0.0


def test_ample_studio_plays_a_piece_through_the_music_5000(launch_bbc) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT, startup_timeout=60.0)
    # Booting into Studio 5000 is a fast-forward; the capture below runs at
    # real time so the stream is consumed as fast as it is made.
    bbc.system.set_speed_multiplier(0.0)
    bbc.expect(STUDIO_MENU, timeout=TIMEOUT)
    bbc.system.set_speed_multiplier(1.0)

    before = _capture_field(bbc, 1.0)
    assert all(left == 0 and right == 0 for left, right in before), "field 2 sounds before the piece"

    # TAB opens AMPLE's command line.
    bbc.keyboard.type(f'\t"{PIECE}" LOAD RUN\r')
    bbc.keyboard.wait_until_typing_complete()
    bbc.expect(PIECE_TITLE, timeout=TIMEOUT)

    pairs = _capture_field(bbc, 4.0)[SAMPLE_RATE:]  # the last 3 s
    samples = [float(v) for pair in pairs for v in pair]
    rms = math.sqrt(sum(v * v for v in samples) / len(samples))
    mean = sum(samples) / len(samples)
    peak = max(abs(v) for v in samples)
    mono = [(left + right) / 2.0 for left, right in pairs]
    above_5k = _fraction_above(mono, 5000.0)

    assert rms > 50.0, f"field 2 is not playing (rms {rms:.1f})"
    assert abs(mean) < 0.02 * rms, f"field 2 is not zero-centred (mean {mean:.2f}, rms {rms:.1f})"
    assert peak < 32767, "field 2 clips"
    assert above_5k < 0.05, f"{above_5k:.3f} of the power is above 5 kHz"
