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
        /// The top-level catalogue slice: a machine, a Model B, with no `catalogue`
        /// named on the case.
        let cache: BBCKeyCache
        /// A cache per named catalogue slice (e.g. "master-128"): the top-level
        /// rows plus that slice's `adds`. A case names one with `catalogue`.
        let catalogues: [String: BBCKeyCache]
        let inlineMappings: [String: [String: Any]]
        let directoryURL: URL
        let cases: [[String: Any]]

        /// The cache a case resolves under: the named catalogue slice when it
        /// carries `catalogue`, else the top-level slice (a Model B's).
        func cache(forCase test: [String: Any]) throws -> BBCKeyCache {
            guard let name = test["catalogue"] as? String else { return cache }
            return try XCTUnwrap(catalogues[name], "unknown catalogue \(name)")
        }
    }

    /// A BBC key cache from an array of catalogue rows (the corpus's shape).
    private func makeCache(fromRows rows: [[String: Any]]) -> BBCKeyCache {
        var response = Beebium_AllKeyMappingsResponse()
        for row in rows {
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
        return cache
    }

    private func loadCorpus() throws -> Corpus {
        var root = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { root.deleteLastPathComponent() }  // -> repository root
        let directoryURL = root.appendingPathComponent("tests/assets/keyboard-mappings")
        let rulesURL = directoryURL.appendingPathComponent("rules.json")
        let data = try Data(contentsOf: rulesURL)
        let json = try XCTUnwrap(
            try JSONSerialization.jsonObject(with: data) as? [String: Any])

        // The top-level catalogue slice -- the machine a case with no `catalogue`
        // resolves under (a Model B's keys).
        let baseRows = try XCTUnwrap(
            (json["catalogue"] as? [String: Any])?["entries"] as? [[String: Any]])

        // Each named catalogue slice (e.g. "master-128") is the top-level slice
        // plus the rows in its `adds` (the Master 128's keypad). A case names one
        // with `catalogue`; 3.6 then makes an entry for a key in the slice apply
        // where a plain Model B's does not.
        var catalogues: [String: BBCKeyCache] = [:]
        for (name, slice) in (json["catalogues"] as? [String: [String: Any]] ?? [:]) {
            let adds = slice["adds"] as? [[String: Any]] ?? []
            catalogues[name] = makeCache(fromRows: baseRows + adds)
        }

        return Corpus(
            cache: makeCache(fromRows: baseRows),
            catalogues: catalogues,
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

            // The machine the case resolves under: its named catalogue slice, or
            // the top-level slice (a Model B). This is how one Logical mapping
            // serves both -- Numpad7 falls through to "7" on a Model B and resolves
            // to Keypad7 on a Master (3.6).
            let cache = try corpus.cache(forCase: test)
            let resolved = mapping.resolve(input, cache: cache)
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
                if let key = expect["key"] as? String {
                    XCTAssertEqual(resolved.bbcKeyName, key, "\(name): BBC key")
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

    /// Build a KeyInput from a corpus event. A platform: macOS case with a
    /// `native` key uses that key code (a 3.4 entry); a case with a `code`
    /// translates the W3C code to a macOS key code (nil, and so unrunnable here,
    /// if this host has no such key). A case carrying both a platform and a
    /// `code` but no `native` is a 3.5 condition on the platform: the top-level
    /// filter has already kept only macOS ones, so it too runs through the code
    /// path. The host character `text` already ignores Control, so it goes into
    /// both character fields.
    private func keyInput(from event: [String: Any]) -> KeyInput? {
        let modifiers = HostModifierName.flags(
            from: event["modifiers"] as? [String] ?? [])
        let text = event["text"] as? String ?? ""

        if event["platform"] as? String == "macOS",
           let native = event["native"] as? [String: Any] {
            guard let keyCode = native["keyCode"] as? Int else { return nil }
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
