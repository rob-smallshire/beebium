// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

import AudioToolbox
import XCTest
@testable import Beebium

/// Tests that audio playback is built on Apple's Default Output audio unit --
/// which has an output element only and never opens an input device, so it
/// cannot touch the microphone -- with a non-interleaved stereo float format.
/// This is what keeps a signed app from asking for the microphone when a
/// machine window's audio starts (AVAudioEngine realised its input node and
/// did exactly that on macOS 14).
final class AudioEngineOutputTests: XCTestCase {

    func testOutputUnitIsApplesDefaultOutputSoItNeverOpensAnInput() {
        let description = AudioEngine.outputComponentDescription
        XCTAssertEqual(description.componentType, kAudioUnitType_Output)
        XCTAssertEqual(description.componentSubType, kAudioUnitSubType_DefaultOutput,
                       "the Default Output unit has no input element, so it cannot touch the mic")
        XCTAssertEqual(description.componentManufacturer, kAudioUnitManufacturer_Apple)
    }

    func testStreamFormatIsNonInterleavedStereoFloatAtTheSampleRate() {
        let format = AudioEngine.streamFormat(sampleRate: 48_000)
        XCTAssertEqual(format.mSampleRate, 48_000)
        XCTAssertEqual(format.mFormatID, kAudioFormatLinearPCM)
        XCTAssertEqual(format.mChannelsPerFrame, 2)
        XCTAssertEqual(format.mBitsPerChannel, 32)
        XCTAssertEqual(format.mFramesPerPacket, 1)
        // Non-interleaved: each channel is its own buffer, so a frame in one
        // buffer is a single 4-byte float.
        XCTAssertEqual(format.mBytesPerFrame, 4)
        XCTAssertEqual(format.mBytesPerPacket, 4)
        XCTAssertNotEqual(format.mFormatFlags & kAudioFormatFlagIsFloat, 0, "float samples")
        XCTAssertNotEqual(format.mFormatFlags & kAudioFormatFlagIsNonInterleaved, 0,
                          "non-interleaved, so the render callback fills one buffer per channel")
    }

    func testStreamFormatTracksTheSampleRate() {
        XCTAssertEqual(AudioEngine.streamFormat(sampleRate: 44_100).mSampleRate, 44_100)
    }
}
