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

"""Offline model of the macOS client's audio chain (AudioRenderer.render).

Reproduces, sample for sample, what the macOS front end does to the SN76489
channels it receives: normalise each unipolar channel by half full scale, pass
it through the 20 Hz DC-removal high-pass biquad (Audio EQ Cookbook
coefficients, as in BiquadFilter.swift), apply the per-channel volume and the
fixed mix gain, pan with constant power, sum, apply the master volume, and soft
limit. There is no client low-pass: the server band-limits the chip with a
4th-order Butterworth at 7.2 kHz before decimating to 48 kHz. Use it to choose
the mix gain (AudioRenderer.mixGain) against the real high-pass rather than an
idealised square wave.

    uv run python client_chain.py sweep --gain 0.17
    uv run python client_chain.py sweep --gain 0.25 --q 1.41421356

`sweep` reports, for a full-volume square at each tone frequency, the filtered
channel peak, the per-side peak of four such channels in phase at centre pan,
and the fraction of those output samples beyond the limiter's knee.
"""

from __future__ import annotations

import argparse
import math
from collections.abc import Sequence

import numpy as np
from scipy.signal import lfilter

SAMPLE_RATE = 48000
#: Half the backend's full-scale channel level (Sn76489::FULL_SCALE = 16384).
HALF_FULL_SCALE = 8192.0
#: A full-volume channel's high level; its low level is 0 (unipolar).
FULL_SCALE_HIGH = 16384
HIGHPASS_CUTOFF_HZ = 20.0
#: Q of a second-order Butterworth section (BiquadFilter.butterworthQ).
BUTTERWORTH_Q = 1.0 / math.sqrt(2.0)
#: The soft limiter's knee: the identity below it.
KNEE = 0.8
#: AudioRenderer.mixGain.
MIX_GAIN = 0.1848

#: The render windows AudioRendererTests uses: settle three 4096-frame chunks,
#: then measure two.
CHUNK = 4096
SETTLE_FRAMES = 3 * CHUNK
MEASURE_FRAMES = 2 * CHUNK

#: Tone frequencies for a sweep, across the SN76489's audible range.
SWEEP_HZ = (125, 250, 500, 1000, 1500, 2000, 3000, 4000, 6000, 8000)


def biquad_coefficients(kind: str, cutoff_hz: float, q: float = BUTTERWORTH_Q) -> tuple[np.ndarray, np.ndarray]:
    """Audio EQ Cookbook low-pass or high-pass coefficients, normalised by a0,
    exactly as BiquadFilter.swift computes them."""
    omega = 2.0 * math.pi * cutoff_hz / SAMPLE_RATE
    sn, cs = math.sin(omega), math.cos(omega)
    alpha = sn / (2.0 * q)
    if kind == "lowpass":
        b = [(1.0 - cs) / 2.0, 1.0 - cs, (1.0 - cs) / 2.0]
    elif kind == "highpass":
        b = [(1.0 + cs) / 2.0, -(1.0 + cs), (1.0 + cs) / 2.0]
    else:
        raise ValueError(f"unknown filter kind {kind!r}")
    a0 = 1.0 + alpha
    return np.array(b) / a0, np.array([1.0, -2.0 * cs / a0, (1.0 - alpha) / a0])


def soft_limit(x: np.ndarray) -> np.ndarray:
    """AudioRenderer.softLimit: identity to the knee, then an exponential
    shoulder toward +/-1."""
    mag = np.abs(x)
    shoulder = (1.0 - KNEE) * (1.0 - np.exp(-(mag - KNEE) / (1.0 - KNEE)))
    return np.where(mag <= KNEE, x, np.sign(x) * (KNEE + shoulder))


def filter_channel(levels: np.ndarray, q: float = BUTTERWORTH_Q) -> np.ndarray:
    """One channel's unipolar levels through normalisation and the high-pass."""
    x = np.asarray(levels, dtype=np.float64) / HALF_FULL_SCALE
    return lfilter(*biquad_coefficients("highpass", HIGHPASS_CUTOFF_HZ, q), x)


