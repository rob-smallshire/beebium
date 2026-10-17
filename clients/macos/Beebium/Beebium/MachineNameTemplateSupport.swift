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

/// Inserting a placeholder's text at the caret of a single-line field. Kept
/// pure so the offset arithmetic is testable without a text field.
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
