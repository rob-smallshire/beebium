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

"""Boot a disc in a beebium server and record its 48 kHz audio stream to WAV.

Records the raw server output (no client-side filtering) so audio quality can
be judged independently of any front-end. The four SN76489 channels are written
both as a mono mix and as one WAV per channel.

Determinism caveat: the AudioService drops samples silently when its 1 s buffer
overflows, and the wire protocol carries no produced-sample count to detect it
(chunk.sequence is a per-stream chunk index, not a sample count). So capture
runs at real time by default, where the gRPC consumer keeps up and no samples
drop; a shortfall between the samples received and the emulated time elapsed is
reported as a proxy for any drops. Faster-than-real-time deterministic capture
would need a server seam (see the issue #82 findings note).

Run under the Python client's environment, e.g.::

    uv run --project clients/beebium-python-client \
        python tools/audio-analysis/capture_disc.py \
        --disc ~/Code/beebjit/test/sound/play_paradroid.ssd \
        --seconds 8 --out-dirpath /path/to/scratch/wav
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import tempfile
import threading
import time
import wave
from pathlib import Path

from beebium.client import Beebium


def _sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def _repo_root() -> Path:
    here = Path(__file__).resolve()
    for parent in here.parents:
        if (parent / "roms").is_dir() and (parent / "clients").is_dir():
            return parent
    raise RuntimeError("could not locate beebium repo root")


def _write_mono_wav(path: str, samples: list[int], rate: int) -> None:
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))


class _AudioRecorder:
    """Subscribe to the audio stream on a background thread and accumulate it."""

    def __init__(self, bbc: Beebium):
        self._bbc = bbc
        self._chunks: list[bytes] = []
        self._chunk_indices: list[int] = []
        self._sample_counts: list[int] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self) -> None:
        try:
            for chunk in self._bbc.audio.subscribe(chunk_size=1024):
                self._chunks.append(chunk.samples)
                self._chunk_indices.append(chunk.sequence)
                self._sample_counts.append(chunk.sample_count)
                if self._stop.is_set():
                    break
        except Exception:
            # The stream is torn down when the server stops; that is expected.
            pass

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    @property
    def total_samples(self) -> int:
        return sum(self._sample_counts)

    def dropped_samples(self) -> int:
        """Samples dropped, detected from the produced-index sequence.

        Each chunk's ``sequence`` is the produced index of its first sample
        (delivered plus dropped so far). Tracking the expected next index from
        the stream start, every jump beyond it is dropped samples; this counts
        drops before the first delivered chunk too.
        """
        dropped = 0
        expected = 0
        for seq, count in zip(self._chunk_indices, self._sample_counts):
            if seq > expected:
                dropped += seq - expected
            expected = seq + count
        return dropped

    def channels(self) -> tuple[list[int], list[int], list[int], list[int], list[int]]:
        """Return (mix, tone0, tone1, tone2, noise) as 16-bit signed sample lists.

        The SN76489 uses two 2x16 source fields, little-endian: source 0 =
        (tone0, tone1), source 1 = (tone2, noise); each channel is a signed
        16-bit unipolar value (0 = silence). DC is left in; the analysis stage
        removes it.
        """
        tone0: list[int] = []
        tone1: list[int] = []
        tone2: list[int] = []
        noise: list[int] = []
        mix: list[int] = []
        data = b"".join(self._chunks)
        # 4 sources x 4 bytes = 16 bytes per sample; sources 0 and 1 hold the SN.
        stride = 16
        for off in range(0, len(data) - stride + 1, stride):
            t0, t1, t2, nz = struct.unpack_from("<4h", data, off)
            tone0.append(t0)
            tone1.append(t1)
            tone2.append(t2)
            noise.append(nz)
            # Mix: average the four so the sum stays within 16-bit range.
            mix.append(max(-32768, min(32767, (t0 + t1 + t2 + nz) // 4)))
        return mix, tone0, tone1, tone2, noise


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--disc", required=True, help="path to the disc image to boot")
    p.add_argument("--seconds", type=float, default=8.0,
                   help="emulated/real seconds to record (default 8)")
    p.add_argument("--out-dirpath", required=True, help="directory for WAV files")
    p.add_argument("--name", default=None, help="base name (default: disc stem)")
    p.add_argument("--dfs-rom", default=None,
                   help="DFS ROM path (default: roms/acorn-dfs_2_26.rom)")
    p.add_argument("--mos-rom", default=None,
                   help="MOS ROM path (default: roms/acorn-mos_1_20.rom)")
    p.add_argument("--basic-rom", default=None,
                   help="BASIC ROM path (default: roms/bbc-basic_2.rom)")
    p.add_argument("--speed", type=float, default=1.0,
                   help="speed multiplier (1.0 real-time, 0.0 unlimited); "
                        "unlimited risks silent sample drops")
    args = p.parse_args()

    root = _repo_root()
    source_disc_filepath = Path(args.disc).expanduser().resolve()
    mos_filepath = Path(args.mos_rom) if args.mos_rom else root / "roms" / "acorn-mos_1_20.rom"
    basic_filepath = Path(args.basic_rom) if args.basic_rom else root / "roms" / "bbc-basic_2.rom"
    dfs_filepath = Path(args.dfs_rom) if args.dfs_rom else root / "roms" / "acorn-dfs_2_26.rom"
    name = args.name or source_disc_filepath.stem
    os.makedirs(args.out_dirpath, exist_ok=True)

    # Isolate the run: mount a fresh copy of the disc and give the server a fresh,
    # empty disc work directory. A per-user copy-on-write image damaged by one run
    # (e.g. a boot that writes back over filing-system workspace) must never leak
    # into the next, and the mounted image is hashed before and after so any
    # write-back to it is caught rather than silently trusted.
    run_dir = Path(tempfile.mkdtemp(prefix="beebium_capture_"))
    disc_filepath = run_dir / source_disc_filepath.name
    shutil.copyfile(source_disc_filepath, disc_filepath)
    os.environ["BEEBIUM_DISC_WORK_DIR"] = str(run_dir / "workdir")
    os.makedirs(os.environ["BEEBIUM_DISC_WORK_DIR"], exist_ok=True)
    disc_sha1_before = _sha1(disc_filepath)
    print(f"mounted a fresh copy of {source_disc_filepath.name} (sha1 {disc_sha1_before})")

    sample_rate = 48000

    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        extra_args=[
            "--fdc", "acorn-1770",
            "--sideways", f"slot=14:type=rom:image={dfs_filepath}",
        ],
        startup_timeout=20.0,
    ) as bbc:
        fmt = bbc.audio.format
        sample_rate = fmt.sample_rate
        print(f"audio format: {sample_rate} Hz, sources={[s.source_name for s in fmt.sources]}")

        recorder = _AudioRecorder(bbc)
        recorder.start()

        bbc.system.set_speed_multiplier(args.speed)
        bbc.debugger.ensure_running()
        # Auto-boot via Shift-Break so the disc's !BOOT runs.
        bbc.boot_disc(disc_filepath)

        wall_start = time.monotonic()
        time.sleep(args.seconds if args.speed else args.seconds)
        wall_elapsed = time.monotonic() - wall_start

        recorder.stop()
        bbc.system.set_speed_multiplier(1.0)

    mix, t0, t1, t2, nz = recorder.channels()
    _write_mono_wav(os.path.join(args.out_dirpath, f"{name}_mix.wav"), mix, sample_rate)
    _write_mono_wav(os.path.join(args.out_dirpath, f"{name}_tone0.wav"), t0, sample_rate)
    _write_mono_wav(os.path.join(args.out_dirpath, f"{name}_tone1.wav"), t1, sample_rate)
    _write_mono_wav(os.path.join(args.out_dirpath, f"{name}_tone2.wav"), t2, sample_rate)
    _write_mono_wav(os.path.join(args.out_dirpath, f"{name}_noise.wav"), nz, sample_rate)

    disc_sha1_after = _sha1(disc_filepath)

    received = recorder.total_samples
    dropped = recorder.dropped_samples()
    print(f"recorded {received} samples ({received / sample_rate:.2f}s) over "
          f"{wall_elapsed:.2f}s wall clock at speed x{args.speed:g}")
    print(f"dropped samples (from the produced-index sequence): {dropped}"
          + ("" if dropped == 0 else " -- capture is NOT drop-free"))
    if disc_sha1_after != disc_sha1_before:
        print(f"WARNING: the mounted disc image changed during the run "
              f"({disc_sha1_before} -> {disc_sha1_after}); the run wrote back to it. "
              f"Treat this capture with suspicion.")
    else:
        print(f"mounted disc unchanged (sha1 {disc_sha1_after})")
    print(f"wrote {name}_mix.wav and per-channel WAVs to {args.out_dirpath}/")
    shutil.rmtree(run_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
