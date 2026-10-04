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

final class NameTemplateNoteTests: XCTestCase {

    func testNoProblemsIsEmpty() {
        XCTAssertEqual(NameTemplateNote.text(unknownKeys: [], malformed: []), "")
    }

    func testOneUnknownKey() {
        XCTAssertEqual(
            NameTemplateNote.text(unknownKeys: ["bogus"], malformed: []),
            "Unknown placeholder {bogus}")
    }

    func testSeveralUnknownKeys() {
        XCTAssertEqual(
            NameTemplateNote.text(unknownKeys: ["a", "b"], malformed: []),
            "Unknown placeholders {a}, {b}")
    }

    func testMalformedOnly() {
        XCTAssertEqual(
            NameTemplateNote.text(unknownKeys: [], malformed: ["{"]),
            "Malformed: {")
    }

    func testUnknownAndMalformedAreJoined() {
        XCTAssertEqual(
            NameTemplateNote.text(unknownKeys: ["bogus"], malformed: ["{"]),
            "Unknown placeholder {bogus}; Malformed: {")
    }
}

final class NameTemplatePlaceholderTooltipTests: XCTestCase {

    func testLabelAndDescriptionAreJoined() {
        XCTAssertEqual(
            NameTemplatePlaceholderTooltip.text(
                label: "Econet station",
                description: "The station number in force."),
            "Econet station: The station number in force.")
    }

    func testLabelOnlyWhenNoDescription() {
        XCTAssertEqual(
            NameTemplatePlaceholderTooltip.text(label: "Preset", description: ""),
            "Preset")
    }

    func testDescriptionOnlyWhenNoLabel() {
        XCTAssertEqual(
            NameTemplatePlaceholderTooltip.text(label: "", description: "Just this."),
            "Just this.")
    }
}

@MainActor
final class DebouncerTests: XCTestCase {

    func testOnlyTheLastOfABurstFires() {
        let debouncer = Debouncer(delay: 0.05)
        let fired = expectation(description: "fired once")
        var count = 0

        // Three rapid schedules; only the last should survive the interval.
        debouncer.schedule { count += 1 }
        debouncer.schedule { count += 1 }
        debouncer.schedule {
            count += 1
            fired.fulfill()
        }

        wait(for: [fired], timeout: 1.0)
        // Give any erroneously-surviving earlier work a chance to fire too.
        let settle = expectation(description: "settle")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { settle.fulfill() }
        wait(for: [settle], timeout: 1.0)
        XCTAssertEqual(count, 1)
    }

    func testCancelPreventsFiring() {
        let debouncer = Debouncer(delay: 0.05)
        var fired = false
        debouncer.schedule { fired = true }
        debouncer.cancel()

        let settle = expectation(description: "settle")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { settle.fulfill() }
        wait(for: [settle], timeout: 1.0)
        XCTAssertFalse(fired)
    }
}
