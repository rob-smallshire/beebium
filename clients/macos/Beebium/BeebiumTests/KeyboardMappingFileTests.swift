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
import AppKit
@testable import Beebium

/// Tests for the keyboard mapping file model: reading version 2 and converting
/// version 1 to version 2 (issue #231, docs/keyboard-mapping-files.md sections 2,
/// 3 and 7). These are pure (no file IO); the manager's rewrite-on-load is built
/// on top.
final class KeyboardMappingFileTests: XCTestCase {

    // MARK: - Version detection

    func testVersion1FileNeedsRewrite() {
        let file = KeyboardMappingFile(json: [
            "version": 1, "id": UUID().uuidString, "name": "Old",
            "keyMappings": []
        ])
        XCTAssertTrue(file.needsRewrite)
    }

    func testVersion2FileDoesNotNeedRewrite() {
        let file = KeyboardMappingFile(json: [
            "version": 2, "id": UUID().uuidString, "name": "New",
            "keys": []
        ])
        XCTAssertFalse(file.needsRewrite)
    }

    func testToJSONAlwaysWritesVersion2WithKeys() {
        let file = KeyboardMappingFile(json: [
            "version": 1, "id": UUID().uuidString, "name": "Old",
            "keyMappings": [["macOS": ["keyCode": 6], "bbc": ["keyName": "Z"]]]
        ])
        let json = file.toJSON()
        XCTAssertEqual(json["version"] as? Int, 2)
        XCTAssertNotNil(json["keys"], "version 2 uses 'keys'")
        XCTAssertNil(json["keyMappings"], "'keyMappings' is a version 1 name")
    }

    // MARK: - Version 1 conversion (section 7)

    func testV1MacKeyCodeBecomesCode() {
        let file = KeyboardMappingFile(json: [
            "version": 1, "id": UUID().uuidString, "name": "T",
            "keyMappings": [["macOS": ["keyCode": 6], "bbc": ["keyName": "Z"]]]
        ])
        let entry = file.entries.first
        XCTAssertEqual(entry?.code, "KeyZ")
        XCTAssertEqual(entry?.bbc?.key, "Z")
        XCTAssertNil(entry?.platform, "a key with a neutral code is portable, not platform specific")
    }

    func testV1ModifiersBecomeWith() {
        // Option+F12 -> Break, the built-ins' Break binding.
        let file = KeyboardMappingFile(json: [
            "version": 1, "id": UUID().uuidString, "name": "T",
            "keyMappings": [[
                "macOS": ["keyCode": 111, "option": true],
                "windows": ["VKCode": 123, "alt": true],
                "bbc": ["keyName": "Break"]
            ]]
        ])
        let entry = file.entries.first
        XCTAssertEqual(entry?.code, "F12")
        XCTAssertEqual(entry?.withModifiers, ["Alt"])
        XCTAssertEqual(entry?.bbc?.key, "Break")
        // The Windows virtual key is not a position; it is dropped because the
        // macOS key gives a code.
        XCTAssertNil(entry?.rawJSON["windows"])
    }

    func testV1FunctionModifierStaysPlatformEntry() {
        let file = KeyboardMappingFile(json: [
            "version": 1, "id": UUID().uuidString, "name": "T",
            "keyMappings": [[
                "macOS": ["keyCode": 103, "function": true],
                "bbc": ["keyName": "f0"]
            ]]
        ])
        let entry = file.entries.first
        XCTAssertNil(entry?.code, "the function modifier has no neutral form")
        XCTAssertEqual(entry?.platform, "macOS")
        XCTAssertEqual(entry?.rawJSON["keyCode"] as? Int, 103)
        XCTAssertEqual(entry?.rawJSON["function"] as? Bool, true)
        XCTAssertEqual(entry?.bbc?.key, "f0")
    }

