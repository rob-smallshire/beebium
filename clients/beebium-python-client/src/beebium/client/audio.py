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

"""Audio sample streaming for the beebium client.

Wraps ``AudioService``: query the output format (sample rate and the set of
sources with their encodings) and subscribe to the stream of packed sample
chunks. This is the sample stream; static SN76489 register introspection lives
on ``bbc.sound`` (DeviceInspection), not here.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from enum import IntEnum

from beebium.client._proto import audio_pb2, audio_pb2_grpc


class SourceEncoding(IntEnum):
    """How to interpret each 32-bit source field within a sample."""

    # 4 x int8 channels, zero-centered (legacy).
    ENCODING_4X8BIT_SIGNED = audio_pb2.ENCODING_4X8BIT_SIGNED
    # 2 x int16 channels (e.g. Music 5000 stereo).
    ENCODING_2X16BIT_SIGNED = audio_pb2.ENCODING_2X16BIT_SIGNED
    # 1 x int32 channel (high quality).
    ENCODING_1X32BIT_SIGNED = audio_pb2.ENCODING_1X32BIT_SIGNED
    # Unused / silent source.
    ENCODING_SILENCE = audio_pb2.ENCODING_SILENCE
    # 4 x uint8 channels, unipolar (0 = silence). No longer used by the SN76489,
    # which now uses two ENCODING_2X16BIT_SIGNED fields.
    ENCODING_4X8BIT_UNSIGNED = audio_pb2.ENCODING_4X8BIT_UNSIGNED


@dataclass(frozen=True)
class AudioSource:
    """Metadata for one audio source (one 32-bit field per sample)."""

    source_index: int
    source_name: str  # "SN76489", "Speech", "Music5000", ...
    encoding: SourceEncoding
    channel_names: tuple[str, ...]  # MOS SOUND channel numbering: "0" (noise), "1".."3"
    group_id: int  # channel group (0 = ungrouped)


@dataclass(frozen=True)
class ChannelGroup:
    """A group of related audio sources, for UI organisation."""

    group_id: int
    group_name: str
    description: str
    color: str  # optional UI hint, e.g. "#FF5733"


@dataclass(frozen=True)
class AudioFormat:
    """How to interpret the packed sample data in each chunk."""

    sample_rate: int  # output sample rate (Hz)
    source_count: int  # number of 32-bit fields per sample
    sources: tuple[AudioSource, ...]
    groups: tuple[ChannelGroup, ...]


@dataclass(frozen=True)
class AudioChunk:
    """A batch of audio samples as they were generated."""

    sequence: int  # chunk sequence number (for drop detection)
    sample_count: int  # samples in this chunk
    cycle_count: int  # emulation cycle at start of chunk (reserved)
    # Packed sample data: sample_count x source_count x 4 bytes. Use the
    # AudioFormat sources' encodings to unpack each 32-bit field.
    samples: bytes


def _audio_source_from_proto(proto: audio_pb2.AudioSource) -> AudioSource:
    return AudioSource(
        source_index=proto.source_index,
        source_name=proto.source_name,
        encoding=SourceEncoding(proto.encoding),
        channel_names=tuple(proto.channel_names),
        group_id=proto.group_id,
    )


def _channel_group_from_proto(proto: audio_pb2.ChannelGroup) -> ChannelGroup:
    return ChannelGroup(
        group_id=proto.group_id,
        group_name=proto.group_name,
        description=proto.description,
        color=proto.color,
    )


def _audio_format_from_proto(proto: audio_pb2.AudioFormat) -> AudioFormat:
    return AudioFormat(
        sample_rate=proto.sample_rate,
        source_count=proto.source_count,
        sources=tuple(_audio_source_from_proto(s) for s in proto.sources),
        groups=tuple(_channel_group_from_proto(g) for g in proto.groups),
    )


def _audio_chunk_from_proto(proto: audio_pb2.AudioChunk) -> AudioChunk:
    return AudioChunk(
        sequence=proto.sequence,
        sample_count=proto.sample_count,
        cycle_count=proto.cycle_count,
        samples=proto.samples,
    )


class Audio:
    """Audio format query and sample streaming.

    Usage::

        fmt = bbc.audio.format
        print(fmt.sample_rate, [s.source_name for s in fmt.sources])

        for chunk in bbc.audio.subscribe(chunk_size=1024):
            process(chunk.samples)
    """

    def __init__(self, stub: audio_pb2_grpc.AudioServiceStub):
        self._stub = stub
        self._format: AudioFormat | None = None

    @property
    def format(self) -> AudioFormat:
        """The audio output format (sample rate, sources, encodings).

        Cached after first access: the format is fixed for a given machine
        configuration.
        """
        if self._format is None:
            response = self._stub.GetAudioFormat(audio_pb2.GetAudioFormatRequest())
            self._format = _audio_format_from_proto(response)
        return self._format

    def subscribe(self, chunk_size: int = 1024) -> Iterator[AudioChunk]:
        """Stream audio sample chunks as they are generated.

        Args:
            chunk_size: Samples per chunk (server default is 1024 when 0).

        Yields:
            AudioChunk objects until the stream is closed.
        """
        request = audio_pb2.SubscribeAudioRequest(chunk_size=chunk_size)
        for proto_chunk in self._stub.SubscribeAudio(request):
            yield _audio_chunk_from_proto(proto_chunk)
