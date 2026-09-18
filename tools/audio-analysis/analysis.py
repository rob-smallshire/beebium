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

"""Objective audio-quality measures for evaluating SN76489 sample playback.

All measures work on a mono float signal in roughly [-1, 1] plus its sample
rate. The functions here answer the questions issue #82 poses: how much energy
sits out of band, what spurious tones are present, where the clicks are, the DC
offset, and how a Beebium capture compares to a reference once the two are
time-aligned.
"""

from __future__ import annotations

import wave
from dataclasses import dataclass, field

import numpy as np


def load_wav(path: str) -> tuple[np.ndarray, int]:
    """Load a 16-bit PCM WAV as mono float in [-1, 1] with its sample rate."""
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        channels = w.getnchannels()
        frames = w.readframes(w.getnframes())
    data = np.frombuffer(frames, dtype=np.int16).astype(np.float64) / 32768.0
    if channels > 1:
        data = data.reshape(-1, channels).mean(axis=1)
    return data, rate


def save_wav(path: str, signal: np.ndarray, rate: int) -> None:
    """Write a mono float signal in [-1, 1] as 16-bit PCM."""
    pcm = np.clip(signal, -1.0, 1.0)
    pcm = np.round(pcm * 32767.0).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def dc_offset(signal: np.ndarray) -> float:
    """Mean sample value (DC bias)."""
    return float(np.mean(signal))


def rms(signal: np.ndarray) -> float:
    return float(np.sqrt(np.mean(signal ** 2)))


def bin_amplitude(signal: np.ndarray, rate: int, freq: float) -> float:
    """Single-frequency amplitude via Goertzel, normalised so a unit sine ~ 1.0."""
    n = len(signal)
    w = 2.0 * np.pi * freq / rate
    coeff = 2.0 * np.cos(w)
    s1 = s2 = 0.0
    for x in signal:
        s0 = x + coeff * s1 - s2
        s2, s1 = s1, s0
    real = s1 - s2 * np.cos(w)
    imag = s2 * np.sin(w)
    return 2.0 * np.hypot(real, imag) / n


def welch_spectrum(signal: np.ndarray, rate: int,
                   nfft: int = 8192) -> tuple[np.ndarray, np.ndarray]:
    """Averaged power spectral density (Welch). Returns (freqs, psd)."""
    from scipy import signal as sig

    nperseg = min(nfft, len(signal))
    freqs, psd = sig.welch(signal, fs=rate, nperseg=nperseg,
                           noverlap=nperseg // 2, window="hann",
                           detrend=False, scaling="density")
    return freqs, psd


@dataclass
class OutOfBandResult:
    total_energy: float
    in_band_energy: float
    out_of_band_energy: float
    out_of_band_fraction: float
    out_of_band_db: float  # 10log10(out/in)


def out_of_band_energy(signal: np.ndarray, rate: int,
                       band_hz: float) -> OutOfBandResult:
    """Fraction of spectral energy above ``band_hz``.

    For 15 kHz sample material the wanted audio sits below roughly half the
    update rate; energy above ``band_hz`` is carrier, alias images or grit.
    DC is removed first so a bias does not swamp the ratio.
    """
    x = signal - np.mean(signal)
    freqs, psd = welch_spectrum(x, rate)
    df = freqs[1] - freqs[0]
    total = float(np.sum(psd) * df)
    in_band = float(np.sum(psd[freqs <= band_hz]) * df)
    oob = float(np.sum(psd[freqs > band_hz]) * df)
    frac = oob / total if total > 0 else 0.0
    db = 10.0 * np.log10(oob / in_band) if in_band > 0 and oob > 0 else float("-inf")
    return OutOfBandResult(total, in_band, oob, frac, db)


@dataclass
class SpuriousTone:
    freq: float
    amplitude_db: float  # relative to the strongest peak


def spurious_tones(signal: np.ndarray, rate: int,
                   top: int = 12, floor_db: float = -60.0) -> list[SpuriousTone]:
    """Strongest spectral peaks, in dB relative to the loudest, as spur candidates."""
    from scipy import signal as sig

    x = signal - np.mean(signal)
    freqs, psd = welch_spectrum(x, rate)
    psd_db = 10.0 * np.log10(psd + 1e-20)
    peak_idx, _ = sig.find_peaks(psd_db, height=np.max(psd_db) + floor_db)
    peak_idx = sorted(peak_idx, key=lambda i: psd_db[i], reverse=True)[:top]
    ref = np.max(psd_db)
    return [SpuriousTone(float(freqs[i]), float(psd_db[i] - ref)) for i in peak_idx]


@dataclass
class ClickResult:
    count: int
    threshold: float
    max_jump: float
    indices: list[int] = field(default_factory=list)


def detect_clicks(signal: np.ndarray, rate: int,
                  sigma: float = 8.0) -> ClickResult:
    """Count sample-to-sample discontinuities far outside the normal step size.

    A dropped or late buffer shows up as a first difference many standard
    deviations beyond the signal's usual slew. The threshold is ``sigma`` times
    the robust (median-absolute-deviation) spread of the first difference.
    """
    diff = np.diff(signal)
    mad = np.median(np.abs(diff - np.median(diff))) + 1e-12
    robust_std = 1.4826 * mad
    threshold = sigma * robust_std
    idx = np.where(np.abs(diff) > threshold)[0]
    max_jump = float(np.max(np.abs(diff))) if len(diff) else 0.0
    return ClickResult(len(idx), float(threshold), max_jump, idx.tolist())


@dataclass
class CompareResult:
    lag_samples: int
    correlation: float
    snr_db: float  # reference power over error power after alignment/gain match


def align_and_compare(reference: np.ndarray, test: np.ndarray,
                      rate: int, max_lag_ms: float = 50.0) -> CompareResult:
    """Cross-correlate to align ``test`` to ``reference``, then report match SNR.

    Both signals are DC-removed and the test is gain-matched by least squares
    before the residual is measured, so the SNR reflects spectral/shape
    difference, not level or offset.
    """
    a = reference - np.mean(reference)
    b = test - np.mean(test)
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]

    max_lag = int(max_lag_ms * 1e-3 * rate)
    corr = np.correlate(a, b, mode="full")
    mid = len(corr) // 2
    lo, hi = mid - max_lag, mid + max_lag + 1
    window = corr[lo:hi]
    best = np.argmax(window) + lo
    lag = best - mid

    if lag >= 0:
        a_al, b_al = a[lag:], b[: len(b) - lag] if lag > 0 else b
    else:
        a_al, b_al = a[: len(a) + lag], b[-lag:]
    m = min(len(a_al), len(b_al))
    a_al, b_al = a_al[:m], b_al[:m]

    denom = float(np.dot(b_al, b_al)) + 1e-20
    gain = float(np.dot(a_al, b_al)) / denom
    error = a_al - gain * b_al
    ref_power = float(np.dot(a_al, a_al)) + 1e-20
    err_power = float(np.dot(error, error)) + 1e-20
    snr = 10.0 * np.log10(ref_power / err_power)
    norm = np.sqrt(ref_power * denom)
    correlation = float(np.dot(a_al, b_al)) / (norm + 1e-20)
    return CompareResult(int(lag), correlation, float(snr))
