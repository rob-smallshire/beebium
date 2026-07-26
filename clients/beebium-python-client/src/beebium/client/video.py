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

"""Video frame access for the beebium client."""

from __future__ import annotations

import array
import threading
from collections.abc import Callable, Iterator
from dataclasses import dataclass, field
from pathlib import Path

from beebium.client._proto import video_pb2, video_pb2_grpc
from beebium.client.exceptions import TimeoutError

# The standard MODE 7 page. A program can drive the SAA5050 into other shapes,
# so this is the nominal size, not a bound on what a screen can be.
TELETEXT_ROWS = 25
TELETEXT_COLUMNS = 40

# "To the far edge from the region's origin." The server clips a region to the
# captured grid, so this reaches the edge whatever the screen's actual size --
# unlike a fixed 40x25, which would truncate a custom-shaped display.
_TELETEXT_TO_EDGE = 0xFFFFFFFF


@dataclass(frozen=True)
class TeletextScreen:
    """A MODE 7 screen, or a region of one, read as characters."""

    active: bool
    """False when the display is not MODE 7, in which case the cells describe
    whatever was last shown in MODE 7 rather than anything current."""

    rows: int
    columns: int

    text: str
    """The region as text, converted server-side so every client agrees on what
    graphics, control codes, concealed cells and double-height rows copy as.
    Lines are joined with LF. For plain text prefer ``screen_text()``, which
    reads every mode and not only MODE 7; this field is here so a caller already
    holding the cells for their attributes need not make a second call."""

    frame_number: int

    cells: list
    """Row-major, ``rows * columns`` entries, each with the attributes in
    effect at that cell."""

    def cell(self, row: int, column: int):
        """The cell at a position within the returned region."""
        return self.cells[row * self.columns + column]


@dataclass(frozen=True)
class PixelRegion:
    """A rectangle in frame pixel coordinates.

    The origin is the top-left of the active area rather than of the bordered
    display, matching the frames the video service streams.
    """

    x: int
    y: int
    width: int
    height: int


@dataclass(frozen=True)
class ScreenTextCell:
    """One cell of a run: where it sits, and whether a glyph was recognised."""

    bounds: PixelRegion

    matched: bool
    """True when a glyph was recognised here; false when the cell had ink the
    font could not identify. An unmatched cell still copies as a space to keep
    columns aligned, but a client can leave it out of a highlight rather than
    dress a failed read up as a success. A genuine space is matched."""


@dataclass(frozen=True)
class ScreenTextRun:
    """A contiguous piece of text and where it was found."""

    text: str

    bounds: PixelRegion
    """Where the run was found, so a client can highlight exactly what it
    captured."""

    cell_width: int
    cell_height: int
    """The character cell geometry the run was read with, so a selection can
    snap to it. Zero when the run is not cell-aligned, as text written at the
    graphics cursor is."""

    cells: list[ScreenTextCell] = field(default_factory=list)
    """The run's cells in reading order, from the glyph-recognising strategy.
    Empty from the teletext strategy, whose cells are exact characters and all
    matched: a client then highlights the whole run's bounds."""


@dataclass(frozen=True)
class ScreenText:
    """Text read from the display, whatever mode is producing it."""

    supported: bool
    """True when at least one band of the requested region had a strategy that
    could read it.

    Distinct from readable-but-empty: a graphics screen that was read and found
    to contain no text is supported with no runs, whereas a display this build
    has no strategy for is unsupported. Which displays fall in the second group
    narrows as strategies are added, so a caller should treat it as "not this
    time" rather than "not ever"."""

    runs: tuple[ScreenTextRun, ...]
    """In reading order: bands top to bottom, and within a band by baseline
    then x."""

    text: str
    """The runs joined by the requested layout, for a caller that wants a
    string and not structure. Lines are joined with LF."""

    unreadable_cells: int
    """Cells a strategy tried to read and could not identify at all. Zero for a
    MODE 7 display, whose cells are exact character codes."""

    ambiguous_cells: int
    """Cells a strategy read but could not pin to a single character, because
    the font in use draws two characters identically. Also zero for MODE 7."""

    frame_number: int


