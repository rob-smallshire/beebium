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

"""Video ULA access for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client._proto import debugger_pb2, debugger_pb2_grpc
from beebium.client._repr import facade_repr


@dataclass
class VideoUlaState:
    """Video ULA state snapshot.

    Includes control register and 16-entry palette (both write-only on hardware).
    """

    # Control register (0xFE20)
    control: int

    # Palette (16 entries: logical color -> physical color)
    palette: tuple[int, ...]

    # Control register bit masks
    CTRL_FLASH = 0x01  # Bit 0: Flash color select
    CTRL_TELETEXT = 0x02  # Bit 1: Teletext mode
    CTRL_LINE_WIDTH = 0x0C  # Bits 2-3: Line width mode
    CTRL_FAST_CLOCK = 0x10  # Bit 4: 2MHz CRTC clock
    CTRL_CURSOR_WIDTH = 0xE0  # Bits 5-7: Cursor width

    @property
    def flash_select(self) -> bool:
        """Flash color select (swaps colors 8-15 with 0-7)."""
        return bool(self.control & self.CTRL_FLASH)

    @property
    def teletext_mode(self) -> bool:
        """True if Mode 7 teletext mode is active."""
        return bool(self.control & self.CTRL_TELETEXT)

    @property
    def line_width_mode(self) -> int:
        """Line width mode (0-3): 0=10 chars, 1=20 chars, 2=40 chars, 3=80 chars."""
        return (self.control >> 2) & 0x03

    @property
    def fast_clock(self) -> bool:
        """True if 2MHz CRTC clock (high-res modes 0, 1, 2)."""
        return bool(self.control & self.CTRL_FAST_CLOCK)

    @property
    def cursor_width(self) -> int:
        """Cursor width bits (0-7)."""
        return (self.control >> 5) & 0x07

    def __str__(self) -> str:
        """Format Video ULA state for display."""
        mode_desc = "Teletext" if self.teletext_mode else f"Bitmap (LW={self.line_width_mode})"
        clock = "2MHz" if self.fast_clock else "1MHz"
        palette_str = " ".join(f"{p:X}" for p in self.palette)
        return f"Control={self.control:02X} ({mode_desc}, {clock})\nPalette: {palette_str}"


class VideoUlaPalette:
    """Sequence-like access to Video ULA palette.

    Allows indexed access: video_ula.palette[5] returns physical color for logical 5.
    """

    def __init__(self, video_ula: VideoUla):
        self._video_ula = video_ula

    def __repr__(self) -> str:
        return facade_repr(self, "the Video ULA's palette, by logical colour")

    def __getitem__(self, index: int) -> int:
        """Get physical color for logical color index (0-15)."""
        if not 0 <= index < 16:
            raise IndexError(f"Palette index {index} out of range (0-15)")
        return self._video_ula.state.palette[index]

    def __len__(self) -> int:
        return 16


class VideoUla:
    """Video ULA access.

    Provides read access to Video ULA state including the control register
    and 16-entry palette (both write-only on actual hardware).

    Usage:
        # Get full ULA state
        state = bbc.video_ula.state
        print(f"Teletext mode: {state.teletext_mode}")

        # Access palette
        physical = bbc.video_ula.palette[7]
        print(f"Logical 7 -> Physical {physical}")
    """

    def __init__(self, stub: debugger_pb2_grpc.DeviceInspectionStub):
        """Create a Video ULA interface.

        Args:
            stub: The gRPC stub for the DeviceInspection service.
        """
        self._stub = stub
        self._palette = VideoUlaPalette(self)

    def __repr__(self) -> str:
        return facade_repr(self, "the Video ULA")

    @property
    def state(self) -> VideoUlaState:
        """Get the current Video ULA state."""
        request = debugger_pb2.GetVideoUlaStateRequest()
        response = self._stub.GetVideoUlaState(request)

        return VideoUlaState(
            control=response.control,
            palette=tuple(response.palette),
        )

    @property
    def palette(self) -> VideoUlaPalette:
        """Indexed access to palette entries (0-15)."""
        return self._palette
