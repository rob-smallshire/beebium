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

/// Runs the shared cross-client conformance corpus
/// (`tests/assets/keyboard-mappings/rules.json`, docs/keyboard-mapping-files.md
/// section 6, issue #231). Every client loads this file and must resolve each
/// case identically. This is the macOS client doing so through KeyboardMapping.
///
/// A case names a mapping (an inline one, or a built-in file in the corpus), a
/// host event (a W3C `code` with held modifiers and the character the layout
/// gives, or a platform entry's native key), and the expected BBC key, Break or
/// "ignored". The macOS client runs the no-platform cases and the macOS platform
/// cases; Linux and Windows platform cases are another client's to run.
final class KeyboardMappingCorpusTests: XCTestCase {

    private struct Corpus {
        let cache: BBCKeyCache
        let inlineMappings: [String: [String: Any]]
        let directoryURL: URL
        let cases: [[String: Any]]
    }

    private func loadCorpus() throws -> Corpus {
        var root = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { root.deleteLastPathComponent() }  // -> repository root
        let directoryURL = root.appendingPathComponent("tests/assets/keyboard-mappings")
        let rulesURL = directoryURL.appendingPathComponent("rules.json")
        let data = try Data(contentsOf: rulesURL)
        let json = try XCTUnwrap(
            try JSONSerialization.jsonObject(with: data) as? [String: Any])

        // Build the BBC key cache from the catalogue slice the corpus carries.
        let catalogue = try XCTUnwrap(
            (json["catalogue"] as? [String: Any])?["entries"] as? [[String: Any]])
        var response = Beebium_AllKeyMappingsResponse()
        for row in catalogue {
            var entry = Beebium_KeyMappingEntry()
            entry.found = true
            entry.character = row["character"] as? String ?? ""
            entry.ikNumber = UInt32(row["ik"] as? Int ?? 0)
            entry.needsShift = row["needsShift"] as? Bool ?? false
            entry.name = row["name"] as? String ?? ""
            response.mappings.append(entry)
        }
        let cache = BBCKeyCache()
        cache.load(from: response)

        return Corpus(
            cache: cache,
            inlineMappings: json["mappings"] as? [String: [String: Any]] ?? [:],
            directoryURL: directoryURL,
            cases: json["cases"] as? [[String: Any]] ?? [])
    }

    private func mapping(for reference: String, in corpus: Corpus) throws -> KeyboardMapping {
        if let inline = corpus.inlineMappings[reference] {
            return KeyboardMapping(json: inline)
        }
        // Otherwise it is a built-in file in the corpus directory.
        let url = corpus.directoryURL.appendingPathComponent(reference)
        let data = try Data(contentsOf: url)
        let json = try XCTUnwrap(
            try JSONSerialization.jsonObject(with: data) as? [String: Any],
            "could not read corpus mapping \(reference)")
        return KeyboardMapping(json: json)
    }

    func testCorpusResolvesIdentically() throws {
        let corpus = try loadCorpus()
        XCTAssertFalse(corpus.cases.isEmpty, "the corpus must carry cases")

        var ran = 0
        for test in corpus.cases {
            let name = test["name"] as? String ?? "(unnamed)"
            guard let event = test["event"] as? [String: Any],
                  let expect = test["expect"] as? [String: Any],
                  let reference = test["mapping"] as? String else {
                XCTFail("malformed case: \(name)")
                continue
            }

            // Run the no-platform cases and the macOS platform cases only.
            if let platform = event["platform"] as? String, platform != "macOS" {
                continue
            }

            let mapping = try self.mapping(for: reference, in: corpus)
            guard let input = keyInput(from: event) else {
                // A code this host has no key for: not runnable here.
                continue
            }

            let resolved = mapping.resolve(input, cache: corpus.cache)
            ran += 1

            if expect["break"] as? Bool == true {
                XCTAssertEqual(resolved?.isBreak, true, "\(name): expected Break")
            } else if expect["ignored"] as? Bool == true {
                XCTAssertNil(resolved, "\(name): expected the key to be ignored")
            } else {
                guard let resolved = resolved else {
                    XCTFail("\(name): expected a BBC key, got nothing")
                    continue
                }
                if let ik = expect["ik"] as? Int {
                    XCTAssertEqual(Int(resolved.ikNumber), ik, "\(name): internal key number")
                }
                XCTAssertEqual(resolved.bbcShift, expect["shift"] as? Bool ?? false,
                               "\(name): BBC SHIFT")
                XCTAssertEqual(resolved.bbcCtrl, expect["ctrl"] as? Bool ?? false,
                               "\(name): BBC CTRL")
            }
        }
        XCTAssertGreaterThan(ran, 0, "no corpus cases ran")
    }

    /// Build a KeyInput from a corpus event. A platform: macOS case uses its
    /// native key code; a code case translates the W3C code to a macOS key code
    /// (nil, and so unrunnable here, if this host has no such key). The host
    /// character `text` already ignores Control, so it goes into both character
    /// fields.
    private func keyInput(from event: [String: Any]) -> KeyInput? {
        let modifiers = HostModifierName.flags(
            from: event["modifiers"] as? [String] ?? [])
        let text = event["text"] as? String ?? ""

        if event["platform"] as? String == "macOS" {
            guard let native = event["native"] as? [String: Any],
                  let keyCode = native["keyCode"] as? Int else { return nil }
            return KeyInput(
                keyCode: UInt16(keyCode),
                characters: text,
                charactersIgnoringModifiers: text,
                modifiers: modifiers)
        }

        guard let code = event["code"] as? String,
              let keyCode = W3CKeyCode.macKeyCode(forCode: code) else { return nil }
        return KeyInput(
            keyCode: keyCode,
            characters: text,
            charactersIgnoringModifiers: text,
            modifiers: modifiers)
    }
}