@dataclass(frozen=True)
class ScreenBandGeometry:
    """The character grid for one band of scanlines, in frame pixels."""

    top: int
    """First scanline, inclusive."""

    bottom: int
    """One past the last."""

    cell_width: int
    cell_height: int

    column_pitch: int
    row_pitch: int
    """Cell-to-cell step. Equal to the cell size except where a mode leaves
    blank scanlines between rows, as MODE 3 and MODE 6 do: an eight-scanline
    glyph on a ten-scanline pitch."""

    origin_x: int
    origin_y: int
    """Where the grid starts within the band."""


@dataclass(frozen=True)
class ScreenGeometry:
    """The character grid the display currently implies, per band."""

    bands: tuple[ScreenBandGeometry, ...]
    frame_number: int


def _geometry_from_proto(proto) -> ScreenGeometry:
    """The band grid, from either GetScreenGeometry or a hold."""
    return ScreenGeometry(
        bands=tuple(
            ScreenBandGeometry(
                top=band.top,
                bottom=band.bottom,
                cell_width=band.cell_width,
                cell_height=band.cell_height,
                column_pitch=band.column_pitch,
                row_pitch=band.row_pitch,
                origin_x=band.origin_x,
                origin_y=band.origin_y,
            )
            for band in proto.bands
        ),
        frame_number=proto.frame_number,
    )


def _frame_from_proto(proto) -> Frame:
    """A frame, from either the stream or a hold."""
    return Frame(
        frame_number=proto.frame_number,
        cycle_count=proto.cycle_count,
        width=proto.width,
        height=proto.height,
        pixels=proto.pixels,
        left_border=proto.left_border,
        right_border=proto.right_border,
        top_border=proto.top_border,
        bottom_border=proto.bottom_border,
        display_width=proto.display_width,
        display_height=proto.display_height,
        field_order=proto.field_order,
        regions=tuple(
            DisplayRegion(
                start_line=r.start_line,
                end_line=r.end_line,
                pixel_width=r.pixel_width,
            )
            for r in proto.regions
        ),
    )


@dataclass(frozen=True)
class ScreenHold:
    """A screen held on the server for the life of a selection.

    A reading depends on the pixels, the band geometry, the teletext grid and
    the font in RAM, all of which move independently. Read at four different
    instants they describe a screen that never existed, so holding captures
    them together and later reads name the capture. The emulator keeps
    running: a hold is a copy, not a pause.
    """

    hold_id: int
    """Names the hold in later ``screen_text`` and ``screen_geometry`` calls."""

    geometry: ScreenGeometry
    """The grid the held screen implies, returned with the hold so it cannot
    drift from the pixels it describes."""

    frame: Frame | None
    """The held still, when ``include_frame`` was asked for, so a caller can
    show exactly the picture its reads are made against."""


@dataclass(frozen=True)
class VideoConfig:
    """Video configuration."""

    width: int
    height: int
    framerate_hz: int


@dataclass(frozen=True)
class DisplayRegion:
    """A display region within a frame (for split-screen modes)."""

    start_line: int
    end_line: int
    pixel_width: int


@dataclass
class Frame:
    """A video frame."""

    frame_number: int
    cycle_count: int
    width: int
    height: int
    pixels: bytes  # BGRA32 format

    # Border/overscan dimensions
    left_border: int = 0
    right_border: int = 0
    top_border: int = 0
    bottom_border: int = 0

    # Display scaling
    display_width: int = 0
    display_height: int = 0

    # Field order: 0=PROGRESSIVE, 1=EVEN_FIRST, 2=ODD_FIRST
    field_order: int = 0

    # Per-region geometry for split-screen modes
    regions: tuple[DisplayRegion, ...] = ()

    def to_pil_image(self):
        """Convert to PIL Image (requires Pillow).

        Returns:
            A PIL Image in RGBA format.

        Raises:
            ImportError: If Pillow is not installed.
        """
        from PIL import Image

        rgba = self._bgra_to_rgba(self.pixels)
        return Image.frombytes("RGBA", (self.width, self.height), rgba)

    def save_png(self, filepath: str | Path) -> None:
        """Save frame as PNG (requires Pillow).

        Args:
            filepath: Path to save the PNG file.

        Raises:
            ImportError: If Pillow is not installed.
        """
        self.to_pil_image().save(str(filepath))

    @staticmethod
    def _bgra_to_rgba(bgra: bytes) -> bytes:
        """Convert BGRA to RGBA by swapping B and R channels."""
        arr = array.array("B", bgra)
        for i in range(0, len(arr), 4):
            arr[i], arr[i + 2] = arr[i + 2], arr[i]  # Swap B and R
        return bytes(arr)


