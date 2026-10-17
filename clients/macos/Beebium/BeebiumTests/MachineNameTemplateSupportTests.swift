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

import XCTest
@testable import Beebium

final class NameTemplateEditingTests: XCTestCase {

    func testInsertAtCaretInTheMiddle() {
        // Caret between "Station " and "(AUN)".
        let (text, caret) = NameTemplateEditing.insert(
            "{econet-station}",
            into: "Station (AUN)",
            replacingUTF16: 8..<8)
        XCTAssertEqual(text, "Station {econet-station}(AUN)")
        XCTAssertEqual(caret, 8 + ("{econet-station}" as NSString).length)
    }

    func testInsertAtTheEnd() {
        let (text, caret) = NameTemplateEditing.insert(
            " #1", into: "BBC Model B", replacingUTF16: 11..<11)
        XCTAssertEqual(text, "BBC Model B #1")
        XCTAssertEqual(caret, 14)
    }

    func testInsertReplacesASelection() {
        // "80" selected (offsets 8..<10) and replaced by the placeholder.
        let (text, caret) = NameTemplateEditing.insert(
            "{econet-station}",
            into: "Station 80",
            replacingUTF16: 8..<10)
        XCTAssertEqual(text, "Station {econet-station}")
        XCTAssertEqual(caret, 8 + ("{econet-station}" as NSString).length)
    }

    func testOffsetsBeyondTheTextAreClamped() {
        // A stale selection past the end must not crash or corrupt.
        let (text, caret) = NameTemplateEditing.insert(
            "X", into: "abc", replacingUTF16: 10..<20)
        XCTAssertEqual(text, "abcX")
        XCTAssertEqual(caret, 4)
    }
}
