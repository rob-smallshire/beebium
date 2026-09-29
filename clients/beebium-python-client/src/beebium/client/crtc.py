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

"""MC6845 CRTC (Cathode Ray Tube Controller) access for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client._proto import debugger_pb2, debugger_pb2_grpc


@dataclass
class CrtcState:
    """MC6845 CRTC state snapshot.

    Includes all 18 registers (including write-only R0-R11) and internal timing state.
    """

    # All 18 registers (R0-R17)
    registers: tuple[int, ...]

    # Currently selected register (address register)
    address_register: int

    # Internal timing counters
    column: int  # Horizontal character position (0 to R0)
    row: int  # Vertical character row (0 to R4)
    raster: int  # Scanline within character row (0 to R9)
    char_addr: int  # Current character address (MA0-MA13)

    # Computed values
    screen_start: int  # R12/R13: Display start address
    cursor_position: int  # R14/R15: Cursor address

    # Sync state
    in_hsync: bool
    in_vsync: bool
    display_enabled: bool

    # Beam position within the field (with column, row and raster above). A
    # field starts at a vsync rising edge.
    odd_field: bool = True  # The field in progress is the odd (first) field; interlace only
    cycles_since_vsync: int = 0  # CPU cycles since the field started

    # Register name constants
    R0_HTOTAL = 0
    R1_HDISPLAYED = 1
    R2_HSYNC_POS = 2
    R3_SYNC_WIDTH = 3
    R4_VTOTAL = 4
    R5_VTOTAL_ADJ = 5
    R6_VDISPLAYED = 6
    R7_VSYNC_POS = 7
    R8_INTERLACE = 8
    R9_MAX_SCANLINE = 9
    R10_CURSOR_START = 10
    R11_CURSOR_END = 11
    R12_START_ADDR_HI = 12
    R13_START_ADDR_LO = 13
    R14_CURSOR_HI = 14
    R15_CURSOR_LO = 15
    R16_LIGHTPEN_HI = 16
    R17_LIGHTPEN_LO = 17

    # Convenience register accessors

    @property
    def htotal(self) -> int:
        """R0: Horizontal total (characters - 1)."""
        return self.registers[0]

    @property
    def hdisplayed(self) -> int:
        """R1: Horizontal displayed (characters)."""
        return self.registers[1]

    @property
    def hsync_pos(self) -> int:
        """R2: Horizontal sync position."""
        return self.registers[2]

    @property
    def sync_width(self) -> int:
        """R3: Sync widths (H in low nibble, V in high nibble)."""
        return self.registers[3]

    @property
    def hsync_width(self) -> int:
        """Horizontal sync width (from R3 low nibble)."""
        return self.registers[3] & 0x0F

    @property
    def vsync_width(self) -> int:
        """Vertical sync width (from R3 high nibble, 0 means 16)."""
        w = (self.registers[3] >> 4) & 0x0F
        return 16 if w == 0 else w

    @property
    def vtotal(self) -> int:
        """R4: Vertical total (rows - 1)."""
        return self.registers[4]

    @property
    def vtotal_adj(self) -> int:
        """R5: Vertical total adjust (scanlines)."""
        return self.registers[5]

    @property
    def vdisplayed(self) -> int:
        """R6: Vertical displayed (rows)."""
        return self.registers[6]

    @property
    def vsync_pos(self) -> int:
        """R7: Vertical sync position."""
        return self.registers[7]

    @property
    def max_scanline(self) -> int:
        """R9: Maximum scanline address (rows - 1)."""
        return self.registers[9] & 0x1F

    @property
    def interlace_mode(self) -> int:
        """R8: Interlace and skew mode."""
        return self.registers[8]

    @property
    def is_interlaced(self) -> bool:
        """True if interlace sync and video mode is active (Mode 7)."""
        return (self.registers[8] & 0x03) == 0x03

    def __str__(self) -> str:
        """Format CRTC state for display."""
        return (
            f"R0-R3: H={self.htotal:02X} D={self.hdisplayed:02X} "
            f"S={self.hsync_pos:02X} W={self.sync_width:02X}\n"
            f"R4-R7: V={self.vtotal:02X} A={self.vtotal_adj:02X} "
            f"D={self.vdisplayed:02X} S={self.vsync_pos:02X}\n"
            f"R8-R9: I={self.interlace_mode:02X} M={self.max_scanline:02X}\n"
            f"Screen={self.screen_start:04X} Cursor={self.cursor_position:04X}\n"
            f"Col={self.column} Row={self.row} Raster={self.raster} Addr={self.char_addr:04X}\n"
            f"HSync={self.in_hsync} VSync={self.in_vsync} Display={self.display_enabled}"
        )


class CrtcRegisters:
    """Sequence-like access to CRTC registers.

    Allows indexed access: crtc.registers[5] returns R5.
    """

    def __init__(self, crtc: Crtc):
        self._crtc = crtc

    def __getitem__(self, index: int) -> int:
        """Get register value by index (0-17)."""
        if not 0 <= index < 18:
            raise IndexError(f"CRTC register index {index} out of range (0-17)")
        return self._crtc.state.registers[index]

    def __len__(self) -> int:
        return 18


class Crtc:
    """MC6845 CRTC access.

    Provides read access to CRTC state including all 18 registers
    and internal timing counters.

    Usage:
        # Get full CRTC state
        state = bbc.crtc.state
        print(f"Screen start: {state.screen_start:04X}")

        # Access individual registers
        htotal = bbc.crtc.registers[0]
        print(f"Horizontal total: {htotal}")
    """

    def __init__(self, stub: debugger_pb2_grpc.DeviceInspectionStub):
        """Create a CRTC interface.

        Args:
            stub: The gRPC stub for the DeviceInspection service.
        """
        self._stub = stub
        self._registers = CrtcRegisters(self)

    @property
    def state(self) -> CrtcState:
        """Get the current CRTC state."""
        request = debugger_pb2.GetCrtcStateRequest()
        response = self._stub.GetCrtcState(request)

        return CrtcState(
            registers=tuple(response.registers),
            address_register=response.address_register,
            column=response.column,
            row=response.row,
            raster=response.raster,
            char_addr=response.char_addr,
            screen_start=response.screen_start,
            cursor_position=response.cursor_position,
            in_hsync=response.in_hsync,
            in_vsync=response.in_vsync,
            display_enabled=response.display_enabled,
            odd_field=response.odd_field,
            cycles_since_vsync=response.cycles_since_vsync,
        )

    @property
    def registers(self) -> CrtcRegisters:
        """Indexed access to CRTC registers (0-17)."""
        return self._registers