class FrameStreamHandle:
    """Handle for a background frame stream."""

    def __init__(self, thread: threading.Thread, stop_event: threading.Event):
        """Create a frame stream handle.

        Args:
            thread: The background thread running the stream.
            stop_event: Event to signal the stream to stop.
        """
        self._thread = thread
        self._stop_event = stop_event

    def stop(self, timeout: float = 1.0) -> None:
        """Stop the background stream.

        Args:
            timeout: Maximum time to wait for the thread to stop (seconds).
        """
        self._stop_event.set()
        self._thread.join(timeout)

    @property
    def is_running(self) -> bool:
        """True if the stream is still running."""
        return self._thread.is_alive()


class Video:
    """Video frame access.

    Provides both streaming and single-frame capture modes.

    Usage:
        # Get current video config
        config = bbc.video.config

        # Capture a single frame
        frame = bbc.video.capture_frame()
        frame.save_png("screenshot.png")

        # Stream frames
        for frame in bbc.video.stream_frames(max_frames=100):
            process(frame)
    """

    def __init__(self, stub: video_pb2_grpc.VideoServiceStub):
        """Create a video interface.

        Args:
            stub: The gRPC stub for the VideoService.
        """
        self._stub = stub
        self._config: VideoConfig | None = None

    @property
    def config(self) -> VideoConfig:
        """Get video configuration."""
        if self._config is None:
            self._config = self._get_config()
        return self._config

    def teletext_screen(
        self,
        *,
        row: int = 0,
        column: int = 0,
        rows: int | None = None,
        columns: int | None = None,
        flowed: bool = False,
    ) -> TeletextScreen:
        """Read the MODE 7 screen as attribute-rich cells.

        This is the way to the SAA5050's per-cell state -- foreground and
        background colour, character set, concealment, flash, double height,
        the cursor, and whether a cell is a control code -- resolved by the chip
        and carried on each cell. Nothing else exposes it: ``screen_text()``
        returns text and geometry but not these attributes.

        For plain text, prefer ``screen_text()``. It reads every mode rather
        than only MODE 7, and copy uses it; this call is for a caller that wants
        the attributes as well. The cells are captured after the SAA5050 has
        resolved the control codes, so there is no hardware-scroll offset to
        undo and no attribute state to re-derive.

        Only MODE 7 has characters to read. In a bitmap mode the returned
        screen has ``active`` False and describes whatever was last shown in
        MODE 7, so callers must check it.

        Args:
            row: First row of the region to read.
            column: First column of the region to read.
            rows: Number of rows; the rest of the screen when None.
            columns: Number of columns; the rest of the screen when None.
            flowed: Join a row that filled the region's width to the next
                without a line break, rejoining a line that wrapped. By
                default each row is its own line, preserving the shape of the
                selection.

        Returns:
            The region's cells and the text they convert to.
        """
        request = video_pb2.GetTeletextScreenRequest(
            layout=(
                video_pb2.TELETEXT_LAYOUT_FLOWED
                if flowed
                else video_pb2.TELETEXT_LAYOUT_ROWS
            )
        )
        if row or column or rows is not None or columns is not None:
            request.region.CopyFrom(
                video_pb2.TeletextScreenRegion(
                    row=row,
                    column=column,
                    rows=rows if rows is not None else _TELETEXT_TO_EDGE,
                    columns=columns if columns is not None else _TELETEXT_TO_EDGE,
                )
            )

        response = self._stub.GetTeletextScreen(request)
        return TeletextScreen(
            active=response.active,
            rows=response.rows,
            columns=response.columns,
            text=response.text,
            frame_number=response.frame_number,
            cells=list(response.cells),
        )

    def screen_text(
        self,
        *,
        region: tuple[int, int, int, int] | None = None,
        search: str = "anywhere",
        flowed: bool = False,
        characters: str = "codes",
        hold_id: int | None = None,
    ) -> ScreenText:
        """Read text from the display, whatever mode is producing it.

        Prefer this to ``teletext_screen`` and to reading screen memory. The
        caller selects in pixels -- the one coordinate system every mode
        shares -- and the server picks a reading strategy per band of
        scanlines, so a split screen is read a band at a time and the caller
        never learns which mode produced what.

        A display this build has no strategy for comes back with ``supported``
        False and no runs, rather than something stale with a flag attached.

        Args:
            region: ``(x, y, width, height)`` in frame pixels; the whole
                display when None. Clipped to the display rather than
                rejected.
            search: ``"anywhere"``, the default, reads all the text -- on the
                grid and placed freely with VDU 5 -- and is a strict superset
                of ``"aligned"``, which reads only the grid and is exact and
                cheaper, as a snapped drag wants. Pick one up front; there is
                no reason to ask for both. Independent of ``region``. Honoured
                by strategies that recognise glyphs in pixels; a MODE 7 display
                is always its grid.
            flowed: Join a run that reached the right edge to the next without
                a line break, rejoining a line that wrapped. By default each
                grid row is its own line, preserving the shape of the
                selection.
            characters: Which meaning a MODE 7 byte carries. ``"codes"``, the
                default, takes the byte at face value, so ``[``, ``]`` and
                ``^`` come back as themselves and a copied BASIC listing keeps
                its assembler blocks and exponentiation. ``"displayed"``
                reports the glyphs the SAA5050 actually drew for those eleven
                codes -- a left arrow, a right arrow, an up arrow and the rest
                -- for capturing a teletext screen as it looked. Only the
                caller knows which was meant. Ignored outside MODE 7, whose
                font is the MOS's and already ASCII.

        Returns:
            The runs, the text they join to, and how much was uncertain.
        """
        searches = {
            "anywhere": video_pb2.SCREEN_TEXT_SEARCH_ANYWHERE,
            "aligned": video_pb2.SCREEN_TEXT_SEARCH_ALIGNED,
        }
        if search not in searches:
            raise ValueError(
                f"Unknown search {search!r}; expected one of "
                f"{', '.join(sorted(searches))}"
            )

        repertoires = {
            "codes": video_pb2.SCREEN_TEXT_CHARACTERS_CODES,
            "displayed": video_pb2.SCREEN_TEXT_CHARACTERS_DISPLAYED,
        }
        if characters not in repertoires:
            raise ValueError(
                f"Unknown characters {characters!r}; expected one of "
                f"{', '.join(sorted(repertoires))}"
            )

        request = video_pb2.GetScreenTextRequest(
            search=searches[search],
            layout=(
                video_pb2.SCREEN_TEXT_LAYOUT_FLOWED
                if flowed
                else video_pb2.SCREEN_TEXT_LAYOUT_ROWS
            ),
            characters=repertoires[characters],
        )
        if hold_id is not None:
            request.hold_id = hold_id
        if region is not None:
            x, y, width, height = region
            request.region.CopyFrom(
                video_pb2.PixelRegion(x=x, y=y, width=width, height=height)
            )

        response = self._stub.GetScreenText(request)
        return ScreenText(
            supported=response.supported,
            runs=tuple(
                ScreenTextRun(
                    text=run.text,
                    bounds=PixelRegion(
                        x=run.bounds.x,
                        y=run.bounds.y,
                        width=run.bounds.width,
                        height=run.bounds.height,
                    ),
                    cell_width=run.cell_width,
                    cell_height=run.cell_height,
                    cells=[
                        ScreenTextCell(
                            bounds=PixelRegion(
                                x=cell.bounds.x,
                                y=cell.bounds.y,
                                width=cell.bounds.width,
                                height=cell.bounds.height,
                            ),
                            matched=cell.matched,
                        )
                        for cell in run.cells
                    ],
                )
                for run in response.runs
            ),
            text=response.text,
            unreadable_cells=response.unreadable_cells,
            ambiguous_cells=response.ambiguous_cells,
            frame_number=response.frame_number,
        )

    def screen_geometry(self, *, hold_id: int | None = None) -> ScreenGeometry:
        """Report the character grid the display currently implies, per band.

        Separate from ``screen_text`` because snapping a drag has to happen
        while the drag is in progress, when there is nothing to send yet. One
        call on mouse-down is ample.

        Every band reports a grid, including one no strategy can read text
        from: where the cells are and what is in them are separate questions.

        Returns:
            One band per run of scanlines sharing a character geometry. A
            split screen has more than one.
        """
        request = video_pb2.GetScreenGeometryRequest()
        if hold_id is not None:
            request.hold_id = hold_id
        return _geometry_from_proto(self._stub.GetScreenGeometry(request))

    def hold_screen(self, *, include_frame: bool = False) -> ScreenHold:
        """Hold the screen as it stands, so reads describe one still.

        Everything a reading depends on is captured together, so a selection
        made against a moving display reads the picture it was drawn on rather
        than whatever has been drawn since. The emulator keeps running.

        Args:
            include_frame: Also return the captured still, so a caller can
                display exactly the picture its reads will be made against.

        Returns:
            The hold. Pass its ``hold_id`` to ``screen_text`` and
            ``screen_geometry``, and release it when finished.
        """
        response = self._stub.HoldScreen(
            video_pb2.HoldScreenRequest(include_frame=include_frame)
        )
        return ScreenHold(
            hold_id=response.hold_id,
            geometry=_geometry_from_proto(response.geometry),
            frame=_frame_from_proto(response.frame) if response.HasField("frame") else None,
        )

    def release_screen(self, hold_id: int) -> None:
        """Let a held screen go.

        Holds expire on their own, so a client that dies does not leak one,
        but a client that has finished should say so.
        """
        self._stub.ReleaseScreen(video_pb2.ReleaseScreenRequest(hold_id=hold_id))

    def capture_frame(self, timeout: float = 1.0) -> Frame:
        """Capture a single frame.

        Starts a frame stream, captures one frame, and stops.

        Args:
            timeout: Maximum time to wait for a frame (seconds).

        Returns:
            The captured frame.

        Raises:
            TimeoutError: If no frame is received within timeout.
        """
        for frame in self.stream_frames(max_frames=1):
            return frame
        raise TimeoutError("No frame received")

    def stream_frames(
        self,
        max_frames: int | None = None,
        callback: Callable[[Frame], None] | None = None,
    ) -> Iterator[Frame]:
        """Stream frames from the emulator.

        Args:
            max_frames: Stop after this many frames (None = infinite).
            callback: Optional callback invoked for each frame.

        Yields:
            Frame objects as they arrive.
        """
        request = video_pb2.SubscribeFramesRequest()
        response = self._stub.SubscribeFrames(request)

        count = 0
        for proto_frame in response:
            regions = ()
            if hasattr(proto_frame, "regions"):
                regions = tuple(
                    DisplayRegion(
                        start_line=r.start_line,
                        end_line=r.end_line,
                        pixel_width=r.pixel_width,
                    )
                    for r in proto_frame.regions
                )
            frame = Frame(
                frame_number=proto_frame.frame_number,
                cycle_count=proto_frame.cycle_count,
                width=proto_frame.width,
                height=proto_frame.height,
                pixels=proto_frame.pixels,
                left_border=proto_frame.left_border,
                right_border=proto_frame.right_border,
                top_border=proto_frame.top_border,
                bottom_border=proto_frame.bottom_border,
                display_width=proto_frame.display_width,
                display_height=proto_frame.display_height,
                field_order=proto_frame.field_order,
                regions=regions,
            )
            if callback:
                callback(frame)
            yield frame
            count += 1
            if max_frames is not None and count >= max_frames:
                break

    def start_background_stream(self, callback: Callable[[Frame], None]) -> FrameStreamHandle:
        """Start streaming frames in a background thread.

        Args:
            callback: Callback invoked for each frame.

        Returns:
            A handle that can be used to stop the stream.
        """
        stop_event = threading.Event()

        def stream_thread():
            try:
                for frame in self.stream_frames():
                    if stop_event.is_set():
                        break
                    callback(frame)
            except Exception:
                pass  # Stream ended or cancelled

        thread = threading.Thread(target=stream_thread, daemon=True)
        thread.start()
        return FrameStreamHandle(thread, stop_event)

    def _get_config(self) -> VideoConfig:
        """Internal: call GetConfig RPC."""
        request = video_pb2.GetConfigRequest()
        response = self._stub.GetConfig(request)
        return VideoConfig(
            width=response.width,
            height=response.height,
            framerate_hz=response.framerate_hz,
        )
