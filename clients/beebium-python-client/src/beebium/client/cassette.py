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

"""The cassette deck for the beebium client.

One deck behind the Serial ULA's cassette interface. The deck is the user's
transport: inserting a tape leaves it stopped at the start, and playing is a
separate request. The tape moves only while the deck plays and the machine's
motor relay is closed, which the filing system does when it wants the tape
(``*TAPE`` then ``CHAIN""`` or ``*LOAD``).
"""

from __future__ import annotations

import time
import wave
from array import array
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

from beebium.client._proto import cassette_pb2, cassette_pb2_grpc
from beebium.client._repr import facade_repr
from beebium.client.exceptions import CassetteError, CreateTapeError


class TapeTransport(Enum):
    """What the deck is doing: stopped, playing, recording, or winding."""

    STOPPED = "stopped"
    PLAYING = "playing"
    RECORDING = "recording"
    REWINDING = "rewinding"
    FAST_FORWARDING = "fast_forwarding"


_TRANSPORT_FROM_PROTO = {
    cassette_pb2.TAPE_TRANSPORT_STOPPED: TapeTransport.STOPPED,
    cassette_pb2.TAPE_TRANSPORT_PLAYING: TapeTransport.PLAYING,
    cassette_pb2.TAPE_TRANSPORT_RECORDING: TapeTransport.RECORDING,
    cassette_pb2.TAPE_TRANSPORT_REWINDING: TapeTransport.REWINDING,
    cassette_pb2.TAPE_TRANSPORT_FAST_FORWARDING: TapeTransport.FAST_FORWARDING,
}


class CassetteErrorKind(Enum):
    """Why a cassette operation failed.

    Travels on :class:`~beebium.client.exceptions.CassetteError` with the
    server's sentence, so callers can branch without parsing it.
    """

    UNSPECIFIED = "unspecified"
    NO_CASSETTE_INTERFACE = "no_cassette_interface"
    CANNOT_OPEN = "cannot_open"
    UNREADABLE = "unreadable"
    UNSUPPORTED_FORMAT = "unsupported_format"
    INVALID_IMAGE = "invalid_image"
    DECK_OCCUPIED = "deck_occupied"
    DECK_EMPTY = "deck_empty"
    INVALID_POSITION = "invalid_position"
    READ_ONLY = "read_only"
    LIVE_SOURCE = "live_source"
    INVALID_FORMAT = "invalid_format"
    NOT_READY = "not_ready"


class CreateTapeErrorKind(Enum):
    """Why a blank tape could not be created; on
    :class:`~beebium.client.exceptions.CreateTapeError`."""

    UNSPECIFIED = "unspecified"
    UNKNOWN_LABEL = "unknown_label"
    DESTINATION_EXISTS = "destination_exists"
    WRITE_FAILED = "write_failed"
    WRONG_EXTENSION = "wrong_extension"


_CREATE_TAPE_ERROR_KIND_FROM_PROTO = {
    cassette_pb2.CREATE_TAPE_ERROR_KIND_UNSPECIFIED: CreateTapeErrorKind.UNSPECIFIED,
    cassette_pb2.CREATE_TAPE_ERROR_KIND_UNKNOWN_LABEL: CreateTapeErrorKind.UNKNOWN_LABEL,
    cassette_pb2.CREATE_TAPE_ERROR_KIND_DESTINATION_EXISTS: CreateTapeErrorKind.DESTINATION_EXISTS,
    cassette_pb2.CREATE_TAPE_ERROR_KIND_WRITE_FAILED: CreateTapeErrorKind.WRITE_FAILED,
    cassette_pb2.CREATE_TAPE_ERROR_KIND_WRONG_EXTENSION: CreateTapeErrorKind.WRONG_EXTENSION,
}


