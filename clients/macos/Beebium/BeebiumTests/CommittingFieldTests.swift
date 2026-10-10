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

/// The field model behind CommittingField: typing, deletion, commit and revert.
/// These exercise the pure logic that keeps the insertion caret alive (a field
/// that commits only on Return/focus loss, not per keystroke).
final class CommittingFieldTests: XCTestCase {

    // A decimal-byte field: parse accepts 0...255, rejects out-of-range and junk.
    private func decParse(_ text: String) -> Int? {
        let t = text.trimmingCharacters(in: .whitespaces)
        guard let v = Int(t), (0...255).contains(v) else { return nil }
        return v
    }
    private func decFormat(_ value: Int) -> String { String(value) }

    // MARK: - CommittingFieldLogic.outcome

    func testOutcomeAcceptsAndCanonicalises() {
        // A hex entry with a prefix parses and renders back canonically.
        let outcome = CommittingFieldLogic.outcome(
            entry: "&0f", parse: KeyboardSectionView.parseHexByte,
            format: KeyboardSectionView.formatHexByte)
        XCTAssertEqual(outcome, .accept(0x0F, display: KeyboardSectionView.formatHexByte(0x0F)))
    }

    func testOutcomeEmptyReverts() {
        let outcome: CommittingFieldLogic.Outcome<Int> = CommittingFieldLogic.outcome(
            entry: "", parse: decParse, format: decFormat)
        XCTAssertEqual(outcome, .revertEmpty)
    }

    func testOutcomeInvalidReverts() {
        let outcome: CommittingFieldLogic.Outcome<Int> = CommittingFieldLogic.outcome(
            entry: "zz", parse: decParse, format: decFormat)
        XCTAssertEqual(outcome, .revertInvalid)
        let outOfRange: CommittingFieldLogic.Outcome<Int> = CommittingFieldLogic.outcome(
            entry: "300", parse: decParse, format: decFormat)
        XCTAssertEqual(outOfRange, .revertInvalid)
    }

    func testOutcomeAcceptsEmptyWhenParserDoes() {
        // A string field whose parser accepts anything treats blank as a value.
        let outcome = CommittingFieldLogic.outcome(entry: "", parse: { $0 }, format: { $0 })
        XCTAssertEqual(outcome, .accept("", display: ""))
    }

    // MARK: - EditableFieldModel: typing, deletion

    func testTypingClearsInvalidMarking() {
        var model = EditableFieldModel<Int>(display: "10")
        // A bad commit marks the field invalid.
        _ = model.commit(currentDisplay: "10", parse: decParse, format: decFormat)  // text is "10", valid
        XCTAssertFalse(model.invalid)
        model.type("zz")
        XCTAssertEqual(model.text, "zz")
        _ = model.commit(currentDisplay: "10", parse: decParse, format: decFormat)
        XCTAssertTrue(model.invalid)
        // Typing again clears the marking immediately.
        model.type("1")
        XCTAssertFalse(model.invalid)
        XCTAssertEqual(model.text, "1")
    }

    func testDeletionIsJustTyping() {
        var model = EditableFieldModel<Int>(display: "31")
        model.type("3")   // deleted the '1'
        XCTAssertEqual(model.text, "3")
        XCTAssertFalse(model.invalid)
    }

    // MARK: - EditableFieldModel: commit

    func testCommitValidReturnsValueAndCanonicalises() {
        var model = EditableFieldModel<Int>(display: KeyboardSectionView.formatHexByte(0x1F))
        model.type("&a")
        let committed = model.commit(currentDisplay: KeyboardSectionView.formatHexByte(0x1F),
                                     parse: KeyboardSectionView.parseHexByte,
                                     format: KeyboardSectionView.formatHexByte)
        XCTAssertEqual(committed, 0x0A)
        XCTAssertEqual(model.text, KeyboardSectionView.formatHexByte(0x0A))
        XCTAssertFalse(model.invalid)
    }

    func testCommitInvalidRevertsAndMarks() {
        var model = EditableFieldModel<Int>(display: "10")
        model.type("999")
        let committed = model.commit(currentDisplay: "10", parse: decParse, format: decFormat)
        XCTAssertNil(committed)
        XCTAssertEqual(model.text, "10")   // reverted to the current value
        XCTAssertTrue(model.invalid)
    }

    func testCommitEmptyRevertsQuietly() {
        var model = EditableFieldModel<Int>(display: "10")
        model.type("")
        let committed = model.commit(currentDisplay: "10", parse: decParse, format: decFormat)
        XCTAssertNil(committed)
        XCTAssertEqual(model.text, "10")
        XCTAssertFalse(model.invalid)   // a blank is not an error
    }

    func testCommitEmptyWritesWhenParserAcceptsBlank() {
        // Integer parameter parser maps "" to "" (the clear sentinel).
        var model = EditableFieldModel<String>(display: "19200")
        model.type("")
        let parameter = Self.integerParameter(key: "baud", min: nil, max: nil)
        let committed = model.commit(
            currentDisplay: "19200",
            parse: { ExtensionParameterForm.parseIntegerEntry($0, parameter: parameter) },
            format: { $0 })
        XCTAssertEqual(committed, "")   // caller clears the key on empty
        XCTAssertFalse(model.invalid)
    }

    // MARK: - EditableFieldModel: external sync

    func testSyncExternalIgnoredWhileFocused() {
        var model = EditableFieldModel<Int>(display: "10")
        model.type("5")   // user is editing
        model.syncExternal(display: "20", focused: true)
        XCTAssertEqual(model.text, "5")   // untouched mid-edit
    }

    func testSyncExternalAdoptedWhenNotFocused() {
        var model = EditableFieldModel<Int>(display: "10")
        model.syncExternal(display: "20", focused: false)
        XCTAssertEqual(model.text, "20")
        XCTAssertFalse(model.invalid)
    }

    // MARK: - Integer parameter parsing (clamping)

    func testParseIntegerEntryClampsToBounds() {
        let parameter = Self.integerParameter(key: "net", min: 0, max: 127)
        XCTAssertEqual(ExtensionParameterForm.parseIntegerEntry("200", parameter: parameter), "127")
        XCTAssertEqual(ExtensionParameterForm.parseIntegerEntry("-5", parameter: parameter), "0")
        XCTAssertEqual(ExtensionParameterForm.parseIntegerEntry(" 42 ", parameter: parameter), "42")
        XCTAssertEqual(ExtensionParameterForm.parseIntegerEntry("", parameter: parameter), "")
        XCTAssertNil(ExtensionParameterForm.parseIntegerEntry("abc", parameter: parameter))
    }

    // MARK: - Fixtures

    private static func integerParameter(key: String, min: Int?, max: Int?) -> ExtensionParameter {
        let json = """
        {"key": "\(key)", "type": "integer", "default": "0"\
        \(min.map { ", \"min\": \($0)" } ?? "")\(max.map { ", \"max\": \($0)" } ?? "")}
        """
        return try! JSONDecoder().decode(ExtensionParameter.self, from: Data(json.utf8))
    }
}
