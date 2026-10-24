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

/// Tests for the pure mapping from a CreateDisc failure kind to the row's
/// short inline marker and to the lead of the user-facing sentence.
final class CreateDiscErrorKindLabelTests: XCTestCase {
    func testClassifiedKindsHaveMarkerLabels() {
        XCTAssertEqual(Beebium_CreateDiscErrorKind.unknownLabel.markerLabel, "Unknown template")
        XCTAssertEqual(Beebium_CreateDiscErrorKind.destinationExists.markerLabel, "File exists")
        XCTAssertEqual(Beebium_CreateDiscErrorKind.writeFailed.markerLabel, "Couldn't write disc")
        XCTAssertEqual(Beebium_CreateDiscErrorKind.wrongExtension.markerLabel, "Wrong extension")
    }

    func testUnclassifiedKindsHaveNoMarkerLabel() {
        // nil lets the caller keep its own generic ("Couldn't create disc").
        XCTAssertNil(Beebium_CreateDiscErrorKind.unspecified.markerLabel)
        XCTAssertNil(Beebium_CreateDiscErrorKind.UNRECOGNIZED(99).markerLabel)
    }

    func testSentenceLeadsAreSentences() {
        // Every kind, classified or not, yields a capitalised sentence so the
        // popover always reads as prose even for a kind we do not yet map.
        for kind: Beebium_CreateDiscErrorKind in
            [.unknownLabel, .destinationExists, .writeFailed, .wrongExtension,
             .unspecified, .UNRECOGNIZED(99)] {
            let lead = kind.sentenceLead
            XCTAssertFalse(lead.isEmpty)
            XCTAssertEqual(lead.first, lead.first?.uppercased().first)
            XCTAssertTrue(lead.hasSuffix("."))
        }
    }

    func testSentenceCompletesTheServerFragment() {
        // The lead and the server's lower-case fragment compose into one
        // sentence, matching the save and move alerts.
        let sentence = SaveClient.sentence(
            Beebium_CreateDiscErrorKind.destinationExists.sentenceLead,
            detail: "destination already exists: /tmp/a.ssd")
        XCTAssertEqual(
            sentence,
            "A file already exists there: destination already exists: /tmp/a.ssd.")
    }

    func testWrongExtensionSentenceCompletesTheServerFragment() {
        let sentence = SaveClient.sentence(
            Beebium_CreateDiscErrorKind.wrongExtension.sentenceLead,
            detail: "destination must end in .dsd: /tmp/blank.ssd")
        XCTAssertEqual(
            sentence,
            "That name has the wrong file extension for this disc: destination must end in .dsd: /tmp/blank.ssd.")
    }
}
