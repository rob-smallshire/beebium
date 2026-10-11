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

    // MARK: - EditableFieldModel: focus gain / edited / resign

    func testNoteFocusGainTracksEdited() {
        var model = EditableFieldModel<Int>(display: "10")
        XCTAssertFalse(model.isEdited)
        model.noteFocusGain()
        XCTAssertFalse(model.isEdited)
        model.type("12")
        XCTAssertTrue(model.isEdited)       // user changed the text
        model.noteFocusGain()               // re-baseline on a fresh focus
        XCTAssertFalse(model.isEdited)
    }

    func testResignUneditedRefreshesInsteadOfCommittingStale() {
        // The field shows 10 and the user does not type. Another control changes
        // the value to 20 while the field is focused (so syncExternal is ignored).
        // On resign the field must refresh to 20, never commit its stale 10 over
        // the other control's change -- the hex-stays-FF bug.
        var model = EditableFieldModel<Int>(display: "10")
        model.noteFocusGain()
        model.syncExternal(display: "20", focused: true)   // ignored mid-focus
        XCTAssertEqual(model.text, "10")
        XCTAssertFalse(model.isEdited)
        // Resign path for an unedited field: adopt the current value.
        model.syncExternal(display: "20", focused: false)
        XCTAssertEqual(model.text, "20")
        XCTAssertFalse(model.invalid)
    }

    func testCommitSettlesFieldSoReturnStopsThere() {
        // Return commits the field (the View's onSubmit calls commit); afterwards
        // the field is no longer "edited", so a later focus loss to another control
        // refreshes rather than committing the same value again. This is what keeps
        // Return from doing more than committing the field it is in.
        var model = EditableFieldModel<Int>(display: "10")
        model.noteFocusGain()
        model.type("42")
        XCTAssertTrue(model.isEdited)
        let committed = model.commit(currentDisplay: "10", parse: decParse, format: decFormat)
        XCTAssertEqual(committed, 42)
        XCTAssertFalse(model.isEdited)          // settled: a subsequent resign won't re-commit
        XCTAssertEqual(model.text, "42")
    }

    func testEditedFieldCommitsOnResign() {
        // When the user did edit, the resign path commits their text.
        var model = EditableFieldModel<Int>(display: "10")
        model.noteFocusGain()
        model.type("42")
        XCTAssertTrue(model.isEdited)
        let committed = model.commit(currentDisplay: "10", parse: decParse, format: decFormat)
        XCTAssertEqual(committed, 42)
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

    // MARK: - Focus-cycle invariants (Tab between two fields over one byte)

    /// Two fields (hex + decimal) over one byte, driven through the exact
    /// focus/blur/commit sequence CommittingField's bridge produces. Used to lock
    /// the invariants a Tab cycle must hold.
    private struct ByteFieldPair {
        var byte: Int
        var hex: EditableFieldModel<Int>
        var dec: EditableFieldModel<Int>

        init(byte: Int) {
            self.byte = byte
            hex = EditableFieldModel(display: Self.hexFmt(byte))
            dec = EditableFieldModel(display: Self.decFmt(byte))
        }

        static func hexFmt(_ b: Int) -> String { String(format: "%02X", b) }
        static func decFmt(_ b: Int) -> String { String(b) }
        static func hexParse(_ s: String) -> Int? {
            guard let v = Int(s.trimmingCharacters(in: .whitespaces), radix: 16),
                  (0...255).contains(v) else { return nil }
            return v
        }
        static func decParse(_ s: String) -> Int? {
            guard let v = Int(s.trimmingCharacters(in: .whitespaces)), (0...255).contains(v) else { return nil }
            return v
        }

        mutating func focusHex() { hex.noteFocusGain() }
        mutating func focusDec() { dec.noteFocusGain() }
        mutating func typeHex(_ s: String) { hex.type(s) }
        mutating func typeDec(_ s: String) { dec.type(s) }

        mutating func blurHex() {
            if hex.isEdited {
                if let v = hex.commit(currentDisplay: Self.hexFmt(byte),
                                      parse: Self.hexParse, format: Self.hexFmt) {
                    byte = v
                    dec.syncExternal(display: Self.decFmt(byte), focused: false)
                }
            } else {
                hex.syncExternal(display: Self.hexFmt(byte), focused: false)
            }
        }
        mutating func blurDec() {
            if dec.isEdited {
                if let v = dec.commit(currentDisplay: Self.decFmt(byte),
                                      parse: Self.decParse, format: Self.decFmt) {
                    byte = v
                    hex.syncExternal(display: Self.hexFmt(byte), focused: false)
                }
            } else {
                dec.syncExternal(display: Self.decFmt(byte), focused: false)
            }
        }
    }

    func testHexDecimalTabCyclePreservesEdit() {
        // The user's exact sequence: edit hex to 8, then Tab round the pair.
        var p = ByteFieldPair(byte: 255)
        XCTAssertEqual(p.hex.text, "FF")
        XCTAssertEqual(p.dec.text, "255")
        p.focusHex(); p.typeHex("8")
        p.blurHex(); p.focusDec()               // Tab 1
        XCTAssertEqual(p.byte, 8)
        XCTAssertEqual(p.hex.text, "08")
        XCTAssertEqual(p.dec.text, "8")
        p.blurDec(); p.focusHex()               // Tab 2
        XCTAssertEqual(p.hex.text, "08")
        XCTAssertEqual(p.dec.text, "8")
        p.blurHex(); p.focusDec()               // Tab 3 -- the one that regressed to FF
        XCTAssertEqual(p.hex.text, "08", "hex must stay 08, not revert to the pre-edit FF")
        XCTAssertEqual(p.dec.text, "8")
        XCTAssertEqual(p.byte, 8)
        // The edit survives any number of further cycles.
        for _ in 0..<6 { p.blurDec(); p.focusHex(); p.blurHex(); p.focusDec() }
        XCTAssertEqual(p.byte, 8)
        XCTAssertEqual(p.hex.text, "08")
        XCTAssertEqual(p.dec.text, "8")
    }

    func testFocusMovementWithoutEditNeverChangesByte() {
        // Tabbing round the pair without typing must not change the byte or either
        // field's text (no spurious commit on focus movement).
        var p = ByteFieldPair(byte: 0x73)
        for _ in 0..<10 {
            p.focusHex(); p.blurHex()
            p.focusDec(); p.blurDec()
            XCTAssertEqual(p.byte, 0x73)
            XCTAssertEqual(p.hex.text, "73")
            XCTAssertEqual(p.dec.text, "115")
        }
    }

    func testEditThenFocusCyclesSurvive_singleField() {
        // An independent field (e.g. Econet port/net/station): an edit commits on
        // blur and survives repeated focus/blur with no further edit.
        var model = EditableFieldModel<Int>(display: "32768")
        var committed = 32768
        let parse: (String) -> Int? = { Int($0.trimmingCharacters(in: .whitespaces)) }  // a port: wide range
        func blur() {
            if model.isEdited {
                if let v = model.commit(currentDisplay: String(committed), parse: parse, format: { String($0) }) {
                    committed = v
                }
            } else {
                model.syncExternal(display: String(committed), focused: false)
            }
        }
        model.noteFocusGain(); model.type("40000"); blur()
        XCTAssertEqual(committed, 40000)
        XCTAssertEqual(model.text, "40000")
        for _ in 0..<8 { model.noteFocusGain(); blur() }
        XCTAssertEqual(committed, 40000)
        XCTAssertEqual(model.text, "40000")
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
