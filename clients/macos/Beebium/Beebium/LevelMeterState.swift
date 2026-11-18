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

/// The live input level meter's state, in decibels of full scale (cassette
/// round 12). A linear bar reads "a few pixels" for a healthy signal 20 dB down
/// and shoots off scale on a single-sample click, so the bar is in decibels, a
/// peak-hold line gives the user a figure to set the source by, and the clip
/// mark lights only on sustained clipping.
///
/// Pure and time-driven: the owner (`CassetteClient`) feeds it the peak and clip
/// run accumulated since the last publish, with the time, and reads back the bar
/// and hold positions (fractions of the bar from the floor to full scale) and
/// whether the clip mark is lit. No clock, view or audio engine here, so the
/// decay, hold and conversion are tested without hardware.
struct LevelMeterState: Equatable {
    /// The bottom of the scale: a signal at or below this draws nothing.
    static let floorDB: Float = -60
    /// The target the tick marks: the band a cassette signal should sit in,
    /// clear of clipping and above the front end's threshold.
    static let targetDB: Float = -12
    /// The bar falls at this rate once the signal drops (instant attack).
    static let barFallDBPerSecond: Float = 20
    /// The peak-hold line sits at the highest level seen for this long after it
    /// was set, before it begins to fall.
    static let holdSeconds: TimeInterval = 2
    /// The peak-hold line falls at this rate once its dwell has expired, until
    /// the bar overtakes it.
    static let holdFallDBPerSecond: Float = 10
    /// The clip mark lights only when a chunk has at least this many consecutive
    /// full-scale samples in the taken channel.
    static let clipRunSamples = 3
    /// The clip mark stays lit this long after the last clipping chunk, then is
    /// absent (there is no grey or dim state).
    static let clipHoldSeconds: TimeInterval = 2

    /// The tick's position, as a fraction of the bar from the floor (0) to full
    /// scale (1). A constant the drawing reads.
    static var targetFraction: Float { fraction(ofDB: targetDB) }

    /// The bar's position (0 at the floor, 1 at full scale).
    private(set) var barFraction: Float = 0
    /// The peak-hold line's position (0 at the floor, 1 at full scale).
    private(set) var holdFraction: Float = 0
    /// Whether the clip mark is lit.
    private(set) var clipLit: Bool = false

    /// The bar and hold levels in decibels; negative infinity is silence (below
    /// the floor).
    private var barDB: Float = -.infinity
    private var holdDB: Float = -.infinity
    /// When the hold line was last raised to a new peak, so its dwell can expire.
    private var holdSetAt: TimeInterval = 0
    /// The last update's time, for the decay interval; nil before the first.
    private var lastUpdate: TimeInterval?
    /// When a held clip mark lapses, or nil when none is held.
    private var clipHeldUntil: TimeInterval?

    /// The decibels of full scale for a linear level (0 silent .. 1 full scale);
    /// negative infinity for silence.
    static func decibels(ofLevel level: Float) -> Float {
        level <= 0 ? -.infinity : 20 * log10(level)
    }

    /// The fraction of the bar (0 at the floor, 1 at full scale) for a decibel
    /// value. At or below the floor (and silence) draws nothing; at or above full
    /// scale fills the bar.
    static func fraction(ofDB db: Float) -> Float {
        if db <= floorDB { return 0 }
        if db >= 0 { return 1 }
        return (db - floorDB) / -floorDB
    }

    /// Take the peak level and clip run accumulated since the last publish, at a
    /// time. The bar rises at once to a louder signal and falls at the bar rate;
    /// the hold line jumps to a new peak and arms its dwell, then falls at the
    /// hold rate once the dwell expires, never below the bar; the clip mark lights
    /// for a sustained run and holds briefly.
    mutating func update(level: Float, clipRun: Int, at time: TimeInterval) {
        let incomingDB = Self.decibels(ofLevel: level)
        if let last = lastUpdate {
            let dt = Float(max(0, time - last))
            barDB = max(incomingDB, barDB - Self.barFallDBPerSecond * dt)
            if incomingDB >= holdDB {
                holdDB = incomingDB
                holdSetAt = time
            } else if time - holdSetAt >= Self.holdSeconds {
                holdDB -= Self.holdFallDBPerSecond * dt
            }
            // A louder bar overtakes a falling hold line.
            holdDB = max(holdDB, barDB)
        } else {
            barDB = incomingDB
            holdDB = incomingDB
            holdSetAt = time
        }
        if clipRun >= Self.clipRunSamples {
            clipHeldUntil = time + Self.clipHoldSeconds
        }
        clipLit = (clipHeldUntil ?? -.infinity) > time
        lastUpdate = time
        barFraction = Self.fraction(ofDB: barDB)
        holdFraction = Self.fraction(ofDB: holdDB)
    }

    /// Return to silence for a fresh stream.
    mutating func reset() {
        self = LevelMeterState()
    }
}
