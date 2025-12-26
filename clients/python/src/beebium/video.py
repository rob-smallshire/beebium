# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

from beebium._proto import video_pb2, video_pb2_grpc
from beebium.exceptions import TimeoutError


@dataclass(frozen=True)
class VideoConfig:
    """Video configuration."""

    width: int
    height: int
    framerate_hz: int


@dataclass
class Frame:
    """A video frame."""

    frame_number: int
    cycle_count: int
    width: int
    height: int
    pixels: bytes  # BGRA32 format

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
            frame = Frame(
                frame_number=proto_frame.frame_number,
                cycle_count=proto_frame.cycle_count,
                width=proto_frame.width,
                height=proto_frame.height,
                pixels=proto_frame.pixels,
            )
            if callback:
                callback(frame)
            yield frame
            count += 1
            if max_frames is not None and count >= max_frames:
                break

    def start_background_stream(
        self, callback: Callable[[Frame], None]
    ) -> FrameStreamHandle:
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
