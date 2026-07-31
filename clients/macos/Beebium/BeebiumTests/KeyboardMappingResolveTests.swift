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

/// Tests for KeyboardMapping.resolve() focusing on the direct (keyMappings)
/// path. Specifically the regression where a direct keyMapping that names a
/// letter (e.g. Planetoid: keyCode 0 -> BBC "A" with no explicit shift) was
/// pulling in needsShift=true from the BBC key cache because the cache's
/// case-insensitive byName index was overwritten by the uppercase entry.
final class KeyboardMappingResolveTests: XCTestCase {

    // MARK: - Helpers

    private func makeBBCKeyCache() -> BBCKeyCache {
        var resp = Beebium_AllKeyMappingsResponse()

        // Lowercase 'a' -- unshifted face of BBC A key.
        var lower = Beebium_KeyMappingEntry()
        lower.found = true
        lower.character = "a"
        lower.ikNumber = 0x41
        lower.needsShift = false
        lower.name = "a"
        resp.mappings.append(lower)

        // Uppercase 'A' -- shifted face. Same ikNumber, different name.
        // In the production cache, the case-insensitive byName index would
        // see this load second and overwrite the lowercase entry. The
        // resolver must not let that bleed needsShift=true into a direct
        // mapping that just names "A".
        var upper = Beebium_KeyMappingEntry()
        upper.found = true
        upper.character = "A"
        upper.ikNumber = 0x41
        upper.needsShift = true
        upper.name = "A"
        resp.mappings.append(upper)

        // Z / z pair -- same shape, used by Planetoid as the "Down" game key.
        var lowerZ = Beebium_KeyMappingEntry()
        lowerZ.found = true
        lowerZ.character = "z"
        lowerZ.ikNumber = 0x61
        lowerZ.needsShift = false
        lowerZ.name = "z"
        resp.mappings.append(lowerZ)

        var upperZ = Beebium_KeyMappingEntry()
        upperZ.found = true
        upperZ.character = "Z"
        upperZ.ikNumber = 0x61
        upperZ.needsShift = true
        upperZ.name = "Z"
        resp.mappings.append(upperZ)

        // m / M pair -- the key at the heart of the CTRL regression.
        var lowerM = Beebium_KeyMappingEntry()
        lowerM.found = true
        lowerM.character = "m"
        lowerM.ikNumber = 0x65
        lowerM.needsShift = false
        lowerM.name = "m"
        resp.mappings.append(lowerM)

        var upperM = Beebium_KeyMappingEntry()
        upperM.found = true
        upperM.character = "M"
        upperM.ikNumber = 0x65
        upperM.needsShift = true
        upperM.name = "M"
        resp.mappings.append(upperM)

        // RETURN, which is what Ctrl+M must NOT resolve to: the host reports
        // that combination as "\r", the character RETURN also produces.
        var ret = Beebium_KeyMappingEntry()
        ret.found = true
        ret.character = "\r"
        ret.ikNumber = 0x49
        ret.needsShift = false
        ret.name = "Return"
        resp.mappings.append(ret)

        // Modifier-style names (no character).
        var shift = Beebium_KeyMappingEntry()
        shift.found = true
        shift.character = ""
        shift.ikNumber = 0x00
        shift.needsShift = false
        shift.name = "Shift"
        resp.mappings.append(shift)

        let cache = BBCKeyCache()
        cache.load(from: resp)
        return cache
    }

    /// Build a tiny KeyboardMapping with a single direct entry, no character mapping.
    private func makeMapping(
        keyCode: Int,
        bbcKeyName: String,
        bbcShift: Bool = false,
        characterMapping: Bool = false
    ) -> KeyboardMapping {
        var bbc: [String: Any] = ["keyName": bbcKeyName]
        if bbcShift { bbc["shift"] = true }
        let entry: [String: Any] = [
            "macOS": ["keyCode": keyCode],
            "bbc": bbc
        ]
        let json: [String: Any] = [
            "version": 1,
            "id": UUID().uuidString,
            "name": "Test",
            "characterMapping": characterMapping,
            "synchronizeCapsLock": false,
            "keyMappings": [entry]
        ]
        return KeyboardMapping(json: json)
    }

