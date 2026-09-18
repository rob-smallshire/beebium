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

"""Tests for the AudioService client (bbc.audio).

The conversion test is server-free (builds an audio_pb2.AudioFormat directly).
The integration tests launch a real server and read the format and a chunk.
"""

from __future__ import annotations

from beebium.client._proto import audio_pb2
from beebium.client.audio import (
    AudioChunk,
    AudioFormat,
    AudioSource,
    SourceEncoding,
    _audio_format_from_proto,
)

# --------------------------------------------------------------------------
# Server-free conversion test
# --------------------------------------------------------------------------


def test_audio_format_from_proto_maps_sources_and_encoding():
    # The SN76489 uses two ENCODING_2X16BIT_SIGNED fields.
    proto = audio_pb2.AudioFormat(sample_rate=48000, source_count=2)
    proto.sources.add(
        source_index=0,
        source_name="SN76489",
        encoding=audio_pb2.ENCODING_2X16BIT_SIGNED,
        channel_names=["1", "2"],
        group_id=1,
    )
    proto.sources.add(
        source_index=1,
        source_name="SN76489",
        encoding=audio_pb2.ENCODING_2X16BIT_SIGNED,
        channel_names=["3", "0"],
        group_id=1,
    )
    fmt = _audio_format_from_proto(proto)

    assert isinstance(fmt, AudioFormat)
    assert fmt.sample_rate == 48000
    assert fmt.source_count == 2
    assert len(fmt.sources) == 2
    source = fmt.sources[0]
    assert isinstance(source, AudioSource)
    assert source.source_name == "SN76489"
    assert source.encoding is SourceEncoding.ENCODING_2X16BIT_SIGNED
    assert source.channel_names == ("1", "2")
    assert fmt.sources[1].channel_names == ("3", "0")


# --------------------------------------------------------------------------
# Integration tests (real server)
# --------------------------------------------------------------------------


def test_format_reports_sample_rate_and_sources(bbc):
    fmt = bbc.audio.format
    assert fmt.sample_rate > 0
    assert fmt.source_count >= 1
    # A BBC always has the SN76489 internal sound chip as a source.
    assert any("SN76489" in s.source_name for s in fmt.sources)


def test_subscribe_yields_a_chunk(bbc):
    chunk = next(bbc.audio.subscribe(chunk_size=256))
    assert isinstance(chunk, AudioChunk)
    assert chunk.sample_count > 0
    assert isinstance(chunk.samples, bytes)
    # samples is sample_count x source_count x 4 bytes.
    expected = chunk.sample_count * bbc.audio.format.source_count * 4
    assert len(chunk.samples) == expected


def test_chunk_unpacks_according_to_format(bbc):
    """Unpack a chunk using GetAudioFormat, exercising the wire contract."""
    import struct

    fmt = bbc.audio.format
    # The SN76489 is delivered as two 2x16 signed fields.
    sn_sources = [s for s in fmt.sources if s.source_name == "SN76489"]
    assert len(sn_sources) == 2
    assert all(
        s.encoding is SourceEncoding.ENCODING_2X16BIT_SIGNED for s in sn_sources
    )

    chunk = next(bbc.audio.subscribe(chunk_size=256))
    stride = fmt.source_count * 4  # bytes per sample across all source fields
    assert len(chunk.samples) >= chunk.sample_count * stride
    for i in range(chunk.sample_count):
        base = i * stride
        # Unpack each SN source field as two little-endian signed 16-bit channels.
        for s in sn_sources:
            off = base + s.source_index * 4
            left, right = struct.unpack_from("<2h", chunk.samples, off)
            # Values are signed 16-bit; silence is 0.
            assert -32768 <= left <= 32767
            assert -32768 <= right <= 32767
