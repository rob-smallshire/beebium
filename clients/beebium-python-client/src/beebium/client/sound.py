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

"""SN76489 Sound Generator access for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client._proto import debugger_pb2, debugger_pb2_grpc
from beebium.client._repr import facade_repr

# MOS SOUND command channel mapping to SN76489 internal channels
# MOS channel 0 = Noise (SN76489 index 3)
# MOS channel 1 = Tone0 (SN76489 index 0)
# MOS channel 2 = Tone1 (SN76489 index 1)
# MOS channel 3 = Tone2 (SN76489 index 2)
MOS_TO_SN76489 = {0: 3, 1: 0, 2: 1, 3: 2}
SN76489_TO_MOS = {v: k for k, v in MOS_TO_SN76489.items()}


@dataclass
class SoundChannelState:
    """State of a single sound channel (tone or noise)."""

    channel_id: int  # SN76489 channel index (0-2 = Tone0-2, 3 = Noise)
    channel_name: str  # "Tone0", "Tone1", "Tone2", or "Noise"

    # Tone channel state (for tone channels)
    frequency_divider: int  # 10-bit divider value (0-1023)
    counter: int  # Current countdown value
    output_bit: bool  # Square wave polarity
    volume: int  # 4-bit attenuation (0=max, 15=silent)
    frequency_hz: float  # Computed output frequency

    # Noise channel state (only valid for noise channel)
    noise_rate: int  # 2-bit rate select (0-3, where 3=Tone2)
    white_noise: bool  # True=white noise, False=periodic
    lfsr_state: int  # 15-bit LFSR state

    @property
    def is_noise(self) -> bool:
        """True if this is the noise channel."""
        return self.channel_id == 3

    @property
    def is_silent(self) -> bool:
        """True if volume is at maximum attenuation (silent)."""
        return self.volume == 15

    @property
    def mos_channel(self) -> int:
        """MOS SOUND command channel number (0-3)."""
        return SN76489_TO_MOS[self.channel_id]

    def __str__(self) -> str:
        """Format channel state for display."""
        if self.is_noise:
            mode = "White" if self.white_noise else "Periodic"
            return (
                f"{self.channel_name}: Vol={self.volume} Rate={self.noise_rate} Mode={mode} LFSR={self.lfsr_state:04X}"
            )
        else:
            return (
                f"{self.channel_name}: Vol={self.volume} Freq={self.frequency_divider} "
                f"({self.frequency_hz:.1f} Hz) Out={int(self.output_bit)}"
            )


@dataclass
class SoundGeneratorState:
    """SN76489 sound generator state snapshot."""

    channels: tuple[SoundChannelState, ...]  # 4 channels: Tone0, Tone1, Tone2, Noise
    latched_register: int  # Currently latched register (0-7)

    def __str__(self) -> str:
        """Format sound generator state for display."""
        lines = [f"Latched register: {self.latched_register}"]
        for ch in self.channels:
            lines.append(str(ch))
        return "\n".join(lines)

    def tone(self, index: int) -> SoundChannelState:
        """Get tone channel by SN76489 index (0-2)."""
        if not 0 <= index <= 2:
            raise KeyError(f"Tone channel index must be 0-2, got {index}")
        return self.channels[index]

    @property
    def noise(self) -> SoundChannelState:
        """Get the noise channel."""
        return self.channels[3]

    def mos_channel(self, mos_num: int) -> SoundChannelState:
        """Get channel by MOS SOUND command number (0-3).

        MOS mapping:
            0 = Noise
            1 = Tone0
            2 = Tone1
            3 = Tone2
        """
        if not 0 <= mos_num <= 3:
            raise KeyError(f"MOS channel number must be 0-3, got {mos_num}")
        return self.channels[MOS_TO_SN76489[mos_num]]


class Sound:
    """SN76489 Sound Generator access.

    Provides read access to sound generator state, which is write-only on hardware.
    Returns chip-native channel ordering (Tone0, Tone1, Tone2, Noise).

    Usage:
        # Get sound generator state
        state = bbc.sound.state()
        print(f"Tone0 frequency: {state.tone(0).frequency_hz:.1f} Hz")

        # Access by MOS SOUND command channel number
        print(f"MOS channel 1 volume: {state.mos_channel(1).volume}")

        # Check noise channel
        if state.noise.white_noise:
            print("White noise mode")
    """

    def __init__(self, stub: debugger_pb2_grpc.DeviceInspectionStub):
        """Create a Sound interface.

        Args:
            stub: The gRPC stub for the DeviceInspection service.
        """
        self._stub = stub

    def __repr__(self) -> str:
        return facade_repr(self, "the SN76489 sound generator")

    def state(self) -> SoundGeneratorState:
        """Get the current sound generator state.

        Returns:
            SoundGeneratorState with all channel states and latched register.
            Channels are in SN76489 native order: Tone0, Tone1, Tone2, Noise.
        """
        request = debugger_pb2.GetSoundGeneratorStateRequest()
        response = self._stub.GetSoundGeneratorState(request)

        channels = tuple(
            SoundChannelState(
                channel_id=ch.channel_id,
                channel_name=ch.channel_name,
                frequency_divider=ch.frequency_divider,
                counter=ch.counter,
                output_bit=ch.output_bit,
                volume=ch.volume,
                frequency_hz=ch.frequency_hz,
                noise_rate=ch.noise_rate,
                white_noise=ch.white_noise,
                lfsr_state=ch.lfsr_state,
            )
            for ch in response.channels
        )

        return SoundGeneratorState(
            channels=channels,
            latched_register=response.latched_register,
        )