    func testV1ShiftLockSpellingNormalised() {
        let file = KeyboardMappingFile(json: [
            "version": 1, "id": UUID().uuidString, "name": "T",
            "keyMappings": [["macOS": ["keyCode": 105], "bbc": ["keyName": "ShiftLock"]]]
        ])
        XCTAssertEqual(file.entries.first?.bbc?.key, "Shift Lock",
                       "the legacy ShiftLock spelling takes the catalogue's Shift Lock")
    }

    func testV1PureWindowsEntryPreservedAsPlatform() {
        let file = KeyboardMappingFile(json: [
            "version": 1, "id": UUID().uuidString, "name": "T",
            "keyMappings": [["windows": ["VKCode": 13], "bbc": ["keyName": "Return"]]]
        ])
        let entry = file.entries.first
        XCTAssertEqual(entry?.platform, "windows")
        XCTAssertEqual(entry?.rawJSON["VKCode"] as? Int, 13)
        XCTAssertEqual(entry?.bbc?.key, "Return")
    }

    // MARK: - Preservation

    func testUnknownTopLevelFieldPreserved() {
        let file = KeyboardMappingFile(json: [
            "version": 2, "id": UUID().uuidString, "name": "T",
            "keys": [], "author": "someone"
        ])
        XCTAssertEqual(file.toJSON()["author"] as? String, "someone")
    }

    func testForeignPlatformEntryPreservedThroughRoundTrip() {
        let v2: [String: Any] = [
            "version": 2, "id": UUID().uuidString, "name": "T",
            "keys": [
                ["code": "KeyZ", "bbc": ["key": "Z"]],
                ["platform": "linux", "scancode": 44, "bbc": ["key": "Z"]]
            ]
        ]
        let file = KeyboardMappingFile(json: v2)
        let keys = file.toJSON()["keys"] as? [[String: Any]]
        XCTAssertEqual(keys?.count, 2)
        let linux = keys?.first(where: { $0["platform"] as? String == "linux" })
        XCTAssertEqual(linux?["scancode"] as? Int, 44, "a foreign platform entry survives untouched")
    }

    // MARK: - macOS resolver view

    func testCodeEntryResolvesToMacKeyCodeAndModifiers() {
        let file = KeyboardMappingFile(json: [
            "version": 2, "id": UUID().uuidString, "name": "T",
            "keys": [["code": "F12", "with": ["Alt"], "bbc": ["key": "Break"]]]
        ])
        let spec = file.entries.first?.macOS
        XCTAssertEqual(spec?.keyCode, 111)
        XCTAssertEqual(spec?.requiredModifiers, [.option])
    }

    func testForeignCodeHasNoMacResolution() {
        // A code this host has no key for does not resolve here, but is kept.
        let file = KeyboardMappingFile(json: [
            "version": 2, "id": UUID().uuidString, "name": "T",
            "keys": [["code": "IntlRo", "bbc": ["key": "Z"]]]
        ])
        // IntlRo maps to a kVK on macOS (94), so pick a genuinely absent code:
        let file2 = KeyboardMappingFile(json: [
            "version": 2, "id": UUID().uuidString, "name": "T",
            "keys": [["code": "MediaPlayPause", "bbc": ["key": "Z"]]]
        ])
        XCTAssertNotNil(file.entries.first?.macOS)
        XCTAssertNil(file2.entries.first?.macOS, "a code with no macOS key does not resolve here")
    }

    // MARK: - Conditional entries: when (section 3.5)

    private func entry(_ raw: [String: Any]) -> KeyMappingEntry {
        let file = KeyboardMappingFile(json: [
            "version": 2, "id": UUID().uuidString, "name": "T", "keys": [raw]
        ])
        return file.entries[0]
    }

    func testEntryWithoutWhenAppliesHere() {
        let e = entry(["code": "KeyZ", "bbc": ["key": "Z"]])
        XCTAssertTrue(e.appliesToThisHost)
        XCTAssertNotNil(e.macOS, "an unconditional entry resolves on this host")
    }

