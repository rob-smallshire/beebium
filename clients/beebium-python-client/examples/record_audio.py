"""Audio: record a machine's sound to a 48 kHz WAV file.

Launches a server (from a preset, for example the B+ 128K Music 5000 machine),
optionally types lines and waits for text on the screen, then records the audio
stream for a number of seconds and writes it as a 16-bit stereo WAV with the
``wave`` module. The audio format from GetAudioFormat decides what each 32-bit
field of a frame holds:

- ``--field N`` writes field N's two int16 channels as left and right (the
  Music 5000 is field 2);
- ``--mix`` mixes every active field: bipolar fields as they are, unipolar
  fields (the SN76489) with each channel's DC removed, summed and scaled by one
  over the number of active fields.

The default is field 2 when the machine has a source there, else the mix.

    uv run python examples/record_audio.py --preset model-b-plus-128k-music-5000 \\
        --expect "Studio 5000 Main Menu" --type '\\t"pavane" LOAD RUN' \\
        --expect Pavane --seconds 30 --out pavane.wav

``--type`` takes Python string escapes (``\\t`` is TAB, which opens AMPLE's
command line); each line is followed by RETURN. Lines and waits run in the
order given. Nothing here sleeps: waits are on the screen or in emulated time.
"""

from __future__ import annotations

import argparse
import struct
import sys
import wave
from pathlib import Path

from beebium.client import Beebium
from beebium.client.audio import SourceEncoding


class _Step(argparse.Action):
    """Keep --type and --expect in command-line order."""

    def __call__(self, parser, namespace, value, option_string=None):
        steps = getattr(namespace, "steps", None) or []
        steps.append((self.dest, value))
        namespace.steps = steps


def _capture(bbc: Beebium, seconds: float) -> tuple[list[tuple[int, ...]], int]:
    fmt = bbc.audio.format
    wanted = int(seconds * fmt.sample_rate)
    frames: list[tuple[int, ...]] = []
    unpack = struct.Struct(f"<{fmt.source_count * 2}h").unpack_from
    stride = fmt.source_count * 4
    for chunk in bbc.audio.subscribe(chunk_size=1024):
        frames.extend(unpack(chunk.samples, i * stride) for i in range(chunk.sample_count))
        if len(frames) >= wanted:
            break
    return frames[:wanted], fmt.sample_rate


def _field(frames: list[tuple[int, ...]], index: int) -> list[tuple[float, float]]:
    return [(float(f[2 * index]), float(f[2 * index + 1])) for f in frames]


def _mix(bbc: Beebium, frames: list[tuple[int, ...]]) -> list[tuple[float, float]]:
    active = [s for s in bbc.audio.format.sources if s.encoding is not SourceEncoding.ENCODING_SILENCE]
    mixed = [[0.0, 0.0] for _ in frames]
    for source in active:
        pair = _field(frames, source.source_index)
        if source.bipolar:
            for out, (left, right) in zip(mixed, pair):
                out[0] += left
                out[1] += right
        else:
            # Unipolar channels: remove each one's DC, and put both in the middle.
            mean_a = sum(a for a, _ in pair) / max(len(pair), 1)
            mean_b = sum(b for _, b in pair) / max(len(pair), 1)
            for out, (a, b) in zip(mixed, pair):
                centred = (a - mean_a) + (b - mean_b)
                out[0] += centred
                out[1] += centred
    scale = 1.0 / max(len(active), 1)
    return [(left * scale, right * scale) for left, right in mixed]


def _write_wav(path: Path, samples: list[tuple[float, float]], rate: int) -> float:
    def clamp(v: float) -> int:
        return max(-32768, min(32767, round(v)))

    data = bytearray()
    peak = 0
    for left, right in samples:
        l_value, r_value = clamp(left), clamp(right)
        peak = max(peak, abs(l_value), abs(r_value))
        data += struct.pack("<hh", l_value, r_value)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(data))
    return peak / 32767.0


def main() -> int:
    parser = argparse.ArgumentParser(description="Record a machine's audio to a WAV file.")
    parser.add_argument("--preset", help="Preset id or path to launch with.")
    parser.add_argument("--variant", default="model-b-plus-128k", help="Server variant (default %(default)s).")
    parser.add_argument("--server", type=Path, help="Server executable (default: found by the client).")
    parser.add_argument("--type", dest="type", action=_Step, help="A line to type, with escapes; repeatable.")
    parser.add_argument("--expect", dest="expect", action=_Step, help="Text to wait for on the screen; repeatable.")
    parser.add_argument("--timeout", type=float, default=120.0, help="Seconds to wait for each --expect.")
    parser.add_argument("--seconds", type=float, default=10.0, help="Seconds of audio to record.")
    choice = parser.add_mutually_exclusive_group()
    choice.add_argument("--field", type=int, help="Record this frame field as stereo.")
    choice.add_argument("--mix", action="store_true", help="Record a mix of every active field.")
    parser.add_argument("--out", type=Path, required=True, help="WAV file to write.")
    args = parser.parse_args()

    extra = ["--preset", args.preset] if args.preset else []
    with Beebium.launch(variant=args.variant, server=args.server, extra_args=extra, startup_timeout=60.0) as bbc:
        # A preset that boots a disc reads the keyboard only once it has
        # loaded, so wait on the screen (--expect), not on the keyboard.
        for kind, value in getattr(args, "steps", None) or []:
            if kind == "expect":
                bbc.expect(value, timeout=args.timeout)
            else:
                bbc.keyboard.type(value.encode().decode("unicode_escape") + "\r")
                bbc.keyboard.wait_until_typing_complete()

        fmt = bbc.audio.format
        print(f"{fmt.sample_rate} Hz, {fmt.source_count} field(s):")
        for s in fmt.sources:
            print(f"  [{s.source_index}] {s.source_name or '-'} {s.encoding.name} bipolar={s.bipolar}")

        field = args.field
        if field is None and not args.mix:
            has_field_2 = any(s.source_index == 2 and s.encoding is not SourceEncoding.ENCODING_SILENCE
                              for s in fmt.sources)
            field = 2 if has_field_2 else None
        frames, rate = _capture(bbc, args.seconds)
        samples = _field(frames, field) if field is not None else _mix(bbc, frames)

    peak = _write_wav(args.out, samples, rate)
    what = f"field {field}" if field is not None else "mix"
    print(f"wrote {args.out}: {len(frames)} frames ({len(frames) / rate:.1f} s) of {what}, peak {peak:.3f} of full scale")
    return 0


if __name__ == "__main__":
    sys.exit(main())
