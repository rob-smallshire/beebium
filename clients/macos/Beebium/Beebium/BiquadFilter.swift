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

import Foundation

/// IIR biquad filter for audio signal processing.
///
/// Implements a 2nd-order IIR filter using the standard biquad difference equation:
/// ```
/// y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
/// ```
///
/// Coefficients are pre-normalized by a0, so a0 is always 1.0.
///
/// Reference: Audio EQ Cookbook by Robert Bristow-Johnson
/// https://www.w3.org/2011/audio/audio-eq-cookbook.html
struct BiquadFilter {

    /// Q of a second-order Butterworth section: 1/sqrt(2), the maximally flat
    /// passband, 3 dB down at the cutoff with no resonant peak. In the Audio EQ
    /// Cookbook's parameterisation alpha = sin(w0) / (2 * Q).
    static let butterworthQ: Float = 1.0 / Float(2.0).squareRoot()

    // MARK: - Coefficients (normalized by a0)

    private var b0: Float
    private var b1: Float
    private var b2: Float
    private var a1: Float
    private var a2: Float

    // MARK: - State (z^-1 delays)

    private var x1: Float = 0
    private var x2: Float = 0
    private var y1: Float = 0
    private var y2: Float = 0

    // MARK: - Initialization

    /// Create a lowpass Butterworth filter.
    ///
    /// - Parameters:
    ///   - cutoffHz: Cutoff frequency in Hz
    ///   - sampleRate: Sample rate in Hz
    init(lowpassCutoffHz cutoffHz: Float, sampleRate: Float) {
        // Calculate angular frequency
        let omega = 2.0 * Float.pi * cutoffHz / sampleRate
        let sn = sin(omega)
        let cs = cos(omega)

        let alpha = sn / (2.0 * Self.butterworthQ)

        // Lowpass coefficients (Audio EQ Cookbook)
        let b0_unnorm = (1.0 - cs) / 2.0
        let b1_unnorm = 1.0 - cs
        let b2_unnorm = (1.0 - cs) / 2.0
        let a0 = 1.0 + alpha
        let a1_unnorm = -2.0 * cs
        let a2_unnorm = 1.0 - alpha

        // Normalize by a0
        self.b0 = b0_unnorm / a0
        self.b1 = b1_unnorm / a0
        self.b2 = b2_unnorm / a0
        self.a1 = a1_unnorm / a0
        self.a2 = a2_unnorm / a0
    }

    /// Create a highpass Butterworth filter.
    ///
    /// - Parameters:
    ///   - cutoffHz: Cutoff frequency in Hz
    ///   - sampleRate: Sample rate in Hz
    init(highpassCutoffHz cutoffHz: Float, sampleRate: Float) {
        let omega = 2.0 * Float.pi * cutoffHz / sampleRate
        let sn = sin(omega)
        let cs = cos(omega)
        let alpha = sn / (2.0 * Self.butterworthQ)

        // Highpass coefficients (Audio EQ Cookbook)
        let b0_unnorm = (1.0 + cs) / 2.0
        let b1_unnorm = -(1.0 + cs)
        let b2_unnorm = (1.0 + cs) / 2.0
        let a0 = 1.0 + alpha
        let a1_unnorm = -2.0 * cs
        let a2_unnorm = 1.0 - alpha

        self.b0 = b0_unnorm / a0
        self.b1 = b1_unnorm / a0
        self.b2 = b2_unnorm / a0
        self.a1 = a1_unnorm / a0
        self.a2 = a2_unnorm / a0
    }

    /// Create a filter with explicit coefficients.
    ///
    /// - Parameters:
    ///   - b0, b1, b2: Feedforward coefficients (numerator)
    ///   - a1, a2: Feedback coefficients (denominator, a0 assumed to be 1.0)
    init(b0: Float, b1: Float, b2: Float, a1: Float, a2: Float) {
        self.b0 = b0
        self.b1 = b1
        self.b2 = b2
        self.a1 = a1
        self.a2 = a2
    }

    // MARK: - Processing

    /// Process a single sample through the filter.
    ///
    /// - Parameter input: Input sample
    /// - Returns: Filtered output sample
    mutating func process(_ input: Float) -> Float {
        // Direct Form I implementation
        let output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2

        // Shift delay line
        x2 = x1
        x1 = input
        y2 = y1
        y1 = output

        return output
    }

    /// Process multiple samples in place.
    ///
    /// - Parameter samples: Buffer of samples to filter
    mutating func process(_ samples: inout [Float]) {
        for i in samples.indices {
            samples[i] = process(samples[i])
        }
    }

    /// Process samples from source to destination buffer.
    ///
    /// - Parameters:
    ///   - source: Input samples
    ///   - destination: Output buffer (must be same size as source)
    mutating func process(from source: UnsafePointer<Float>, to destination: UnsafeMutablePointer<Float>, count: Int) {
        for i in 0..<count {
            destination[i] = process(source[i])
        }
    }