    func testWhenPlatformListIncludingMacOSApplies() {
        let e = entry(["code": "KeyZ", "when": ["platform": ["macOS", "linux"]],
                       "bbc": ["key": "Z"]])
        XCTAssertTrue(e.appliesToThisHost)
        XCTAssertNotNil(e.macOS)
    }

    func testWhenPlatformSingleNameMacOSApplies() {
        let e = entry(["code": "KeyZ", "when": ["platform": "macOS"],
                       "bbc": ["key": "Z"]])
        XCTAssertTrue(e.appliesToThisHost)
    }

    func testWhenPlatformOtherPlatformsSkippedHere() {
        // The Logical Alt+F11 Shift Lock, which is for the platforms whose
        // keyboards lack F13: invisible to the macOS resolver.
        let e = entry(["code": "F11", "with": ["Alt"],
                       "when": ["platform": ["linux", "windows"]],
                       "bbc": ["key": "Shift Lock", "action": "Shift Lock"]])
        XCTAssertFalse(e.appliesToThisHost)
        XCTAssertNil(e.macOS, "an entry for other platforms does not resolve here")
    }

    func testWhenPlatformSingleOtherNameSkippedHere() {
        let e = entry(["code": "KeyZ", "when": ["platform": "linux"],
                       "bbc": ["key": "Z"]])
        XCTAssertFalse(e.appliesToThisHost)
    }

    func testUnknownPredicateDoesNotHold() {
        let e = entry(["code": "KeyZ", "when": ["x-later-predicate": "ISO"],
                       "bbc": ["key": "Z"]])
        XCTAssertFalse(e.appliesToThisHost, "an unknown predicate does not hold")
        XCTAssertNil(e.macOS)
    }

    func testUnknownPlatformValueShapeDoesNotHold() {
        let e = entry(["code": "KeyZ", "when": ["platform": 42],
                       "bbc": ["key": "Z"]])
        XCTAssertFalse(e.appliesToThisHost, "a value of an unknown shape does not hold")
    }

    func testEveryPredicateMustHold() {
        // A known predicate holding does not outweigh an unknown one alongside it.
        let e = entry(["code": "KeyZ",
                       "when": ["platform": ["macOS"], "x-later-predicate": "ISO"],
                       "bbc": ["key": "Z"]])
        XCTAssertFalse(e.appliesToThisHost)
    }

    func testEmptyWhenHoldsEverywhere() {
        let e = entry(["code": "KeyZ", "when": [:], "bbc": ["key": "Z"]])
        XCTAssertTrue(e.appliesToThisHost, "an empty when imposes no predicate")
        XCTAssertNotNil(e.macOS)
    }

    func testSkippedConditionalEntryRoundTripsThroughSave() {
        let raw: [String: Any] = [
            "code": "F11", "with": ["Alt"],
            "when": ["platform": ["linux", "windows"]],
            "bbc": ["key": "Shift Lock", "action": "Shift Lock"]
        ]
        let v2: [String: Any] = [
            "version": 2, "id": UUID().uuidString, "name": "T",
            "keys": [["code": "KeyZ", "bbc": ["key": "Z"]], raw]
        ]
        let file = KeyboardMappingFile(json: v2)
        // It does not resolve here, yet it is written back verbatim.
        XCTAssertFalse(file.entries[1].appliesToThisHost)
        let keys = file.toJSON()["keys"] as? [[String: Any]]
        XCTAssertEqual(keys?.count, 2)
        let kept = keys?.first(where: { ($0["when"] as? [String: Any]) != nil })
        let when = kept?["when"] as? [String: Any]
        XCTAssertEqual(when?["platform"] as? [String], ["linux", "windows"],
                       "a skipped conditional entry survives a save untouched")
        XCTAssertEqual(kept?["with"] as? [String], ["Alt"])
    }
}