def render(
    channels: Sequence[np.ndarray],
    *,
    gain: float = MIX_GAIN,
    pans: Sequence[float] | None = None,
    volumes: Sequence[float] | None = None,
    master: float = 1.0,
    q: float = BUTTERWORTH_Q,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """The stereo output for per-channel unipolar levels.

    Returns (left, right, left_before_limit, right_before_limit).
    """
    count = len(channels)
    pans = pans if pans is not None else [0.0] * count
    volumes = volumes if volumes is not None else [1.0] * count
    left = np.zeros(len(channels[0]))
    right = np.zeros(len(channels[0]))
    for levels, pan, volume in zip(channels, pans, volumes, strict=True):
        sample = filter_channel(levels, q) * volume * gain
        left += sample * math.cos((pan + 1.0) * math.pi / 4.0)
        right += sample * math.sin((pan + 1.0) * math.pi / 4.0)
    left *= master
    right *= master
    return soft_limit(left), soft_limit(right), left, right


def square(frequency_hz: int, frames: int) -> np.ndarray:
    """A full-volume square, high first, as AudioRendererTests generates it."""
    half_period = SAMPLE_RATE // (2 * frequency_hz)
    n = np.arange(frames)
    return np.where((n // half_period) % 2 == 0, FULL_SCALE_HIGH, 0)


def _settled(signal: np.ndarray) -> np.ndarray:
    return signal[SETTLE_FRAMES : SETTLE_FRAMES + MEASURE_FRAMES]


def channel_peak(frequency_hz: int, q: float = BUTTERWORTH_Q) -> float:
    """Settled peak of one full-volume channel after the high-pass, before the
    mix gain and pan (1.0 would be the ideal unit amplitude)."""
    levels = square(frequency_hz, SETTLE_FRAMES + MEASURE_FRAMES)
    return float(np.max(np.abs(_settled(filter_channel(levels, q)))))


def in_phase_mix(frequency_hz: int, channels: int, gain: float, q: float = BUTTERWORTH_Q) -> tuple[float, float]:
    """Per-side peak before the limiter, and the fraction of output samples
    beyond the knee, for `channels` full-volume squares in phase at centre."""
    levels = square(frequency_hz, SETTLE_FRAMES + MEASURE_FRAMES)
    _, _, left, _ = render([levels] * channels, gain=gain, q=q)
    settled = _settled(left)
    return float(np.max(np.abs(settled))), float(np.mean(np.abs(settled) > KNEE))


def sweep(args: argparse.Namespace) -> None:
    print(f"mix gain {args.gain}, filter Q {args.q:.4f}, knee {KNEE}")
    print(f"{'tone Hz':>8} {'channel peak':>13} {'4ch per side':>13} {'beyond knee':>12}")
    worst_hz, worst_peak = 0, 0.0
    for frequency_hz in SWEEP_HZ:
        peak = channel_peak(frequency_hz, args.q)
        side, beyond = in_phase_mix(frequency_hz, 4, args.gain, args.q)
        if peak > worst_peak:
            worst_hz, worst_peak = frequency_hz, peak
        print(f"{frequency_hz:>8} {peak:>13.4f} {side:>13.4f} {beyond:>11.1%}")
    print(f"worst channel peak {worst_peak:.4f} at {worst_hz} Hz; quarter-scale gain for it: {0.25 / worst_peak:.4f}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sweep", help="filtered peaks and knee exceedance across tone frequencies")
    s.add_argument("--gain", type=float, default=MIX_GAIN, help=f"mix gain (default {MIX_GAIN})")
    s.add_argument("--q", type=float, default=BUTTERWORTH_Q, help="biquad Q (default Butterworth, 1/sqrt 2)")
    s.set_defaults(func=sweep)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
