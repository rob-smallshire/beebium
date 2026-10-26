// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#include <beebium/AudioFrameAssembler.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beebium;

TEST_CASE("AudioFrameAssembler places the SN76489 fields and silences the rest",
          "[audio][frame-assembler]") {
    AudioBuffer buffer(16);
    AudioFrameAssembler frames(buffer);

    frames.push_frame(AudioSample::field_2x16bit(1, 2), AudioSample::field_2x16bit(-3, 4));

    AudioSample sample;
    REQUIRE(buffer.read(&sample, 1) == 1);
    REQUIRE(sample.sources[0] == AudioSample::field_2x16bit(1, 2));
    REQUIRE(sample.sources[1] == AudioSample::field_2x16bit(-3, 4));
    REQUIRE(sample.sources[2] == 0);
    REQUIRE(sample.sources[3] == 0);
}

TEST_CASE("AudioFrameAssembler drops frames while detached", "[audio][frame-assembler]") {
    AudioBuffer buffer(16);
    AudioFrameAssembler frames;
    frames.push_frame(1, 2);
    REQUIRE(buffer.available() == 0);

    frames.attach(&buffer);
    frames.push_frame(1, 2);
    REQUIRE(buffer.available() == 1);

    frames.attach(nullptr);
    frames.push_frame(1, 2);
    REQUIRE(buffer.available() == 1);
}

TEST_CASE("field_2x16bit puts the first channel in the low half", "[audio][frame-assembler]") {
    REQUIRE(AudioSample::field_2x16bit(0x1234, 0x5678) == 0x56781234u);
    REQUIRE(AudioSample::field_2x16bit(-1, 0) == 0x0000FFFFu);
    AudioSample sample;
    sample.pack_2x16bit(2, -2, 3);
    REQUIRE(sample.sources[2] == AudioSample::field_2x16bit(-2, 3));
}
