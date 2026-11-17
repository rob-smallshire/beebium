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

/// Tests for the pure mapping from a CreateTape failure kind to the deck's
/// short inline marker and to the lead of the user-facing sentence, mirroring
/// the disc-creation mapping.
final class CreateTapeErrorKindLabelTests: XCTestCase {
    func testClassifiedKindsHaveMarkerLabels() {
        XCTAssertEqual(Beebium_CreateTapeErrorKind.unknownLabel.markerLabel, "Unknown template")
        XCTAssertEqual(Beebium_CreateTapeErrorKind.destinationExists.markerLabel, "File exists")
        XCTAssertEqual(Beebium_CreateTapeErrorKind.writeFailed.markerLabel, "Couldn't write tape")
        XCTAssertEqual(Beebium_CreateTapeErrorKind.wrongExtension.markerLabel, "Wrong extension")
    }

    func testUnclassifiedKindsHaveNoMarkerLabel() {
        // nil lets the caller keep its own generic ("Couldn't create tape").
        XCTAssertNil(Beebium_CreateTapeErrorKind.unspecified.markerLabel)
        XCTAssertNil(Beebium_CreateTapeErrorKind.UNRECOGNIZED(99).markerLabel)
    }

    func testSentenceLeadsAreSentences() {
        for kind: Beebium_CreateTapeErrorKind in
            [.unknownLabel, .destinationExists, .writeFailed, .wrongExtension,
             .unspecified, .UNRECOGNIZED(99)] {
            let lead = kind.sentenceLead
            XCTAssertFalse(lead.isEmpty)
            XCTAssertEqual(lead.first, lead.first?.uppercased().first)
            XCTAssertTrue(lead.hasSuffix("."))
        }
    }

    func testWrongExtensionSentenceCompletesTheServerFragment() {
        let sentence = SaveClient.sentence(
            Beebium_CreateTapeErrorKind.wrongExtension.sentenceLead,
            detail: "destination must end in .uef: /tmp/blank.csw")
        XCTAssertEqual(
            sentence,
            "That name must end in .uef for a tape: destination must end in .uef: /tmp/blank.csw.")
    }
}

/// The blank-tape save name keeps the template's .uef extension, reusing the
/// same naming rule the blank-disc flow uses (the server detects the tape's
/// format from the extension and refuses a wrong one).
final class BlankTapeNamingTests: XCTestCase {
    func testAppendsUefWhenMissing() {
        let chosen = URL(fileURLWithPath: "/tapes/PROG")
        let url = BlankDiscNaming.url(forChosen: chosen,
                                      templateFilename: "Blank C15 (15 minutes).uef")
        XCTAssertEqual(url.pathExtension, "uef")
    }

    func testKeepsUefWhenAlreadyPresent() {
        let chosen = URL(fileURLWithPath: "/tapes/PROG.uef")
        let url = BlankDiscNaming.url(forChosen: chosen,
                                      templateFilename: "Blank C15 (15 minutes).uef")
        XCTAssertEqual(url.lastPathComponent, "PROG.uef")
    }
}

@MainActor
final class CassetteCreateTapeDispatchTests: XCTestCase {
    func testCreateTapeIsRefusedWithoutAConnection() async {
        let client = CassetteClient()
        guard case .failure(.notConnected) = await client.createTape(
            label: "Blank C15 (15 minutes)", destinationPath: "/tapes/x.uef", overwrite: true) else {
            return XCTFail("expected notConnected from createTape()")
        }
    }
}
