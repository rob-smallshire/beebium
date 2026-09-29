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

"""The client-chain model reproduces the numbers the macOS mix gain rests on (#90, #120).

    uv run pytest test_client_chain.py
"""

from __future__ import annotations

import math

import pytest

from client_chain import BUTTERWORTH_Q, KNEE, MIX_GAIN, channel_peak, in_phase_mix

#: The Q the biquads had before #120: alpha = sin(w0) / (2 * sqrt 2).
RESONANT_Q = math.sqrt(2.0)


def test_the_filters_worst_case_is_a_125hz_tone() -> None:
    peaks = {f: channel_peak(f) for f in (125, 250, 1000, 6000)}
    assert max(peaks, key=peaks.__getitem__) == 125
    assert peaks[125] == pytest.approx(1.471, abs=0.002)


def test_the_mix_gain_is_quarter_scale_over_the_worst_case_peak() -> None:
    assert 0.25 / channel_peak(125) == pytest.approx(MIX_GAIN, abs=0.001)


def test_one_channel_at_1khz_matches_the_swift_measurement() -> None:
    # AudioRendererTests.testOneFullScaleChannelAt1kHzPeaksAtItsMeasuredLevel
    one_side, _ = in_phase_mix(1000, 1, MIX_GAIN)
    assert one_side == pytest.approx(0.1397, abs=0.0005)


@pytest.mark.parametrize("frequency_hz", [125, 1000, 6000])
def test_four_full_volume_channels_in_phase_stay_under_the_knee(frequency_hz: int) -> None:
    side, beyond = in_phase_mix(frequency_hz, 4, MIX_GAIN)
    assert side <= 0.72
    assert side < KNEE
    assert beyond == 0.0


def test_the_resonant_filters_before_120_overshot_a_square_by_64_percent() -> None:
    assert channel_peak(1000, RESONANT_Q) == pytest.approx(1.644, abs=0.002)
    # ...so at the quarter-scale gain first proposed, four channels hit the limiter.
    side, beyond = in_phase_mix(1000, 4, 0.25, RESONANT_Q)
    assert side == pytest.approx(1.1625, abs=0.001)
    assert beyond > 0.0


def test_the_butterworth_q_matches_the_swift_constant() -> None:
    assert BUTTERWORTH_Q == pytest.approx(1.0 / math.sqrt(2.0))
