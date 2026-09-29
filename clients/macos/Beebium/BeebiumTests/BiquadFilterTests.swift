// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version. Beebium is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details. You should have received a copy of the GNU General Public License along with
// Beebium. If not, see <https://www.gnu.org/licenses/>.

import XCTest
@testable import Beebium

/// The audio biquads are second-order Butterworth sections: 3 dB down at the
/// cutoff, and never above 0 dB (no resonant peak) anywhere in the band.
final class BiquadFilterTests: XCTestCase {

    private let sampleRate: Float = 48000

    /// A representative low-pass cutoff for exercising the general BiquadFilter
    /// low-pass mode. The audio chain no longer uses a client low-pass (the
    /// server band-limits before decimation), but BiquadFilter still offers one
    /// as a reusable filter, and its Butterworth response is checked here.
    private let lowpassCutoffHz: Float = 8000

    /// Gain in dB of `filter` for a sine at `frequencyHz`, measured after the
    /// filter settles. The window is one second, so an integer frequency
    /// completes a whole number of cycles in it and the RMS of the sampled sine
    /// is exactly its amplitude over sqrt(2), whatever the sample phase.
    private func gainDB(_ filter: BiquadFilter, atHz frequencyHz: Int) -> Float {
        var filter = filter
        let window = Int(sampleRate)
        let settle = window
        var sumSquares: Double = 0
        for n in 0..<(settle + window) {
            let phase = 2.0 * Double.pi * Double(frequencyHz) * Double(n) / Double(sampleRate)
            let output = filter.process(Float(sin(phase)))
            if n >= settle {
                sumSquares += Double(output) * Double(output)
            }
        }
        let amplitude = (2.0 * sumSquares / Double(window)).squareRoot()
        return Float(20.0 * log10(amplitude))
    }

    /// Frequencies across the audio band, logarithmically spaced.
    private let bandHz = [20, 30, 50, 80, 125, 200, 315, 500, 800, 1250, 2000, 3150,
                          5000, 6300, 7000, 7500, 8000, 8500, 10000, 12500, 16000, 20000]

    func testLowpassIsThreeDecibelsDownAtItsCutoff() {
        let filter = BiquadFilter(lowpassCutoffHz: lowpassCutoffHz, sampleRate: sampleRate)
        let gain = gainDB(filter, atHz: Int(lowpassCutoffHz))
        XCTAssertEqual(gain, -3.01, accuracy: 0.1)
    }

    func testLowpassNeverGainsAnywhereInTheBand() {
        let filter = BiquadFilter(lowpassCutoffHz: lowpassCutoffHz, sampleRate: sampleRate)
        for frequency in bandHz {
            XCTAssertLessThanOrEqual(gainDB(filter, atHz: frequency), 0.01, "low-pass gains at \(frequency) Hz")
        }
    }

    func testHighpassIsThreeDecibelsDownAtItsCutoff() {
        let filter = BiquadFilter(highpassCutoffHz: AudioRenderer.highpassCutoffHz, sampleRate: sampleRate)
        let gain = gainDB(filter, atHz: Int(AudioRenderer.highpassCutoffHz))
        XCTAssertEqual(gain, -3.01, accuracy: 0.1)
    }

    func testHighpassNeverGainsAnywhereInTheBand() {
        let filter = BiquadFilter(highpassCutoffHz: AudioRenderer.highpassCutoffHz, sampleRate: sampleRate)
        for frequency in bandHz {
            XCTAssertLessThanOrEqual(gainDB(filter, atHz: frequency), 0.01, "high-pass gains at \(frequency) Hz")
        }
    }

    // Reconfiguring an existing filter gives the same response as building one.
    func testReconfiguredFiltersMatchFreshlyBuiltOnes() {
        var lowpass = BiquadFilter(highpassCutoffHz: 100, sampleRate: sampleRate)
        lowpass.setLowpassCutoff(lowpassCutoffHz, sampleRate: sampleRate)
        XCTAssertEqual(gainDB(lowpass, atHz: 8000), -3.01, accuracy: 0.1)

        var highpass = BiquadFilter(lowpassCutoffHz: 100, sampleRate: sampleRate)
        highpass.setHighpassCutoff(AudioRenderer.highpassCutoffHz, sampleRate: sampleRate)
        XCTAssertEqual(gainDB(highpass, atHz: Int(AudioRenderer.highpassCutoffHz)), -3.01, accuracy: 0.1)
    }
}