_ERROR_KIND_FROM_PROTO = {
    cassette_pb2.CASSETTE_ERROR_KIND_UNSPECIFIED: CassetteErrorKind.UNSPECIFIED,
    cassette_pb2.CASSETTE_ERROR_KIND_NO_CASSETTE_INTERFACE: CassetteErrorKind.NO_CASSETTE_INTERFACE,
    cassette_pb2.CASSETTE_ERROR_KIND_CANNOT_OPEN: CassetteErrorKind.CANNOT_OPEN,
    cassette_pb2.CASSETTE_ERROR_KIND_UNREADABLE: CassetteErrorKind.UNREADABLE,
    cassette_pb2.CASSETTE_ERROR_KIND_UNSUPPORTED_FORMAT: CassetteErrorKind.UNSUPPORTED_FORMAT,
    cassette_pb2.CASSETTE_ERROR_KIND_INVALID_IMAGE: CassetteErrorKind.INVALID_IMAGE,
    cassette_pb2.CASSETTE_ERROR_KIND_DECK_OCCUPIED: CassetteErrorKind.DECK_OCCUPIED,
    cassette_pb2.CASSETTE_ERROR_KIND_DECK_EMPTY: CassetteErrorKind.DECK_EMPTY,
    cassette_pb2.CASSETTE_ERROR_KIND_INVALID_POSITION: CassetteErrorKind.INVALID_POSITION,
    cassette_pb2.CASSETTE_ERROR_KIND_READ_ONLY: CassetteErrorKind.READ_ONLY,
    cassette_pb2.CASSETTE_ERROR_KIND_LIVE_SOURCE: CassetteErrorKind.LIVE_SOURCE,
    cassette_pb2.CASSETTE_ERROR_KIND_INVALID_FORMAT: CassetteErrorKind.INVALID_FORMAT,
    cassette_pb2.CASSETTE_ERROR_KIND_NOT_READY: CassetteErrorKind.NOT_READY,
}


@dataclass(frozen=True)
class TapeMetadata:
    """A tape in the deck."""

    name: str
    url: str
    format: str  # "UEF", "CSW" or "WAV"
    title: str
    origin: str
    target_machine: str
    duration_seconds: float
    recordable: bool  # whether record() can record on it: a UEF whose file the server can write
    sample_rate: int = 0  # a WAV's rate in Hz; 0 for UEF and CSW
    channel: str = ""  # a stereo WAV's or live stream's channel, "left" or "right"; empty for a mono source
    record_refusal: str = ""  # when not recordable, why, as the sentence record() would raise


@dataclass(frozen=True)
class BlankTape:
    """A blank tape template the server ships, from which
    :meth:`Cassette.create_tape` makes a tape."""

    label: str  # the file name without its extension, e.g. "C15"
    filename: str  # e.g. "C15.uef"
    kind: str  # "tape"
    size_bytes: int
    duration_seconds: float


@dataclass(frozen=True)
class DeckStatus:
    """The deck as the server last published it."""

    has_cassette_interface: bool
    tape: TapeMetadata | None  # None when the deck is empty
    transport: TapeTransport
    position_seconds: float
    motor_on: bool  # the machine's relay, whether or not a tape is in
    ula_part: str  # "ferranti" or "vlsi"
    live_ring_fill: float = 0.0  # a live tape's ring, 0 to 1
    live_overruns: int = 0  # samples a live tape dropped because its ring was full
    live_underruns: int = 0  # samples of silence a live tape made up because its ring was empty
    signal_map_ready: bool = False  # whether the tape's signal map is ready (see Cassette.signal_map)
    signal_map_generation: int = 0  # how many signal maps the server has published; fetch again when it changes
    append_refusal: str = ""  # why seek_to_append_point() would be refused now, as its sentence; empty if it would not


@dataclass(frozen=True)
class AppendPoint:
    """Where seek_to_append_point() left the head."""

    position_seconds: float
    at_end: bool  # the point was at or past the end of the tape: no room, and the deck stopped there


@dataclass(frozen=True)
class SignalMap:
    """Where the tape carries signal: one level a second, 0 (silent
    throughout) to 255 (signal throughout), the fraction of that second
    within a run of edges, carrier included."""

    ready: bool  # False while the map is being computed, and for a live tape
    seconds: int  # the number of levels, the tape's duration rounded up
    levels: bytes  # one a second, empty until ready