    // MARK: - Configuration

    /// Reconfigure as a lowpass filter with new cutoff frequency.
    ///
    /// - Parameters:
    ///   - cutoffHz: New cutoff frequency in Hz
    ///   - sampleRate: Sample rate in Hz
    mutating func setLowpassCutoff(_ cutoffHz: Float, sampleRate: Float) {
        let omega = 2.0 * Float.pi * cutoffHz / sampleRate
        let sn = sin(omega)
        let cs = cos(omega)
        let alpha = sn / (2.0 * Self.butterworthQ)

        let b0_unnorm = (1.0 - cs) / 2.0
        let b1_unnorm = 1.0 - cs
        let b2_unnorm = (1.0 - cs) / 2.0
        let a0 = 1.0 + alpha
        let a1_unnorm = -2.0 * cs
        let a2_unnorm = 1.0 - alpha

        b0 = b0_unnorm / a0
        b1 = b1_unnorm / a0
        b2 = b2_unnorm / a0
        a1 = a1_unnorm / a0
        a2 = a2_unnorm / a0
    }

    /// Reconfigure as a highpass filter with new cutoff frequency.
    ///
    /// - Parameters:
    ///   - cutoffHz: New cutoff frequency in Hz
    ///   - sampleRate: Sample rate in Hz
    mutating func setHighpassCutoff(_ cutoffHz: Float, sampleRate: Float) {
        let omega = 2.0 * Float.pi * cutoffHz / sampleRate
        let sn = sin(omega)
        let cs = cos(omega)
        let alpha = sn / (2.0 * Self.butterworthQ)

        let b0_unnorm = (1.0 + cs) / 2.0
        let b1_unnorm = -(1.0 + cs)
        let b2_unnorm = (1.0 + cs) / 2.0
        let a0 = 1.0 + alpha
        let a1_unnorm = -2.0 * cs
        let a2_unnorm = 1.0 - alpha

        b0 = b0_unnorm / a0
        b1 = b1_unnorm / a0
        b2 = b2_unnorm / a0
        a1 = a1_unnorm / a0
        a2 = a2_unnorm / a0
    }

    /// Reset filter state (clear delay lines).
    ///
    /// Call this when there's a discontinuity in the input signal
    /// (e.g., when starting playback or after a buffer underrun).
    mutating func reset() {
        x1 = 0
        x2 = 0
        y1 = 0
        y2 = 0
    }
}

// MARK: - Filter Bank

/// A bank of biquad filters for multi-channel processing.
///
/// Provides convenient per-channel filtering with identical coefficients
/// but independent state for each channel.
struct BiquadFilterBank {

    private var filters: [BiquadFilter]

    /// Number of channels in the filter bank
    var channelCount: Int { filters.count }

    /// Create a filter bank with lowpass filters.
    ///
    /// - Parameters:
    ///   - channelCount: Number of independent filter channels
    ///   - cutoffHz: Cutoff frequency for all filters
    ///   - sampleRate: Sample rate in Hz
    init(channelCount: Int, lowpassCutoffHz cutoffHz: Float, sampleRate: Float) {
        filters = (0..<channelCount).map { _ in
            BiquadFilter(lowpassCutoffHz: cutoffHz, sampleRate: sampleRate)
        }
    }

    /// Create a filter bank with highpass filters.
    ///
    /// - Parameters:
    ///   - channelCount: Number of independent filter channels
    ///   - cutoffHz: Cutoff frequency for all filters
    ///   - sampleRate: Sample rate in Hz
    init(channelCount: Int, highpassCutoffHz cutoffHz: Float, sampleRate: Float) {
        filters = (0..<channelCount).map { _ in
            BiquadFilter(highpassCutoffHz: cutoffHz, sampleRate: sampleRate)
        }
    }

    /// Process a single sample for a specific channel.
    ///
    /// - Parameters:
    ///   - input: Input sample
    ///   - channel: Channel index
    /// - Returns: Filtered output sample
    mutating func process(_ input: Float, channel: Int) -> Float {
        guard channel >= 0 && channel < filters.count else { return input }
        return filters[channel].process(input)
    }

    /// Reconfigure all filters with new lowpass cutoff.
    mutating func setLowpassCutoff(_ cutoffHz: Float, sampleRate: Float) {
        for i in filters.indices {
            filters[i].setLowpassCutoff(cutoffHz, sampleRate: sampleRate)
        }
    }

    /// Reconfigure all filters with new highpass cutoff.
    mutating func setHighpassCutoff(_ cutoffHz: Float, sampleRate: Float) {
        for i in filters.indices {
            filters[i].setHighpassCutoff(cutoffHz, sampleRate: sampleRate)
        }
    }

    /// Reset all filter states.
    mutating func reset() {
        for i in filters.indices {
            filters[i].reset()
        }
    }
}
