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

/// Section 3.6 of docs/keyboard-mapping-files.md: an entry whose BBC key the
/// connected machine does not have does not apply -- it is skipped as if absent,
/// so the host key falls through to character mapping and a sibling entry can
/// still win, the keyboard view omits it, and it is never declined. One mapping
/// file serves every model: Logical carries the Master 128's numeric-keypad
/// entries (Numpad7 -> Keypad7 and so on), and on a Model B those must not
/// capture the host keypad, whose digits arrive through character mapping.
@MainActor
final class KeyboardMappingMachineKeysTests: XCTestCase {

    // macOS virtual key code for the numeric-keypad 7 (kVK_ANSI_Keypad7).
    private let numpad7: UInt16 = 89

    // BBC matrix positions used by the fixtures (arbitrary but distinct).
    private let modelB7IK: UInt8 = 0x27
    private let masterKeypad7IK: UInt8 = 0x71

    // MARK: - Catalogue fixtures

    /// A catalogue entry for the key named `name`, producing `character` (empty
    /// for a named key with no character face).
    private func mapping(name: String, character: String, ikNumber: UInt8,
                         needsShift: Bool = false) -> Beebium_KeyMappingEntry {
        var entry = Beebium_KeyMappingEntry()
        entry.found = true
        entry.name = name
        entry.character = character
        entry.ikNumber = UInt32(ikNumber)
        entry.needsShift = needsShift
        return entry
    }

    private func loadedCache(_ entries: [Beebium_KeyMappingEntry]) -> BBCKeyCache {
        var response = Beebium_AllKeyMappingsResponse()
        response.mappings = entries
        let cache = BBCKeyCache()
        cache.load(from: response)
        return cache
    }

    /// A Model B catalogue: it has the ordinary top-row "7" key but no numeric
    /// keypad, so it does not list Keypad7.
    private func modelBCache() -> BBCKeyCache {
        loadedCache([mapping(name: "7", character: "7", ikNumber: modelB7IK)])
    }

    /// A Master 128 catalogue: the top-row "7" plus the keypad's Keypad7.
    private func masterCache() -> BBCKeyCache {
        loadedCache([
            mapping(name: "7", character: "7", ikNumber: modelB7IK),
            mapping(name: "Keypad7", character: "", ikNumber: masterKeypad7IK),
        ])
    }

    // MARK: - Mapping fixtures

    /// The Logical-style entry mapping the host keypad 7 to the Master's Keypad7,
    /// with character mapping on so a Model B keypad falls through to it.
    private func keypadMapping() -> KeyboardMapping {
        makeMapping(entries: [["macOS": ["keyCode": Int(numpad7)],
                               "bbc": ["keyName": "Keypad7"]]])
    }

    private func makeMapping(entries: [[String: Any]],
                             characterMapping: Bool = true) -> KeyboardMapping {
        let json: [String: Any] = [
            "version": 1,
            "id": UUID().uuidString,
            "name": "Test",
            "characterMapping": characterMapping,
            "synchronizeCapsLock": false,
            "keyMappings": entries,
        ]
        return KeyboardMapping(json: json)
    }

    /// The host keypad-7 event: the native key, with the digit it types so the
    /// character path has something to resolve.
    private func keypad7Event() -> KeyInput {
        KeyInput(keyCode: numpad7, characters: "7",
                 charactersIgnoringModifiers: "7", modifiers: [])
    }

    // MARK: - Resolution

    func testOnAModelBTheKeypadEntryFallsThroughToCharacterMappingAndTypesSeven() {
        let resolved = keypadMapping().resolve(keypad7Event(), cache: modelBCache())

        let key = try? XCTUnwrap(resolved, "the keypad must still type its digit on a Model B")
        XCTAssertEqual(key?.bbcKeyName, "7",
                       "the Keypad7 entry does not apply, so Numpad7 falls through to character mapping")
        XCTAssertEqual(key?.ikNumber, modelB7IK)
        XCTAssertTrue(key?.fromCharacterMapping ?? false,
                      "it is the ordinary 7 key reached by character mapping, not a physical entry")
    }

    func testOnAMasterTheKeypadEntryResolvesToTheKeypadKey() {
        let resolved = keypadMapping().resolve(keypad7Event(), cache: masterCache())

        let key = try? XCTUnwrap(resolved, "the Master has the Keypad7 key")
        XCTAssertEqual(key?.bbcKeyName, "Keypad7",
                       "the machine has the key, so the physical entry wins rule 1")
        XCTAssertEqual(key?.ikNumber, masterKeypad7IK)
        XCTAssertFalse(key?.fromCharacterMapping ?? true,
                       "it resolved through the physical entry, not character mapping")
    }