@dataclass(frozen=True)
class StreamAudioSummary:
    """How a live stream went, when it ended."""

    samples_received: int
    samples_dropped: int  # overruns: the ring was full
    silence_samples_inserted: int  # underruns: the ring was empty


def _tape_metadata(proto: cassette_pb2.TapeMetadata) -> TapeMetadata:
    return TapeMetadata(
        name=proto.name,
        url=proto.url,
        format=proto.format,
        title=proto.title,
        origin=proto.origin,
        target_machine=proto.target_machine,
        duration_seconds=proto.duration_seconds,
        recordable=proto.recordable,
        sample_rate=proto.sample_rate,
        channel=proto.channel,
        record_refusal=proto.record_refusal,
    )


def _deck_status(proto: cassette_pb2.DeckStatus) -> DeckStatus:
    return DeckStatus(
        has_cassette_interface=proto.has_cassette_interface,
        tape=_tape_metadata(proto.tape) if proto.tape_inserted else None,
        transport=_TRANSPORT_FROM_PROTO.get(proto.transport, TapeTransport.STOPPED),
        position_seconds=proto.position_seconds,
        motor_on=proto.motor_on,
        ula_part=proto.ula_part,
        live_ring_fill=proto.live_ring_fill,
        live_overruns=proto.live_overruns,
        live_underruns=proto.live_underruns,
        signal_map_ready=proto.signal_map_ready,
        signal_map_generation=proto.signal_map_generation,
        append_refusal=proto.append_refusal,
    )


def _url_of(path_or_url: str | Path) -> str:
    """A path as a file:// URL; a URL (anything with "://") as it is."""
    if isinstance(path_or_url, Path):
        return path_or_url.resolve().as_uri()
    if "://" in path_or_url:
        return path_or_url
    return Path(path_or_url).resolve().as_uri()


def _check(response) -> None:
    if not response.success:
        raise CassetteError(response.error,
                            kind=_ERROR_KIND_FROM_PROTO.get(response.kind, CassetteErrorKind.UNSPECIFIED))


