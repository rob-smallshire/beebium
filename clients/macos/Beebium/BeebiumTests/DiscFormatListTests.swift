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

/// The picker's accepted extensions come from the server's list-floppy-formats
/// rather than a list in the client. These pin the parse of that output, using
/// exactly what the subcommand prints, so the picker filter tracks the server.
final class DiscFormatListTests: XCTestCase {
    /// Verbatim `beebium-model-b --format jsonl list-floppy-formats` output.
    private let realOutput = """
        {"name":"SSD/DSD (DFS)","description":"Acorn Disc Filing System sector image","extensions":[".ssd",".dsd"],"write_support":true}
        {"name":"ADFS (ADF/ADL/ADM/ADS)","description":"Advanced Disc Filing System sector image","extensions":[".adf",".adl",".adm",".ads"],"write_support":true}
        {"name":"HFE (HxC Floppy Emulator)","description":"HxC Floppy Emulator flux-level disc image","extensions":[".hfe"],"write_support":true}
        """

    func testExtractsEveryFormatsExtensions() {
        let exts = Set(PresetManager.parseDiscImageExtensions(fromFormatList: realOutput))
        XCTAssertEqual(exts, ["ssd", "dsd", "adf", "adl", "adm", "ads", "hfe"])
    }

    func testStripsTheLeadingDot() {
        let exts = PresetManager.parseDiscImageExtensions(fromFormatList: realOutput)
        XCTAssertTrue(exts.allSatisfy { !$0.hasPrefix(".") })
    }

    func testLowercasesExtensions() {
        let exts = PresetManager.parseDiscImageExtensions(
            fromFormatList: #"{"extensions":[".SSD",".Dsd"]}"#)
        XCTAssertEqual(exts, ["ssd", "dsd"])
    }

    func testSkipsBlankAndMalformedLines() {
        let mixed = """

            {"extensions":[".ssd"]}
            not json at all
            {"name":"no extensions key"}
            {"extensions":[".hfe"]}
            """
        let exts = PresetManager.parseDiscImageExtensions(fromFormatList: mixed)
        XCTAssertEqual(exts, ["ssd", "hfe"])
    }

    func testEmptyInputYieldsNothing() {
        XCTAssertTrue(PresetManager.parseDiscImageExtensions(fromFormatList: "").isEmpty)
    }
}
