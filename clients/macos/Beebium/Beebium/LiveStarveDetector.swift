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

import Foundation

/// Which way the live input is drifting from the Beeb, each with its own
/// remedy for whoever reads the message (section 9.0). An underrun means the
/// host is not feeding the server fast enough and silence is being made up; an
/// overrun means the host is feeding faster than the Beeb consumes and samples
/// are being dropped.
enum LiveStarveCondition: Equatable {
    case underrun
    case overrun

    /// The short, honest inline marker label (section 3.4): the full sentence
    /// is the popover's, not the marker's.
    var marker: String {
        switch self {
        case .underrun: return "Live input not keeping up"
        case .overrun: return "Live input arriving too fast"
        }
    }

    /// The client-authored sentence (section 3.4: a capital and a full stop).
    /// There is no server fragment to incorporate -- the server reports counts,
    /// not a refusal -- so the client words the whole thing.
    var sentence: String {
        switch self {
        case .underrun:
            return "The live input is not keeping up with the Beeb; "
                + "some of the tape is being made up as silence."
        case .overrun:
            return "The live input is arriving faster than the Beeb can take it; "
                + "some of the tape is being dropped."
        }
    }
}

/// Decides when a live input's buffering has gone wrong enough to tell the user
/// (section 9.0). The server reports cumulative overrun and underrun counts in
/// the deck status; this is fed one `(overruns, underruns)` pair per whole
/// second of a live stream and raises a condition only once the count has grown
/// in each of `LIVE_STARVE_SECONDS` consecutive seconds, so a single late chunk
/// while the ring primes never raises anything.
///
/// A condition that has been raised -- whether it is still showing or the user
/// has dismissed it -- does not raise again until its count has stopped growing
/// for `LIVE_STARVE_SECONDS` consecutive seconds and then grows afresh. The two
/// conditions are tracked independently, so the other may raise (and replace the
/// showing message) at any time. `reset()` starts a new stream clean.
///
/// Pure: all the timing is in the sequence of observations, so the whole state
/// machine is tested without a clock, a view or a server.
struct LiveStarveDetector {
    /// The number of consecutive growing seconds that raises a condition, and
    /// the number of non-growing seconds that re-arms a raised one. Named here
    /// (the specification's `LIVE_STARVE_SECONDS`), so the constant is tested.
    static let starveSeconds = 3

    /// The growth run, clear run and raised-and-not-yet-re-armed flag for one
    /// condition.
    private struct Track {
        var growth = 0
        var clear = 0
        var armed = true

        /// Step one second. Returns true when this second raises the condition.
        mutating func step(grew: Bool) -> Bool {
            if grew {
                growth += 1
                clear = 0
            } else {
                clear += 1
                growth = 0
                if clear >= LiveStarveDetector.starveSeconds { armed = true }
            }
            guard armed, growth >= LiveStarveDetector.starveSeconds else { return false }
            // Raise once; re-arm only after the count has stopped growing for a
            // full run, so a persistent drift does not re-raise every few
            // seconds.
            armed = false
            growth = 0
            return true
        }
    }

    private var over = Track()
    private var under = Track()
    private var hasBaseline = false
    private var lastOverruns: UInt64 = 0
    private var lastUnderruns: UInt64 = 0

    /// Observe the cumulative counts at one whole second of a live stream.
    /// Returns the condition newly raised this second, or nil. The first
    /// observation of a stream only establishes the baseline (there is nothing
    /// yet to compare against), so it never raises.
    ///
    /// Both conditions can grow in the same second; the underrun wins, since
    /// made-up silence is the audible corruption and the user sees the latest
    /// kind (section 9.0's "the other condition replaces the showing one").
    mutating func observe(overruns: UInt64, underruns: UInt64) -> LiveStarveCondition? {
        defer {
            lastOverruns = overruns
            lastUnderruns = underruns
        }
        guard hasBaseline else {
            hasBaseline = true
            return nil
        }
        let underRaised = under.step(grew: underruns > lastUnderruns)
        let overRaised = over.step(grew: overruns > lastOverruns)
        if underRaised { return .underrun }
        if overRaised { return .overrun }
        return nil
    }

    /// Start a new stream clean: no baseline, no runs, both conditions armed.
    mutating func reset() {
        self = LiveStarveDetector()
    }
}

/// The copyable detail text a raised starve message carries (section 9.0): the
/// sentence, then the counts at the moment of raising -- not updated live.
/// Pure, so the figures are tested without a view.
enum LiveStarveDetail {
    /// The full popover text: the condition's sentence, a blank line, then the
    /// counts since the stream began.
    static func text(condition: LiveStarveCondition,
                     overruns: UInt64, underruns: UInt64, sampleRate: UInt32) -> String {
        condition.sentence + "\n\n"
            + counts(condition: condition, overruns: overruns,
                     underruns: underruns, sampleRate: sampleRate)
    }

    /// Just the counts sentence: dropped samples for an overrun, or seconds of
    /// made-up silence (from the underrun sample count and the stream's rate)
    /// for an underrun.
    static func counts(condition: LiveStarveCondition,
                       overruns: UInt64, underruns: UInt64, sampleRate: UInt32) -> String {
        switch condition {
        case .overrun:
            return "Dropped \(grouped(overruns)) samples since the stream began."
        case .underrun:
            let seconds = Double(underruns) / Double(max(sampleRate, 1))
            return "Made up \(String(format: "%.1f", seconds)) s of silence since the stream began."
        }
    }

    /// A non-negative integer with thousands separated by commas, independent of
    /// locale so the figure reads the same everywhere and is testable.
    static func grouped(_ value: UInt64) -> String {
        let digits = Array(String(value))
        var out = ""
        for (index, character) in digits.enumerated() {
            if index > 0 && (digits.count - index) % 3 == 0 { out.append(",") }
            out.append(character)
        }
        return out
    }
}