class Cassette:
    """The cassette deck: insert, eject, play, stop, seek, and its status.

    Every unsuccessful request raises :class:`~beebium.client.exceptions.CassetteError`,
    whose message is the server's sentence and whose ``kind`` is a
    :class:`CassetteErrorKind`.
    """

    def __init__(self, stub: cassette_pb2_grpc.CassetteServiceStub):
        self._stub = stub

    def __repr__(self) -> str:
        return facade_repr(self, "the cassette deck")

    def insert(self, path_or_url: str | Path, channel: str | None = None) -> TapeMetadata:
        """Insert a UEF, CSW or WAV tape image the server can reach, judged by
        content. A WAV is played through a model of the machine's cassette input.

        The deck is left stopped at the start of the tape.

        Args:
            path_or_url: The tape image.
            channel: For a stereo WAV, "left" or "right"; by default the server
                plays the louder.
        """
        request = cassette_pb2.InsertTapeRequest(url=_url_of(path_or_url))
        if channel is not None:
            request.channel = channel
        response = self._stub.InsertTape(request)
        _check(response)
        return _tape_metadata(response.tape)

    def eject(self) -> None:
        """Stop and remove the tape."""
        _check(self._stub.EjectTape(cassette_pb2.EjectTapeRequest()))

    def play(self) -> None:
        """Press play. The tape moves while the machine's motor relay is closed."""
        _check(self._stub.Play(cassette_pb2.PlayRequest()))

    def stop(self) -> None:
        """Press stop."""
        _check(self._stub.Stop(cassette_pb2.StopRequest()))

    def seek(self, seconds: float) -> None:
        """Move the tape to `seconds` from its start; beyond the end lands at the end."""
        _check(self._stub.Seek(cassette_pb2.SeekRequest(position_seconds=seconds)))

    def seek_to_append_point(self) -> AppendPoint:
        """Move the tape to just past its last recording: the end of the last
        second of its signal map with any signal, plus two seconds, ready to
        record another after it; the start of a silent tape. Like seek(), it
        ends a recording or a wind first and a playing deck plays on; a point
        at or past the end stops the deck there, with `at_end` set.

        Raises:
            CassetteError: DECK_EMPTY, LIVE_SOURCE, or NOT_READY while the
                tape's signal map is still being computed.
        """
        response = self._stub.SeekToAppendPoint(cassette_pb2.SeekToAppendPointRequest())
        _check(response)
        return AppendPoint(position_seconds=response.position_seconds, at_end=response.at_end)

    def rewind(self) -> None:
        """Wind backwards at 30 times real speed, whether or not the motor
        relay is closed, until stop, play, a seek, eject or the start of the
        tape, where the deck stops. ``seek(0)`` returns to the start at once."""
        _check(self._stub.Rewind(cassette_pb2.RewindRequest()))

    def fast_forward(self) -> None:
        """Wind forwards at 30 times real speed, whether or not the motor relay
        is closed, until stop, play, a seek, eject or the end of the tape,
        where the deck stops."""
        _check(self._stub.FastForward(cassette_pb2.FastForwardRequest()))

    def record(self) -> None:
        """Press record. From here, while the motor relay is closed, the tape
        moves and what passes under the head is replaced by what the machine
        sends, or by silence. Stop, play, a seek, a wind, eject and the end of
        the tape end it, and the tape is then written back to its file.

        Raises:
            CassetteError: With kind READ_ONLY for a tape Beebium only reads
                (a CSW), or DECK_EMPTY.
        """
        _check(self._stub.Record(cassette_pb2.RecordRequest()))

    def list_blank_tapes(self) -> list[BlankTape]:
        """The blank tape templates the server ships, in file name order."""
        response = self._stub.ListBlankTapes(cassette_pb2.ListBlankTapesRequest())
        return [
            BlankTape(label=tape.label, filename=tape.filename, kind=tape.kind,
                      size_bytes=tape.size_bytes, duration_seconds=tape.duration_seconds)
            for tape in response.tapes
        ]

    def create_tape(self, label: str, destination_filepath: str | Path, *,
                    overwrite: bool = False) -> Path:
        """Create a blank tape: copy the template with this label to a path on
        the server's host. Insert it with :meth:`insert`.

        A `destination_filepath` without an extension gets ".uef"; one with
        another extension is refused.

        Returns:
            The absolute path of the new tape.

        Raises:
            CreateTapeError: If the label is unknown (UNKNOWN_LABEL), the
                destination exists and `overwrite` is not set
                (DESTINATION_EXISTS), it ends in another extension
                (WRONG_EXTENSION), or the copy cannot be written (WRITE_FAILED).
        """
        response = self._stub.CreateTape(cassette_pb2.CreateTapeRequest(
            label=label, destination_path=str(destination_filepath), overwrite=overwrite))
        if not response.success:
            raise CreateTapeError(
                response.error,
                kind=_CREATE_TAPE_ERROR_KIND_FROM_PROTO.get(response.kind, CreateTapeErrorKind.UNSPECIFIED))
        return Path(response.path)

    def stream_audio(self, samples: Iterable[bytes | array], sample_rate: int, *,
                     sample_format: str = "f32", channels: int = 1, channel: str | None = None,
                     device_name: str | None = None) -> StreamAudioSummary:
        """Play `samples` as the live tape, as a client capturing a deck's
        line-out would: little-endian chunks of "s16" or "f32" samples at
        `sample_rate`, mono or stereo (`channel` "left" or "right" for stereo).
        The deck gets the live tape, stopped, once the stream begins; call
        :meth:`play` (from another thread) to hear it. The relay does not gate
        it. Returns when the samples run out (the live tape is then ejected) or
        the tape is ejected.

        The server plays the stream in emulated time at its own rate and does
        not resample, so the samples should arrive at real time; see
        :meth:`stream_wav`.

        Raises:
            CassetteError: With kind DECK_OCCUPIED or INVALID_FORMAT if the
                stream is refused.
        """
        formats = {"s16": cassette_pb2.AUDIO_SAMPLE_FORMAT_S16, "f32": cassette_pb2.AUDIO_SAMPLE_FORMAT_F32}
        if sample_format not in formats:
            raise ValueError(f'sample_format is "s16" or "f32", not {sample_format!r}')
        audio_format = cassette_pb2.AudioInputFormat(
            sample_rate=sample_rate, sample_format=formats[sample_format], channels=channels,
            channel=channel or "", device_name=device_name or "")

        def chunks():
            yield cassette_pb2.AudioInputChunk(format=audio_format)
            for chunk in samples:
                data = chunk.tobytes() if isinstance(chunk, array) else bytes(chunk)
                yield cassette_pb2.AudioInputChunk(samples=data)

        summary = self._stub.StreamAudio(chunks())
        if summary.error:
            raise CassetteError(summary.error,
                                kind=_ERROR_KIND_FROM_PROTO.get(summary.kind, CassetteErrorKind.UNSPECIFIED))
        return StreamAudioSummary(samples_received=summary.samples_received,
                                  samples_dropped=summary.samples_dropped,
                                  silence_samples_inserted=summary.silence_samples_inserted)

    def stream_wav(self, wav_filepath: str | Path, *, realtime: bool = True, channel: str | None = None,
                   block_seconds: float = 0.01) -> StreamAudioSummary:
        """Play a 16-bit PCM WAV as the live tape, read in blocks and, with
        `realtime`, paced to wall-clock time, as a deck's line-out arrives.
        Blocking: start it on a thread and call :meth:`play` once the deck
        shows the live tape."""
        wav = wave.open(str(wav_filepath), "rb")
        if wav.getsampwidth() != 2:
            wav.close()
            raise ValueError("stream_wav plays 16-bit PCM WAVs")
        rate, wav_channels = wav.getframerate(), wav.getnchannels()
        frames = max(1, int(rate * block_seconds))

        def blocks():
            start = time.monotonic()
            sent = 0
            try:
                while True:
                    data = wav.readframes(frames)
                    if not data:
                        return
                    yield data
                    sent += len(data) // (2 * wav_channels)
                    if realtime:
                        delay = start + sent / rate - time.monotonic()
                        if delay > 0:
                            time.sleep(delay)
            finally:
                wav.close()

        return self.stream_audio(blocks(), rate, sample_format="s16", channels=wav_channels,
                                 channel=channel if wav_channels == 2 else None,
                                 device_name=Path(wav_filepath).name)

    def status(self) -> DeckStatus:
        """The deck's state, read without stopping the machine."""
        return _deck_status(self._stub.GetDeckStatus(cassette_pb2.GetDeckStatusRequest()))

    def signal_map(self) -> SignalMap:
        """Where the tape in the deck carries signal, a level a second.

        The server computes it off the emulation thread when a tape goes in
        and again when a recording ends; until then, and for a live tape,
        `ready` is False. DeckStatus.signal_map_generation changes each time
        a new map is ready.

        Raises:
            CassetteError: DECK_EMPTY with no tape, or NO_CASSETTE_INTERFACE.
        """
        response = self._stub.GetTapeSignalMap(cassette_pb2.GetTapeSignalMapRequest())
        _check(response)
        return SignalMap(ready=response.ready, seconds=response.seconds, levels=bytes(response.levels))

    def events(self, min_interval_ms: int = 100) -> Iterator[DeckStatus]:
        """The deck's status now, then on every change of presence, transport,
        relay or tape, and while it plays its position at most every
        `min_interval_ms` of emulated time. Close the iterator to stop."""
        stream = self._stub.SubscribeDeckStatus(
            cassette_pb2.SubscribeDeckStatusRequest(min_interval_ms=min_interval_ms))
        try:
            for proto in stream:
                yield _deck_status(proto)
        finally:
            stream.cancel()