    func testASiblingEntryTheMachineHasStillWinsWhenAnotherDoesNotApply() {
        // Two entries for the same host key: Keypad7 (Master only) and the plain
        // "7" key. On a Model B the first does not apply, so the second wins
        // rule 1 directly rather than the key going dead or falling to character
        // mapping.
        let mapping = makeMapping(entries: [
            ["macOS": ["keyCode": Int(numpad7)], "bbc": ["keyName": "Keypad7"]],
            ["macOS": ["keyCode": Int(numpad7)], "bbc": ["keyName": "7"]],
        ], characterMapping: false)

        let resolved = mapping.resolve(keypad7Event(), cache: modelBCache())

        XCTAssertEqual(resolved?.bbcKeyName, "7")
        XCTAssertEqual(resolved?.ikNumber, modelB7IK)
        XCTAssertFalse(resolved?.fromCharacterMapping ?? true,
                       "the sibling physical entry won rule 1; character mapping is off anyway")
    }

    func testBreakAppliesEvenThoughItIsNotInTheCatalogue() {
        // Break is not a matrix key and is not listed in GetAllKeyMappings, but it
        // is present on every machine, so its entry must not be filtered out.
        let mapping = makeMapping(entries: [
            ["macOS": ["keyCode": 100], "bbc": ["keyName": "Break"]],
        ], characterMapping: false)
        let input = KeyInput(keyCode: 100, source: .physicalKeyboard)

        let resolved = mapping.resolve(input, cache: modelBCache())

        XCTAssertEqual(resolved?.bbcKeyName, "Break", "Break is present on every machine")
    }

    func testBeforeTheCatalogueLoadsNoEntryIsTreatedAsMissing() {
        // The machine is unknown until the catalogue loads; treating every key as
        // absent would blank the keyboard, so an unloaded cache keeps every entry.
        let emptyCache = BBCKeyCache()
        XCTAssertFalse(emptyCache.isLoaded)
        XCTAssertTrue(emptyCache.machineHasKey(named: "Keypad7"))
        XCTAssertTrue(emptyCache.machineHasKey(named: "7"))
    }

    // MARK: - Keyboard view

    func testTheReferenceTableOmitsAnEntryTheMachineLacks() {
        // An action-labelled entry for a key the connected machine does not have
        // is not shown in the keyboard view's reference table (3.6), while one the
        // machine has still appears.
        let manager = KeyboardMappingManager()
        let mapping = makeMapping(entries: [
            ["macOS": ["keyCode": Int(numpad7)],
             "bbc": ["keyName": "Keypad7", "action": "Keypad 7"]],
            ["macOS": ["keyCode": 123], "bbc": ["keyName": "Left", "action": "Cursor left"]],
        ])
        manager.bbcKeyCache.load(from: catalogueWithLeftButNoKeypad())
        manager.activeMapping = mapping

        let actions = manager.mappingReferenceEntries.map(\.action)
        XCTAssertTrue(actions.contains("Cursor left"), "a key the machine has is listed")
        XCTAssertFalse(actions.contains("Keypad 7"),
                       "a key the machine lacks is omitted from the reference table")
    }

    func testDisableableKeysOmitKeysTheMachineLacks() {
        let manager = KeyboardMappingManager()
        let mapping = makeMapping(entries: [
            ["macOS": ["keyCode": Int(numpad7)],
             "bbc": ["keyName": "Keypad7", "disabled": false]],
            ["macOS": ["keyCode": 123], "bbc": ["keyName": "Left", "disabled": false]],
        ])
        manager.bbcKeyCache.load(from: catalogueWithLeftButNoKeypad())
        manager.activeMapping = mapping

        let names = manager.disableableKeyNames
        XCTAssertTrue(names.contains("Left"))
        XCTAssertFalse(names.contains("Keypad7"),
                       "a key the machine lacks is not offered as a key to switch off")
    }

    private func catalogueWithLeftButNoKeypad() -> Beebium_AllKeyMappingsResponse {
        var response = Beebium_AllKeyMappingsResponse()
        response.mappings = [
            mapping(name: "7", character: "7", ikNumber: modelB7IK),
            mapping(name: "Left", character: "", ikNumber: 0x19),
        ]
        return response
    }
}
