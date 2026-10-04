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

/// Editing a machine-name template in the rename popover (#153): inserting a
/// placeholder's text at the caret. Kept pure so the offset arithmetic is
/// testable without a text field.
enum NameTemplateEditing {
    /// Insert `insertion` into `text`, replacing the half-open UTF-16 range
    /// `[lower, upper)` (a caret is an empty selection where lower == upper).
    /// Returns the new text and the new caret, which sits just after the
    /// inserted text. Offsets are measured and clamped in UTF-16, matching an
    /// NSTextField's selected range, so a stale selection cannot crash or
    /// corrupt the string.
    static func insert(_ insertion: String,
                       into text: String,
                       replacingUTF16 range: Range<Int>) -> (text: String, caret: Int) {
        let ns = text as NSString
        let lower = max(0, min(range.lowerBound, ns.length))
        let upper = max(lower, min(range.upperBound, ns.length))
        let nsRange = NSRange(location: lower, length: upper - lower)
        let newText = ns.replacingCharacters(in: nsRange, with: insertion)
        let caret = lower + (insertion as NSString).length
        return (newText, caret)
    }
}

/// Turns a server `NameTemplateReport` into a short note for the rename popover,
/// naming the parts of the template that will not render as the user might
/// expect (#153). Pure, so the wording is testable without a server.
enum NameTemplateNote {
    /// A one-line note naming the unknown placeholders and malformed fragments
    /// in a template, or "" when there is nothing to warn about. Inapplicable
    /// placeholders are not named here: they render empty by design and the
    /// picker already shows them dimmed, so flagging them as a problem would
    /// mislead.
    static func text(unknownKeys: [String], malformed: [String]) -> String {
        var parts: [String] = []
        if !unknownKeys.isEmpty {
            let keys = unknownKeys.map { "{\($0)}" }.joined(separator: ", ")
            parts.append(unknownKeys.count == 1
                ? "Unknown placeholder \(keys)"
                : "Unknown placeholders \(keys)")
        }
        if !malformed.isEmpty {
            let fragments = malformed.joined(separator: ", ")
            parts.append(malformed.count == 1
                ? "Malformed: \(fragments)"
                : "Malformed: \(fragments)")
        }
        return parts.joined(separator: "; ")
    }
}

/// The placeholder picker's rows show the template insertion text ("{econet-
/// station}") as a button, with the human label and description moved to the
/// tooltip (#153 refinement). This composes that tooltip. Pure, so the wording
/// is testable.
enum NameTemplatePlaceholderTooltip {
    static func text(label: String, description: String) -> String {
        if description.isEmpty { return label }
        if label.isEmpty { return description }
        return "\(label): \(description)"
    }
}

/// Coalesces rapidly repeated work into a single call after a quiet interval --
/// the rename popover's live preview waits for the user to stop typing before it
/// asks the server to render (#153). Each `schedule` cancels the previous
/// pending call, so only the last survives the interval.
@MainActor
final class Debouncer {
    private let delay: TimeInterval
    private var pending: DispatchWorkItem?

    init(delay: TimeInterval) {
        self.delay = delay
    }

    /// Run `action` after `delay`, unless another `schedule` arrives first.
    func schedule(_ action: @escaping () -> Void) {
        pending?.cancel()
        let item = DispatchWorkItem(block: action)
        pending = item
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: item)
    }

    /// Cancel any pending call. Nothing fires until the next `schedule`.
    func cancel() {
        pending?.cancel()
        pending = nil
    }
}
