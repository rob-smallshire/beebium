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
/// be checked against it). Each case asserts the full, sorted tag list. The
/// built-in disc controller is passed in, standing for the model's storage
/// schema (`builtin.fdc.device`), so the Master and B+ cases are pinned by the
/// schema value rather than a model-id switch.
final class PresetTaggingTests: XCTestCase {

    private func tags(_ json: String, builtinFdcDevice: String? = nil) -> [PresetTag] {
        let object = try! JSONSerialization.jsonObject(with: Data(json.utf8)) as! [String: Any]
        return PresetTagger.tags(model: nil, preset: object, builtinFdcDevice: builtinFdcDevice)
    }

    private func t(_ group: PresetTagGroup, _ value: String) -> PresetTag {
        PresetTag(group: group, value: value)
    }

    func testPlainModelBHasNoDisc() {
        // A Model B has an FDC socket, not a built-in controller, so with nothing
        // fitted it is no disc.
        XCTAssertEqual(tags(#"{"model":"model-b","name":"Model B"}"#),
                       [t(.model, "Model B"), t(.storage, "No disc")])
    }

    func testModelBWith1770Disc() {
        let json = #"""
        {"model":"model-b","name":"Model B (Disc)",
         "storage":{"fdc_socket":{"id":"acorn-1770"}}}
        """#
        XCTAssertEqual(tags(json), [t(.model, "Model B"), t(.storage, "1770")])
    }

    func testModelB8271Disc() {
        let json = #"""
        {"model":"model-b","name":"Model B (8271)",
         "storage":{"fdc_socket":{"id":"acorn-8271"}}}
        """#
        XCTAssertEqual(tags(json), [t(.model, "Model B"), t(.storage, "8271")])
    }

    func testFdcSocketNoneCountsAsNoDisc() {
        // A disc socket fitted with no controller is, to someone picking a
        // machine, no disc -- not a mysterious "none".
        let json = #"""
        {"model":"model-b","name":"No controller",
         "storage":{"fdc_socket":{"id":"none"}}}
        """#
        XCTAssertEqual(tags(json), [t(.model, "Model B"), t(.storage, "No disc")])
    }

    func testMasterTakesItsBuiltinController() {
        // The Master has no socket; its schema carries builtin.fdc = WD1770, so a
        // Master preset with no storage block is still 1770.
        XCTAssertEqual(tags(#"{"model":"master-128","name":"Master 128"}"#, builtinFdcDevice: "WD1770"),
                       [t(.model, "Master 128"), t(.storage, "1770")])
    }

    func testBPlusTakesItsBuiltinControllerWhenNoSocketFitted() {
        // The B+ family also has a built-in WD1770; a floppy-only preset (no
        // fdc_socket) still reads 1770 from the schema, not a hard-coded default.
        let json = #"""
        {"model":"model-b-plus-128k","name":"Music 5000",
         "storage":{"floppy_drives":[{"drive":0,"image_uri":"x.ssd"}]},
         "extensions":[{"name":"hybrid-music-5000"}]}
        """#
        XCTAssertEqual(tags(json, builtinFdcDevice: "WD1770"),
                       [t(.model, "Model B+"), t(.storage, "1770"), t(.peripherals, "Music 5000")])
    }

    func testModelBPlus128KWith65C102Copro() {
        // No storage and no built-in controller supplied: no disc.
        let json = #"""
        {"model":"model-b-plus-128k","name":"B+ 65C102",
         "extensions":[{"name":"acorn-65c102-coprocessor"}]}
        """#
        XCTAssertEqual(tags(json, builtinFdcDevice: "WD1770"),
                       [t(.model, "Model B+"), t(.storage, "1770"), t(.processor, "65C102")])
    }

    func testEconetAunStation() {
        let json = #"""
        {"model":"model-b","name":"Station 80",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":80,"transport":{"name":"aun","parameters":{"port":"0"}}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.model, "Model B"), t(.storage, "1770"), t(.network, "AUN")])
    }

    func testFileServerIsTaggedByTransportNotAsFileServer() {
        // The L3 file server runs over AUN with a 65C02 copro. Its SCSI host, SCSI
        // disc and RTC are plumbing, not peripherals or a network value of their
        // own, so the only network tag is the transport.
        let json = #"""
        {"model":"model-b","name":"L3 File Server",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":254,"transport":{"name":"aun","parameters":{"port":"0"}}},
         "extensions":[{"name":"acorn-rtc"},{"name":"acorn-scsi"},
                       {"name":"scsi-hard-disc"},{"name":"acorn-65c02-coprocessor"}]}
        """#
        XCTAssertEqual(tags(json), [
            t(.model, "Model B"),
            t(.storage, "1770"),
            t(.processor, "65C02"),
            t(.network, "AUN"),
        ])
    }

    func testPiconetIsANetworkValue() {
        let json = #"""
        {"model":"model-b","name":"Station 81",
         "storage":{"fdc_socket":{"id":"acorn-1770"}},
         "econet":{"station":81,"transport":{"name":"piconet"}}}
        """#
        XCTAssertEqual(tags(json),
                       [t(.model, "Model B"), t(.storage, "1770"), t(.network, "Piconet")])
    }

    /// An unknown peripheral extension extends the facet by its raw name.
    func testUnknownPeripheralExtensionShowsByName() {
        let json = #"""
        {"model":"model-b","name":"Gadget",
         "extensions":[{"name":"some-new-gadget"}]}
        """#
        XCTAssertEqual(tags(json),
                       [t(.model, "Model B"), t(.storage, "No disc"), t(.peripherals, "some-new-gadget")])
    }

    /// The row's model wins over the file's, so a row that names its model still
    /// classifies when the file is terse.
    func testRowModelOverridesFileModel() {
        let object = try! JSONSerialization.jsonObject(
            with: Data(#"{"model":"model-b","name":"X"}"#.utf8)) as! [String: Any]
        let tags = PresetTagger.tags(model: "master-128", preset: object, builtinFdcDevice: "WD1770")
        XCTAssertEqual(tags, [t(.model, "Master 128"), t(.storage, "1770")])
    }
}
