// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

/// Drives the Processor panel's emulation-speed control.
///
/// Owns the speed state shown by the slider and the achieved-speed marker, and
/// polls the server's pacing stats while the panel is visible. The numeric scale
/// is a symmetric log2 axis with 1x at the centre: the upper bound is the
/// session's running-max attainable speed rounded up to a power of two, and the
/// lower bound is its reciprocal. The running max only grows, so the scale never
/// jars by shrinking, but it expands when the host frees up and a higher speed
/// becomes attainable.
@MainActor
final class SpeedControlModel: ObservableObject {
    /// Configured multiplier the slider reflects (1.0 = real-time). The source
    /// of truth for what is sent when not running unlimited.
    @Published var configuredMultiplier: Double = 1.0

    /// Most recently reported achieved multiplier (the read-only marker). ~1x
    /// under normal real-time pacing.
    @Published private(set) var achievedMultiplier: Double = 1.0

    /// Whether the emulator is running unlimited (the 0.0 wire sentinel).
    @Published private(set) var isUnlimited: Bool = false

    /// Running max of the server's estimated max-attainable speed over the
    /// session. Seeds the scale; only ever grows.
    @Published private(set) var sessionMaxAttainable: Double = 1.0

    /// Ceiling on the axis coverage in log2 units: 2^9 = 512x, past any real
    /// host, so a spurious estimate (see poll) cannot overspread the axis.
    private static let maxCoverageExponent = 9

    /// The axis shape: how many labels (seven or nine) and their log2 spacing k.
    /// Labels sit at 2^(k*i) for i in -halfSpan...halfSpan, so seven labels span
    /// [b^-3, b^3] and nine span [b^-4, b^4] for base b = 2^k. Both families are
    /// candidates; the axis takes whichever covers the session max with the
    /// tightest coverage, so it grows in small steps -- 8, 16, 64, 256, 512 --
    /// rather than the 8, 64, 512 that the seven-label family alone would jump
    /// through. A tie prefers nine (finer, smaller base). Floored at base 2.
    private var scaleShape: (halfSpan: Int, step: Int) {
        let l = log2(max(sessionMaxAttainable, 1.0))
        let k7 = max(1, Int(ceil(l / 3.0)))  // seven labels: 2^(3*k7) >= max
        let k9 = max(1, Int(ceil(l / 4.0)))  // nine labels:  2^(4*k9) >= max
        return (4 * k9 <= 3 * k7) ? (4, k9) : (3, k7)
    }

    /// Tick spacing in log2 units, k (base b = 2^k).
    var tickStep: Int { scaleShape.step }

    /// Scale half-width in log2 units: halfSpan tick steps each side of 1x.
    var scaleExponent: Int { scaleShape.halfSpan * scaleShape.step }

    var scaleMax: Double { pow(2.0, Double(scaleExponent)) }
    var scaleMin: Double { 1.0 / scaleMax }

    private weak var systemClient: SystemClient?
    private var pollTask: Task<Void, Never>?
    private var hasInitialised = false
    // Poll a little faster than the server publishes (~500ms) so each new
    // sample is picked up promptly; the marker is then animated to it. No
    // client-side smoothing: the server window already averages, and stacking
    // an EMA on top over-smooths into a laggy "wave".
    private static let pollInterval = Duration.milliseconds(250)

    /// Bind to the system client used for the RPCs. Safe to call before connect;
    /// polls simply return nothing until the client is connected.
    func bind(to systemClient: SystemClient) {
        self.systemClient = systemClient
    }

    /// Begin polling pacing stats (call when the panel appears).
    func startPolling() {
        guard pollTask == nil else { return }
        pollTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.poll()
                try? await Task.sleep(for: SpeedControlModel.pollInterval)
            }
        }
    }

    /// Stop polling (call when the panel disappears).
    func stopPolling() {
        pollTask?.cancel()
        pollTask = nil
    }

    private func poll() async {
        guard let stats = await systemClient?.getPacingStats() else { return }

        // Show the server's sample directly; the view animates the marker to it.
        // Only assign on change so an unchanged poll doesn't restart the glide.
        if stats.achievedSpeedMultiplier != achievedMultiplier {
            achievedMultiplier = stats.achievedSpeedMultiplier
        }

        // Grow the session max from the server's estimate (and the achieved rate
        // as a floor). estimated is 0 until the first window completes. The
        // estimate is achieved / active-fraction, which spikes arbitrarily high
        // when the emulator is briefly near-idle (a boot, a reset); since the
        // session max never shrinks, one such spike would permanently blow up
        // the scale. Ignore non-finite values and clamp growth to the axis
        // ceiling so the scale stays bounded and meaningful.
        let candidate = max(stats.estimatedMaxSpeedMultiplier, stats.achievedSpeedMultiplier)
        let ceiling = pow(2.0, Double(Self.maxCoverageExponent))
        if candidate.isFinite && candidate > sessionMaxAttainable {
            sessionMaxAttainable = min(candidate, ceiling)
        }

        // Adopt the server's configured speed once, at startup, so a machine
        // launched with --speed is reflected. After that the slider/toggle own
        // this state (the GUI is the only thing changing it).
        if !hasInitialised {
            hasInitialised = true
            if stats.speedMultiplier == 0.0 {
                isUnlimited = true
            } else {
                configuredMultiplier = stats.speedMultiplier
            }
        }
    }

    // MARK: - User actions

    /// Apply a multiplier from the slider (turns off unlimited).
    func apply(multiplier: Double) {
        configuredMultiplier = multiplier
        isUnlimited = false
        Task { await systemClient?.setSpeedMultiplier(multiplier) }
    }

    /// Toggle unlimited speed. When turned off, restores the slider's multiplier.
    func setUnlimited(_ unlimited: Bool) {
        isUnlimited = unlimited
        let target = unlimited ? 0.0 : configuredMultiplier
        Task { await systemClient?.setSpeedMultiplier(target) }
    }
}
