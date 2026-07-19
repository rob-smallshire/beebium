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
from dataclasses import dataclass
from pathlib import Path

from beebium.client._proto import video_pb2, video_pb2_grpc
from beebium.client.exceptions import TimeoutError


# A MODE 7 display is always this size.
TELETEXT_ROWS = 25
TELETEXT_COLUMNS = 40


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
    Lines are joined with LF."""

    frame_number: int

    cells: list
    """Row-major, ``rows * columns`` entries, each with the attributes in
    effect at that cell."""

    def cell(self, row: int, column: int):
        """The cell at a position within the returned region."""
        return self.cells[row * self.columns + column]


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
        """Read the MODE 7 screen as characters rather than pixels.

        Prefer this to reading screen memory. The cells are captured after the
        SAA5050 has resolved the control codes, so there is no hardware-scroll
        offset to undo and no attribute state to re-derive -- the two failings
        of the screen-memory scrapers this replaces.

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
                    rows=rows if rows is not None else TELETEXT_ROWS,
                    columns=columns if columns is not None else TELETEXT_COLUMNS,
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