    private func keyInput(keyCode: UInt16) -> KeyInput {
        // The existing keycode-only initialiser leaves characters empty
        // and modifiers clear, which is exactly what the direct-mapping
        // path needs (it takes keyCode and dispatches before consulting
        // characters or modifiers for character mapping).
        return KeyInput(keyCode: keyCode, source: .physicalKeyboard)
    }

    // MARK: - Regression: Planetoid A / Z spuriously synthesise BBC SHIFT

    func testDirectMapping_LetterName_DoesNotSynthesiseShift() {
        let cache = makeBBCKeyCache()
        let mapping = makeMapping(keyCode: 0, bbcKeyName: "A")

        let resolved = mapping.resolve(keyInput(keyCode: 0), cache: cache)

        XCTAssertNotNil(resolved)
        XCTAssertEqual(resolved?.ikNumber, 0x41)
        // The whole point: a direct mapping that names "A" without an
        // explicit shift=true must NOT cause BBC SHIFT to be pressed,
        // even though the byName cache returns the uppercase entry whose
        // needsShift is true.
        XCTAssertEqual(resolved?.bbcShift, false)
    }

    func testDirectMapping_LetterNameZ_DoesNotSynthesiseShift() {
        let cache = makeBBCKeyCache()
        let mapping = makeMapping(keyCode: 6, bbcKeyName: "Z")

        let resolved = mapping.resolve(keyInput(keyCode: 6), cache: cache)

        XCTAssertEqual(resolved?.bbcShift, false)
    }

    func testDirectMapping_ExplicitShift_StillHonoured() {
        // If the user really does want SHIFT synthesised for a direct
        // mapping (e.g. mapping a function key to BBC "A" expecting capital
        // A), they say so via shift=true in the JSON. That path must keep
        // working.
        let cache = makeBBCKeyCache()
        let mapping = makeMapping(keyCode: 0, bbcKeyName: "A", bbcShift: true)

        let resolved = mapping.resolve(keyInput(keyCode: 0), cache: cache)

        XCTAssertEqual(resolved?.bbcShift, true)
    }

    // MARK: - CTRL is not a character-generating modifier

    /// A mapping with character mapping on and no direct keyCode entries, so
    /// resolution must go through the character path.
    private func makeCharacterOnlyMapping() -> KeyboardMapping {
        let json: [String: Any] = [
            "version": 1,
            "id": UUID().uuidString,
            "name": "CharacterOnly",
            "characterMapping": true,
            "synchronizeCapsLock": false,
            "keyMappings": []
        ]
        return KeyboardMapping(json: json)
    }

    func testCtrlM_ResolvesToTheMKey_NotReturn() {
        // macOS reports Ctrl+M with characters "\r" -- the same character the
        // RETURN key produces. Resolving on that would press a different
        // physical key, which guest software scanning the keyboard matrix can
        // see (Commstar's viewdata mode does exactly that).
        let cache = makeBBCKeyCache()
        let mapping = makeCharacterOnlyMapping()
        let input = KeyInput(
            keyCode: 46,
            characters: "\r",
            charactersIgnoringModifiers: "m",
            modifiers: [.control]
        )

        let resolved = mapping.resolve(input, cache: cache)

        XCTAssertEqual(resolved?.ikNumber, 0x65, "Ctrl+M must press the M key")
        XCTAssertNotEqual(resolved?.ikNumber, 0x49, "Ctrl+M must not press RETURN")
    }

    func testPlainReturn_StillResolvesToReturn() {
        let cache = makeBBCKeyCache()
        let mapping = makeCharacterOnlyMapping()
        let input = KeyInput(
            keyCode: 36,
            characters: "\r",
            charactersIgnoringModifiers: "\r",
            modifiers: []
        )

        XCTAssertEqual(mapping.resolve(input, cache: cache)?.ikNumber, 0x49)
    }

    func testShiftStillGeneratesCharacters() {
        // SHIFT genuinely selects which character a key produces, so it must
        // still be resolved from the modified character.
        let cache = makeBBCKeyCache()
        let mapping = makeCharacterOnlyMapping()
        let input = KeyInput(
            keyCode: 46,
            characters: "M",
            charactersIgnoringModifiers: "m",
            modifiers: [.shift]
        )

        let resolved = mapping.resolve(input, cache: cache)

        XCTAssertEqual(resolved?.ikNumber, 0x65)
        XCTAssertEqual(resolved?.bbcShift, true, "the shifted face was requested")
    }
}
