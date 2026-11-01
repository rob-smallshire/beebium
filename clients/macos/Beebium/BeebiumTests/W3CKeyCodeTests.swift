// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Tests for the macOS-key-code to W3C-code translation table (issue #231).
///
/// The point of the table is that it must agree, byte for byte, with the shared
/// corpus every client converts against. These tests pin the embedded Swift
/// table equal to `tests/assets/keyboard-mappings/macos-keycodes.json`, and
/// check that the table is a clean bijection so the inverse direction is sound.
final class W3CKeyCodeTests: XCTestCase {

    /// Locate the shared corpus from this test's own source location, so the
    /// check does not depend on the test bundle's resources.
    private func corpusCodes() throws -> [UInt16: String] {
        // .../clients/macos/Beebium/BeebiumTests/W3CKeyCodeTests.swift
        var root = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { root.deleteLastPathComponent() }  // -> repository root
        let url = root
            .appendingPathComponent("tests/assets/keyboard-mappings/macos-keycodes.json")
        let data = try Data(contentsOf: url)
        let json = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        let codes = try XCTUnwrap(json?["codes"] as? [String: String],
                                  "macos-keycodes.json must carry a 'codes' object")
        var result: [UInt16: String] = [:]
        for (key, value) in codes {
            let number = try XCTUnwrap(UInt16(key), "key \(key) is not a kVK number")
            result[number] = value
        }
        return result
    }

    func testEmbeddedTableMatchesSharedCorpus() throws {
        let corpus = try corpusCodes()
        XCTAssertEqual(
            W3CKeyCode.codeByMacKeyCode, corpus,
            "The embedded macOS-key-code table has drifted from the shared corpus "
            + "macos-keycodes.json. Regenerate it from the corpus.")
    }

    func testForwardAndReverseAreConsistent() {
        for (keyCode, code) in W3CKeyCode.codeByMacKeyCode {
            XCTAssertEqual(W3CKeyCode.code(forMacKeyCode: keyCode), code)
            XCTAssertEqual(
                W3CKeyCode.macKeyCode(forCode: code), keyCode,
                "code \(code) did not round-trip back to kVK \(keyCode)")
        }
    }

    func testInverseIsABijection() {
        // Every code is distinct, so the inverse has one entry per forward entry.
        XCTAssertEqual(
            W3CKeyCode.macKeyCodeByCode.count,
            W3CKeyCode.codeByMacKeyCode.count,
            "two macOS key codes share a W3C code; the inverse would be ambiguous")
    }

    func testFunctionKeyHasNoNeutralCode() {
        // kVK_Function (63) has no W3C code value and must stay a platform entry.
        XCTAssertNil(W3CKeyCode.code(forMacKeyCode: 63))
    }

    func testKnownKeysTranslate() {
        // A handful of anchors from the built-in Default mapping.
        XCTAssertEqual(W3CKeyCode.code(forMacKeyCode: 6), "KeyZ")
        XCTAssertEqual(W3CKeyCode.code(forMacKeyCode: 36), "Enter")
        XCTAssertEqual(W3CKeyCode.code(forMacKeyCode: 53), "Escape")
        XCTAssertEqual(W3CKeyCode.code(forMacKeyCode: 111), "F12")
        XCTAssertEqual(W3CKeyCode.code(forMacKeyCode: 126), "ArrowUp")
        XCTAssertEqual(W3CKeyCode.macKeyCode(forCode: "ShiftLeft"), 56)
        XCTAssertEqual(W3CKeyCode.macKeyCode(forCode: "Backspace"), 51)
    }
}
