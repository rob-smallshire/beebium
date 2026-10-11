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

/// The picker's accepted extensions come from the server's list-disc-formats
/// rather than a list in the client, filtered by kind (the floppy picker takes
/// floppy formats, the hard-disc picker hard-disc ones). These pin the parse of
/// that output, using exactly what the subcommand prints, so the picker filters
/// track the server.
final class DiscFormatListTests: XCTestCase {
    /// Verbatim `beebium-model-b --format jsonl list-disc-formats` output.
    private let realOutput = """
        {"name":"SSD/DSD (DFS)","kind":"floppy","description":"Acorn Disc Filing System sector image","extensions":[".ssd",".dsd"],"write_support":true}
        {"name":"ADFS (ADF/ADL/ADM/ADS)","kind":"floppy","description":"Advanced Disc Filing System sector image","extensions":[".adf",".adl",".adm",".ads"],"write_support":true}
        {"name":"HFE (HxC Floppy Emulator)","kind":"floppy","description":"HxC Floppy Emulator flux-level disc image","extensions":[".hfe"],"write_support":true}
        {"name":"Acorn SCSI","kind":"hard-disc","description":"Acorn SCSI hard disc image: the sectors in a .dat file, with the drive's geometry in a .dsc file beside it","extensions":[".dat"],"write_support":true}
        """

    private func extensions(ofKind kind: String, in output: String) -> [String] {
        PresetManager.parseDiscFormats(fromFormatList: output)
            .filter { $0.kind == kind }
            .flatMap { $0.extensions }
    }

    func testFloppyExtensionsExcludeHardDisc() {
        XCTAssertEqual(Set(extensions(ofKind: "floppy", in: realOutput)),
                       ["ssd", "dsd", "adf", "adl", "adm", "ads", "hfe"])
    }

    func testHardDiscExtensions() {
        XCTAssertEqual(extensions(ofKind: "hard-disc", in: realOutput), ["dat"])
    }

    func testStripsTheLeadingDot() {
        let exts = PresetManager.parseDiscFormats(fromFormatList: realOutput)
            .flatMap { $0.extensions }
        XCTAssertTrue(exts.allSatisfy { !$0.hasPrefix(".") })
    }

    func testLowercasesExtensions() {
        let exts = PresetManager.parseDiscFormats(
            fromFormatList: #"{"kind":"floppy","extensions":[".SSD",".Dsd"]}"#)
            .flatMap { $0.extensions }
        XCTAssertEqual(exts, ["ssd", "dsd"])
    }

    func testSkipsBlankAndMalformedLines() {
        let mixed = """

            {"kind":"floppy","extensions":[".ssd"]}
            not json at all
            {"name":"no extensions key"}
            {"kind":"floppy","extensions":[".hfe"]}
            """
        let exts = PresetManager.parseDiscFormats(fromFormatList: mixed).flatMap { $0.extensions }
        XCTAssertEqual(exts, ["ssd", "hfe"])
    }

    func testEmptyInputYieldsNothing() {
        XCTAssertTrue(PresetManager.parseDiscFormats(fromFormatList: "").isEmpty)
    }
}
