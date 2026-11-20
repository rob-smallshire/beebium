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

/// The spike shim's client-side facet derivation, over fixtures that mirror the
/// bundled presets, so we can read what it decided (and the server can later be
/// checked against it). Each case asserts the full, sorted tag list. The built-in
/// disc controller is passed in, standing for the model's storage schema
/// (`builtin.fdc.device`), so the Master and B+ cases are pinned by the schema
/// value rather than a model-id switch.
final class PresetTaggingTests: XCTestCase {

    private func tags(_ json: String, builtinFdcDevice: String? = nil) -> [PresetTag] {
        let object = try! JSONSerialization.jsonObject(with: Data(json.utf8)) as! [String: Any]
        return PresetTagger.tags(model: nil, preset: object, builtinFdcDevice: builtinFdcDevice)
    }

    private func t(_ group: PresetTagGroup, _ value: String) -> PresetTag {
        PresetTag(group: group, value: value)
    }

    func testPlainModelBHasNoDiscAndNoStorage() {
        // A Model B with no disc controller fitted: no disc, and no storage media.
        XCTAssertEqual(tags(#"{"model":"model-b","name":"Model B"}"#),
                       [t(.model, "Model B"), t(.discController, "No disc")])
    }

    func testModelBWith1770DiscHasFloppy() {
        let json = #"""
        {"model":"model-b","name":"Model B (Disc)",
         "storage":{"fdc_socket":{"id":"acorn-1770"}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.model, "Model B"), t(.discController, "1770"), t(.storage, "Floppy")])
    }

    func testModelB8271Disc() {
        let json = #"""
        {"model":"model-b","name":"Model B (8271)",
         "storage":{"fdc_socket":{"id":"acorn-8271"}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.model, "Model B"), t(.discController, "8271"), t(.storage, "Floppy")])
    }

    func testFdcSocketNoneCountsAsNoDisc() {
        let json = #"""
        {"model":"model-b","name":"No controller",
         "storage":{"fdc_socket":{"id":"none"}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.model, "Model B"), t(.discController, "No disc")])
    }

    func testMasterTakesItsBuiltinController() {
        // The Master has no socket; its schema carries builtin.fdc = WD1770, so a
        // Master preset with no storage block is still 1770, with a floppy.
        XCTAssertEqual(tags(#"{"model":"master-128","name":"Master 128"}"#, builtinFdcDevice: "WD1770"),
                       [t(.model, "Master 128"), t(.discController, "1770"), t(.storage, "Floppy")])
    }

    func testBPlusTakesItsBuiltinControllerAndMusic5000() {
        let json = #"""
        {"model":"model-b-plus-128k","name":"Music 5000",
         "storage":{"floppy_drives":[{"drive":0,"image_uri":"x.ssd"}]},
         "extensions":[{"name":"hybrid-music-5000"}]}
        """#
        XCTAssertEqual(tags(json, builtinFdcDevice: "WD1770"), [
            t(.model, "Model B+"),
            t(.discController, "1770"),
            t(.storage, "Floppy"),
            t(.peripherals, "Music 5000"),
        ])
    }

    func testBPlusWith65C102Copro() {
        let json = #"""
        {"model":"model-b-plus-128k","name":"B+ 65C102",
         "extensions":[{"name":"acorn-65c102-coprocessor"}]}
        """#
        XCTAssertEqual(tags(json, builtinFdcDevice: "WD1770"), [
            t(.model, "Model B+"),
            t(.coprocessor, "65C102"),
            t(.discController, "1770"),
            t(.storage, "Floppy"),
        ])
    }

    func testEconetAunStation() {
        let json = #"""
        {"model":"model-b","name":"Station 80",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":80,"transport":{"name":"aun","parameters":{"port":"0"}}}}
        """#
        XCTAssertEqual(tags(json), [
            t(.model, "Model B"),
            t(.discController, "1770"),
            t(.network, "AUN"),
            t(.storage, "Floppy"),
        ])
    }

    func testFileServerSpansEveryFacet() {
        // The L3 file server: Model B, 65C02 copro, 1770 over AUN, a floppy and a
        // SCSI hard disc, with an RTC and SCSI adapter as peripherals. Its SCSI
        // host is a peripheral; its SCSI disc is Storage/Hard disc; neither is a
        // network value.
        let json = #"""
        {"model":"model-b","name":"L3 File Server",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":254,"transport":{"name":"aun","parameters":{"port":"0"}}},
         "extensions":[{"name":"acorn-rtc"},{"name":"acorn-scsi"},
                       {"name":"scsi-hard-disc"},{"name":"acorn-65c02-coprocessor"}]}
        """#
        XCTAssertEqual(tags(json), [
            t(.model, "Model B"),
            t(.coprocessor, "65C02"),
            t(.discController, "1770"),
            t(.network, "AUN"),
            t(.storage, "Floppy"),
            t(.storage, "Hard disc"),
            t(.peripherals, "Real-time clock"),
            t(.peripherals, "SCSI adapter"),
        ])
    }

    func testPiconetIsANetworkValue() {
        let json = #"""
        {"model":"model-b","name":"Station 81",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":81,"transport":{"name":"piconet"}}}
        """#
        XCTAssertEqual(tags(json), [
            t(.model, "Model B"),
            t(.discController, "1770"),
            t(.network, "Piconet"),
            t(.storage, "Floppy"),
        ])
    }

    /// An unknown peripheral extension extends the facet by its raw name.
    func testUnknownPeripheralExtensionShowsByName() {
        let json = #"""
        {"model":"model-b","name":"Gadget",
         "extensions":[{"name":"some-new-gadget"}]}
        """#
        XCTAssertEqual(tags(json), [
            t(.model, "Model B"),
            t(.discController, "No disc"),
            t(.peripherals, "some-new-gadget"),
        ])
    }

    /// The row's model wins over the file's, so a row that names its model still
    /// classifies when the file is terse.
    func testRowModelOverridesFileModel() {
        let object = try! JSONSerialization.jsonObject(
            with: Data(#"{"model":"model-b","name":"X"}"#.utf8)) as! [String: Any]
        let tags = PresetTagger.tags(model: "master-128", preset: object, builtinFdcDevice: "WD1770")
        XCTAssertEqual(tags, [t(.model, "Master 128"), t(.discController, "1770"), t(.storage, "Floppy")])
    }
}
