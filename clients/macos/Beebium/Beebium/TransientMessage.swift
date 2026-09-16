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

/// A drive's current status message, of one of two deliberately distinct kinds.
///
/// A `notice` is a short, self-explanatory precondition ("Eject disc first"):
/// it needs no elaboration, so it shows briefly and clears itself. A `failure`
/// carries the server's full verbatim reason and must not be lost or truncated:
/// it is persistent (no timer), the UI opens its detail immediately and leaves
/// an inline marker as the re-entry point, and it goes away only when the user
/// clears it, a new outcome replaces it, or the drive is ejected.
///
/// Both floppy drive views -- the live sidebar and the New Machine dialog --
/// report failures the same way, so the behaviour lives here once.
@MainActor
final class TransientMessage: ObservableObject {
    enum Kind: Equatable {
        /// Self-clearing; the inline line shows `text`. No detail, no popover.
        case notice(String)
        /// Persistent; the inline marker shows `brief` (a short, class-agnostic
        /// label like "Insert failed"), and `detail` is the server's full
        /// message shown in the popover -- never inline, so it cannot truncate.
        case failure(brief: String, detail: String)
    }

    @Published private(set) var kind: Kind?

    /// Bumped on each `showFailure`, so a view can open the detail popover once
    /// per failure ("popover-first") without reopening it every time the marker
    /// re-appears -- which would fight a user who closed it but kept the marker.
    @Published private(set) var failureToken = 0

    /// Seconds a `notice` lingers before clearing itself. Failures ignore it.
    private let lifetime: Double
    private var dismissal: Task<Void, Never>?

    init(lifetime: Double = 6.0) {
        self.lifetime = lifetime
    }

    /// The text of a self-clearing notice, or nil when none is showing.
    var noticeText: String? {
        if case .notice(let text) = kind { return text }
        return nil
    }

    /// The current persistent failure's (brief, detail), or nil when none.
    var failure: (brief: String, detail: String)? {
        if case .failure(let brief, let detail) = kind { return (brief, detail) }
        return nil
    }

    /// Show a short, self-explanatory notice; it clears itself after `lifetime`.
    /// For preconditions, not server failures (those use `showFailure`).
    func show(_ brief: String) {
        kind = .notice(brief)
        scheduleDismissal()
    }

    /// Show a persistent failure carrying the server's full message. It does not
    /// time out; it is replaced by the next `show`/`showFailure`, or removed by
    /// `clear`. `brief` is the inline marker label; `detail` is the popover text.
    func showFailure(_ brief: String, detail: String) {
        dismissal?.cancel()
        dismissal = nil
        kind = .failure(brief: brief, detail: detail)
        failureToken &+= 1
    }

    func clear() {
        dismissal?.cancel()
        dismissal = nil
        kind = nil
    }

    private func scheduleDismissal() {
        dismissal?.cancel()
        dismissal = Task { [weak self] in
            guard let lifetime = self?.lifetime else { return }
            try? await Task.sleep(nanoseconds: UInt64(lifetime * 1_000_000_000))
            guard !Task.isCancelled else { return }
            await MainActor.run { self?.kind = nil }
        }
    }
}
