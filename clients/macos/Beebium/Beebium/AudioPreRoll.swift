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

/// Adaptive pre-roll for the audio playback ring buffer.
///
/// The ring buffer is the jitter buffer between the gRPC receive thread (the
/// producer) and the CoreAudio render thread (the consumer). Its steady-state
/// fill is the keypress-to-sound latency, so it wants to be small; but if the
/// host starves either thread for longer than that fill, the buffer runs dry and
/// the output stutters.
///
/// #126: an 85 ms fixed buffer stuttered on Jet Set Willy's title music whenever
/// a scheduling stall ran longer than 85 ms under a loaded host, while the server
/// audio stream itself carried zero dropped samples even at loadavg 217. So the
/// fault was purely in the client's buffering, and this is the fix.
///
/// Policy. The buffer has room for 500 ms so the space exists, but playback holds
/// only `targetFill` buffered -- that is the latency -- starting at 120 ms. On an
/// underrun the target rises by 60 ms, up to 480 ms, and is HELD for the rest of
/// the session (no decay, so it cannot oscillate); `reset()` (a new connection)
/// returns it to 120 ms. After an underrun, playback re-primes: it stays silent
/// until the buffer refills to the new target, so there is one audible gap and
/// then stable output, rather than a burst of repeated short glitches. A host
/// that never stalls pays only the 120 ms floor.
///
/// Render-thread state: created and used only from the audio render thread.
final class AudioPreRoll {

    static let capacityMilliseconds = 500
    static let initialTargetMilliseconds = 120
    static let targetStepMilliseconds = 60
    static let maxTargetMilliseconds = 480

    static func frames(milliseconds ms: Int, sampleRate: Int) -> Int {
        ms * sampleRate / 1000
    }

    /// Ring-buffer capacity for a sample rate: the 500 ms envelope the policy
    /// needs, so a target that has climbed to 480 ms still fits with headroom.
    static func capacityFrames(sampleRate: Int) -> Int {
        frames(milliseconds: capacityMilliseconds, sampleRate: sampleRate)
    }

    private let sampleRate: Int
    private let initialTarget: Int
    private let step: Int
    private let maxTarget: Int

    /// Current steady-state fill target in frames -- the playback latency.
    private(set) var targetFill: Int
    /// True while re-buffering: the caller emits an all-silent callback and
    /// consumes nothing, letting the buffer refill to `targetFill`.
    private(set) var isPriming: Bool = true

    init(sampleRate: Int) {
        self.sampleRate = sampleRate
        initialTarget = Self.frames(milliseconds: Self.initialTargetMilliseconds, sampleRate: sampleRate)
        step = Self.frames(milliseconds: Self.targetStepMilliseconds, sampleRate: sampleRate)
        maxTarget = Self.frames(milliseconds: Self.maxTargetMilliseconds, sampleRate: sampleRate)
        targetFill = initialTarget
    }

    /// The current playback latency in milliseconds (for display/diagnostics).
    var targetFillMilliseconds: Int { targetFill * 1000 / sampleRate }

    /// How many frames to pull from the ring buffer this callback, given how many
    /// are `available` and how many the callback `requested`. Returns 0 while
    /// priming -- the caller then emits a silent callback and consumes nothing.
    /// When it returns `requested`, the caller reads that many and, if the read
    /// comes back short, must call `noteUnderrun()`.
    func framesToPull(available: Int, requested: Int) -> Int {
        if isPriming {
            guard available >= targetFill else { return 0 }
            isPriming = false
        }
        return requested
    }

    /// Record that a playing read came back short: raise the target (capped at
    /// 480 ms) and re-prime, so playback resumes only once the buffer has refilled
    /// to the higher target.
    func noteUnderrun() {
        targetFill = min(maxTarget, targetFill + step)
        isPriming = true
    }

    /// Return to the initial target and the priming state -- a new connection.
    func reset() {
        targetFill = initialTarget
        isPriming = true
    }
}
