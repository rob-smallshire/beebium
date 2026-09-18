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

"""CLI: report objective audio measures for a WAV, and compare two WAVs.

Examples::

    uv run python analyze.py measure capture.wav --band 7500 --plots out/
    uv run python analyze.py compare beebium.wav beebjit.wav --plots out/
"""

from __future__ import annotations

import argparse
import os

import numpy as np

import analysis as A


def _plot_spectrum(path: str, signals: dict[str, tuple[np.ndarray, int]],
                   title: str) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(10, 5))
    for label, (sig, rate) in signals.items():
        freqs, psd = A.welch_spectrum(sig - np.mean(sig), rate)
        ax.semilogy(freqs, psd + 1e-20, label=label, linewidth=0.9)
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("PSD")
    ax.set_title(title)
    ax.set_xlim(0, max(r for _, r in signals.values()) / 2)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


def _plot_spectrogram(path: str, sig: np.ndarray, rate: int, title: str) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from scipy import signal as ssig

    f, t, sxx = ssig.spectrogram(sig - np.mean(sig), fs=rate, nperseg=1024,
                                 noverlap=768)
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.pcolormesh(t, f, 10 * np.log10(sxx + 1e-20), shading="gouraud")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (Hz)")
    ax.set_title(title)
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


def measure(args) -> None:
    sig, rate = A.load_wav(args.wav)
    oob = A.out_of_band_energy(sig, rate, args.band)
    clicks = A.detect_clicks(sig, rate)
    spurs = A.spurious_tones(sig, rate)

    print(f"file:            {args.wav}")
    print(f"sample rate:     {rate} Hz")
    print(f"duration:        {len(sig) / rate:.3f} s ({len(sig)} samples)")
    print(f"DC offset:       {A.dc_offset(sig):+.5f}")
    print(f"RMS:             {A.rms(sig):.5f}")
    print(f"out-of-band >{args.band:.0f} Hz: {oob.out_of_band_fraction * 100:.2f}% "
          f"of energy ({oob.out_of_band_db:+.1f} dB vs in-band)")
    print(f"clicks:          {clicks.count} (threshold {clicks.threshold:.4f}, "
          f"max jump {clicks.max_jump:.4f})")
    print("spurious peaks (dB below loudest):")
    for s in spurs:
        print(f"   {s.freq:8.0f} Hz  {s.amplitude_db:+6.1f} dB")

    if args.plots:
        os.makedirs(args.plots, exist_ok=True)
        base = os.path.splitext(os.path.basename(args.wav))[0]
        _plot_spectrum(os.path.join(args.plots, f"{base}_spectrum.png"),
                       {base: (sig, rate)}, f"Spectrum: {base}")
        _plot_spectrogram(os.path.join(args.plots, f"{base}_spectrogram.png"),
                          sig, rate, f"Spectrogram: {base}")
        print(f"plots written to {args.plots}/")


def compare(args) -> None:
    ref, rr = A.load_wav(args.reference)
    test, tr = A.load_wav(args.test)
    if rr != tr:
        print(f"WARNING: sample rates differ ({rr} vs {tr}); comparison assumes {rr}")
    cmp = A.align_and_compare(ref, test, rr)
    oob_ref = A.out_of_band_energy(ref, rr, args.band)
    oob_test = A.out_of_band_energy(test, tr, args.band)

    print(f"reference:       {args.reference}")
    print(f"test:            {args.test}")
    print(f"alignment lag:   {cmp.lag_samples} samples ({cmp.lag_samples / rr * 1000:+.2f} ms)")
    print(f"correlation:     {cmp.correlation:.4f}")
    print(f"match SNR:       {cmp.snr_db:+.1f} dB")
    print(f"out-of-band >{args.band:.0f} Hz  reference: {oob_ref.out_of_band_fraction * 100:.2f}%  "
          f"test: {oob_test.out_of_band_fraction * 100:.2f}%")

    if args.plots:
        os.makedirs(args.plots, exist_ok=True)
        _plot_spectrum(os.path.join(args.plots, "compare_spectrum.png"),
                       {"reference": (ref, rr), "test": (test, tr)},
                       "Spectrum comparison")
        print(f"plots written to {args.plots}/")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    m = sub.add_parser("measure", help="objective measures for one WAV")
    m.add_argument("wav")
    m.add_argument("--band", type=float, default=7500.0,
                   help="out-of-band threshold in Hz (default 7500)")
    m.add_argument("--plots", default=None, help="directory for PNG plots")
    m.set_defaults(func=measure)

    c = sub.add_parser("compare", help="align and compare two WAVs")
    c.add_argument("reference")
    c.add_argument("test")
    c.add_argument("--band", type=float, default=7500.0)
    c.add_argument("--plots", default=None)
    c.set_defaults(func=compare)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
