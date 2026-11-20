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

/// The spike's client-side preset tagging, over fixtures that mirror the bundled
/// presets, so we can read what the derivation decided (and the server can later
/// be checked against it). Each case asserts the full, sorted tag list.
final class PresetTaggingTests: XCTestCase {

    private func tags(_ json: String) -> [PresetTag] {
        let object = try! JSONSerialization.jsonObject(with: Data(json.utf8)) as! [String: Any]
        return PresetTagger.tags(model: nil, preset: object)
    }

    private func t(_ group: PresetTagGroup, _ value: String) -> PresetTag {
        PresetTag(group: group, value: value)
    }

    func testPlainModelBHasFamilyAndNoDisc() {
        XCTAssertEqual(tags(#"{"model":"model-b","name":"Model B"}"#),
                       [t(.family, "Model B"), t(.disc, "No disc")])
    }

    func testModelBWith1770Disc() {
        let json = #"""
        {"model":"model-b","name":"Model B (Disc)",
         "storage":{"fdc_socket":{"id":"acorn-1770"}}}
        """#
        XCTAssertEqual(tags(json), [t(.family, "Model B"), t(.disc, "1770")])
    }

    func testModelB8271Disc() {
        let json = #"""
        {"model":"model-b","name":"Model B (8271)",
         "storage":{"fdc_socket":{"id":"acorn-8271"}}}
        """#
        XCTAssertEqual(tags(json), [t(.family, "Model B"), t(.disc, "8271")])
    }

    func testMaster128() {
        XCTAssertEqual(tags(#"{"model":"master-128","name":"Master 128"}"#),
                       [t(.family, "Master 128"), t(.disc, "No disc")])
    }

    func testModelBPlus128KWith65C102Copro() {
        let json = #"""
        {"model":"model-b-plus-128k","name":"B+ 65C102",
         "extensions":[{"name":"acorn-65c102-coprocessor"}]}
        """#
        XCTAssertEqual(tags(json),
                       [t(.family, "Model B+"), t(.coprocessor, "65C102"), t(.disc, "No disc")])
    }

    func testIntegraBBoard() {
        let json = #"""
        {"model":"model-b-integra-b","name":"Integra-B",
         "storage":{"fdc_socket":{"id":"acorn-1770"}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.family, "Model B"), t(.board, "Integra-B"), t(.disc, "1770")])
    }

    func testAtplSidewiseBoard() {
        let json = #"""
        {"model":"model-b-atpl-sidewise","name":"ATPL",
         "storage":{"fdc_socket":{"id":"acorn-1770"}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.family, "Model B"), t(.board, "ATPL Sidewise"), t(.disc, "1770")])
    }

    func testEconetAunStation() {
        let json = #"""
        {"model":"model-b","name":"Station 80",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":80,"transport":{"name":"aun","parameters":{"port":"0"}}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.family, "Model B"), t(.econet, "AUN"), t(.disc, "1770")])
    }

    func testMusic5000TakesModelDefaultDisc() {
        // Storage present (floppy drives) but no explicit FDC: the B+ built-in is
        // the 1770.
        let json = #"""
        {"model":"model-b-plus-128k","name":"Music 5000",
         "storage":{"floppy_drives":[{"drive":0,"image_uri":"x.ssd"}]},
         "extensions":[{"name":"hybrid-music-5000"}]}
        """#
        XCTAssertEqual(tags(json),
                       [t(.family, "Model B+"), t(.music, "Music 5000"), t(.disc, "1770")])
    }

    func testFileServerHasCoproAunAndFileServer() {
        // The L3 file server: SCSI host (File server), AUN, a 65C02 copro, 1770.
        let json = #"""
        {"model":"model-b","name":"L3 File Server",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":254,"transport":{"name":"aun","parameters":{"port":"0"}}},
         "extensions":[{"name":"acorn-rtc"},{"name":"acorn-scsi"},
                       {"name":"scsi-hard-disc"},{"name":"acorn-65c02-coprocessor"}]}
        """#
        XCTAssertEqual(tags(json), [
            t(.family, "Model B"),
            t(.coprocessor, "65C02"),
            t(.econet, "AUN"),
            t(.econet, "File server"),
            t(.disc, "1770"),
        ])
    }

    /// The row's model wins over the file's, so a row that names its model still
    /// classifies when the file is terse.
    func testRowModelOverridesFileModel() {
        let object = try! JSONSerialization.jsonObject(
            with: Data(#"{"model":"model-b","name":"X"}"#.utf8)) as! [String: Any]
        let tags = PresetTagger.tags(model: "master-128", preset: object)
        XCTAssertEqual(tags, [t(.family, "Master 128"), t(.disc, "No disc")])
    }
}
