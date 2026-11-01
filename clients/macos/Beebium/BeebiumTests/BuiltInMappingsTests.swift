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

/// Tests for the shipped built-in keyboard mappings (issue #231). The macOS
/// client ships the same version 2 built-ins as every other client, so this pins
/// the bundle's files equal to the shared corpus and checks their ratified ids
/// and names (docs/keyboard-mapping-files.md section 5). The manager loads these
/// from Bundle.main at runtime, which a unit test cannot see, so the files are
/// read from the source tree instead.
final class BuiltInMappingsTests: XCTestCase {

    /// name -> ratified id (section 5).
    private static let expected: [String: String] = [
        "Logical": "00000000-0000-0000-0000-000000000001",
        "Thrust": "00000000-0000-0000-0000-000000000002",
        "Snapper": "00000000-0000-0000-0000-000000000003",
        "Arcadians": "00000000-0000-0000-0000-000000000004",
        "Boffin": "00000000-0000-0000-0000-000000000005",
        "Planetoid": "00000000-0000-0000-0000-000000000006",
    ]

    private func repositoryRoot() -> URL {
        var root = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { root.deleteLastPathComponent() }
        return root
    }

    private var bundleMappingsDir: URL {
        repositoryRoot().appendingPathComponent(
            "clients/macos/Beebium/Beebium/Resources/KeyboardMappings")
    }

    private var corpusDir: URL {
        repositoryRoot().appendingPathComponent("tests/assets/keyboard-mappings")
    }

    private func loadJSON(_ url: URL) throws -> [String: Any] {
        let data = try Data(contentsOf: url)
        return try XCTUnwrap(try JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    func testBundleBuiltInsHaveRatifiedIdsAndAreVersion2() throws {
        for (name, id) in Self.expected {
            let url = bundleMappingsDir.appendingPathComponent("\(name).json")
            let json = try loadJSON(url)
            XCTAssertEqual(json["version"] as? Int, 2, "\(name) must be version 2")
            XCTAssertEqual(json["id"] as? String, id, "\(name) id")
            XCTAssertEqual(json["name"] as? String, name, "\(name) name")

            let mapping = KeyboardMapping(json: json, isBuiltIn: true)
            XCTAssertEqual(mapping.id.uuidString.lowercased(), id, "\(name) parsed id")
            XCTAssertFalse(mapping.fileNeedsRewrite, "\(name) is already current")
        }
    }

    func testBundleBuiltInsEqualTheSharedCorpus() throws {
        for name in Self.expected.keys {
            let bundle = try loadJSON(bundleMappingsDir.appendingPathComponent("\(name).json"))
            let corpus = try loadJSON(corpusDir.appendingPathComponent("\(name).json"))
            XCTAssertTrue(
                (bundle as NSDictionary).isEqual(to: corpus),
                "\(name) differs between the app bundle and the shared corpus; "
                + "the built-ins must be identical across clients")
        }
    }

    func testLogicalIsTheManagerDefaultId() {
        // The manager uses this id as the default/active mapping and the toggle
        // target; Logical must carry it.
        XCTAssertEqual(
            KeyboardMappingManager.defaultLogicalMappingID.uuidString.lowercased(),
            Self.expected["Logical"])
    }

    func testThereAreExactlySixBuiltInFiles() throws {
        let files = try FileManager.default.contentsOfDirectory(
            at: bundleMappingsDir, includingPropertiesForKeys: nil, options: .skipsHiddenFiles)
            .filter { $0.pathExtension == "json" }
        XCTAssertEqual(Set(files.map { $0.deletingPathExtension().lastPathComponent }),
                       Set(Self.expected.keys),
                       "the bundle ships exactly the six ratified built-ins")
    }
}
