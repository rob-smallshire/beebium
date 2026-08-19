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

/// A brief message that shows for a moment and then clears itself.
///
/// For a passing failure a control needs to report -- a rejected drop, a
/// refused action -- without leaving it stuck on screen. Set it and forget it:
/// it dismisses on its own after a while, or on demand. The `brief` line is
/// what a row shows; the `detail` (the same by default) is for a tooltip, so
/// a shortened message can still carry the fuller reason behind it.
///
/// Both floppy drive views -- the live sidebar and the preset editor -- report
/// the same kinds of failure the same way, so the behaviour lives here once
/// rather than in each.
@MainActor
final class TransientMessage: ObservableObject {
    struct Content: Equatable {
        let brief: String
        let detail: String
    }

    @Published private(set) var content: Content?

    /// Seconds a message lingers before clearing itself.
    private let lifetime: Double
    private var dismissal: Task<Void, Never>?

    init(lifetime: Double = 6.0) {
        self.lifetime = lifetime
    }

    var brief: String? { content?.brief }
    var detail: String? { content?.detail }

    /// Show `brief` now; it clears itself after `lifetime`. `detail` is the
    /// fuller text behind it, defaulting to `brief`.
    func show(_ brief: String, detail: String? = nil) {
        content = Content(brief: brief, detail: detail ?? brief)
        dismissal?.cancel()
        dismissal = Task { [weak self] in
            guard let lifetime = self?.lifetime else { return }
            try? await Task.sleep(nanoseconds: UInt64(lifetime * 1_000_000_000))
            guard !Task.isCancelled else { return }
            await MainActor.run { self?.content = nil }
        }
    }

    func clear() {
        dismissal?.cancel()
        dismissal = nil
        content = nil
    }
}
