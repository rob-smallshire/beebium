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

from collections.abc import Iterator
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

from beebium.client._proto import cassette_pb2, cassette_pb2_grpc
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
    recordable: bool  # whether record() can record on it: a UEF can, a CSW or WAV cannot
    sample_rate: int = 0  # a WAV's rate in Hz; 0 for UEF and CSW
    channel: str = ""  # the WAV channel played, "left" or "right"; empty for UEF and CSW


@dataclass(frozen=True)
class BlankTape:
    """A blank tape template the server ships, from which
    :meth:`Cassette.create_tape` makes a tape."""

    label: str  # the file name without its extension, e.g. "Blank C15 (15 minutes)"
    filename: str  # e.g. "Blank C15 (15 minutes).uef"
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
    )


def _deck_status(proto: cassette_pb2.DeckStatus) -> DeckStatus:
    return DeckStatus(
        has_cassette_interface=proto.has_cassette_interface,
        tape=_tape_metadata(proto.tape) if proto.tape_inserted else None,
        transport=_TRANSPORT_FROM_PROTO.get(proto.transport, TapeTransport.STOPPED),
        position_seconds=proto.position_seconds,
        motor_on=proto.motor_on,
        ula_part=proto.ula_part,
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

    def status(self) -> DeckStatus:
        """The deck's state, read without stopping the machine."""
        return _deck_status(self._stub.GetDeckStatus(cassette_pb2.GetDeckStatusRequest()))

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
