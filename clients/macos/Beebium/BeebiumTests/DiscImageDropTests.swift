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

final class DiscImageTypesTests: XCTestCase {
    func testRecognisesSupportedExtensions() {
        for ext in ["ssd", "dsd", "adf", "adl", "adm", "ads", "hfe", "img"] {
            XCTAssertTrue(
                DiscImageTypes.isDiscImage(URL(fileURLWithPath: "/discs/elite.\(ext)")),
                "\(ext) should be recognised")
        }
    }

    func testExtensionMatchIsCaseInsensitive() {
        XCTAssertTrue(DiscImageTypes.isDiscImage(URL(fileURLWithPath: "/discs/ELITE.SSD")))
        XCTAssertTrue(DiscImageTypes.isDiscImage(URL(fileURLWithPath: "/discs/Elite.Ssd")))
    }

    func testRejectsOtherFiles() {
        XCTAssertFalse(DiscImageTypes.isDiscImage(URL(fileURLWithPath: "/notes.txt")))
        XCTAssertFalse(DiscImageTypes.isDiscImage(URL(fileURLWithPath: "/disc.ssd.zip")))
        XCTAssertFalse(DiscImageTypes.isDiscImage(URL(fileURLWithPath: "/discs")))
    }

    func testNoContentTypeIsAWildcard() {
        // Substituting a permissive stand-in for an extension that fails to
        // resolve would silently widen every file picker to all files.
        for type in DiscImageTypes.contentTypes {
            XCTAssertNotEqual(type, .data)
            XCTAssertNotEqual(type, .item)
            XCTAssertNotEqual(type, .content)
        }
    }
}

final class DiscDropRefusalTests: XCTestCase {
    private let occupied = "Eject disc first"

    func testAcceptsOneDiscImageOnAnEmptySlot() {
        XCTAssertNil(discDropRefusal(for: .one(isDiscImage: true),
                                     isSlotEmpty: true,
                                     occupiedReason: occupied))
    }

    func testRefusesAnOccupiedSlot() {
        XCTAssertEqual(discDropRefusal(for: .one(isDiscImage: true),
                                       isSlotEmpty: false,
                                       occupiedReason: occupied),
                       .slotOccupied(occupied))
    }

    func testRefusesSeveralFilesEvenOnAnEmptySlot() {
        XCTAssertEqual(discDropRefusal(for: .several,
                                       isSlotEmpty: true,
                                       occupiedReason: occupied),
                       .severalFiles)
    }

    func testRefusesAFileThatIsNotADiscImage() {
        XCTAssertEqual(discDropRefusal(for: .one(isDiscImage: false),
                                       isSlotEmpty: true,
                                       occupiedReason: occupied),
                       .notADiscImage)
    }

    func testUnsuitableFileIsReportedAheadOfAnOccupiedSlot() {
        // "Eject disc first" would imply that ejecting makes the drop work,
        // which is untrue of a file that was never a disc image.
        XCTAssertEqual(discDropRefusal(for: .one(isDiscImage: false),
                                       isSlotEmpty: false,
                                       occupiedReason: occupied),
                       .notADiscImage)
    }

    func testRefusesEverythingWhenTheServerIsElsewhere() {
        // A path is only meaningful to a process sharing this filesystem, so
        // a perfectly good disc image is still not droppable.
        XCTAssertEqual(discDropRefusal(for: .one(isDiscImage: true),
                                       isSlotEmpty: true,
                                       occupiedReason: occupied,
                                       isServerLocal: false),
                       .serverElsewhere)
    }

    func testRemoteServerIsReportedAheadOfEveryOtherReason() {
        // Nothing the user does to the file or the drive can make this work,
        // so saying anything else would send them off fixing the wrong thing.
        for candidate: DiscDropCandidate in [.none, .several,
                                             .one(isDiscImage: false),
                                             .one(isDiscImage: true)] {
            for isSlotEmpty in [true, false] {
                XCTAssertEqual(discDropRefusal(for: candidate,
                                               isSlotEmpty: isSlotEmpty,
                                               occupiedReason: occupied,
                                               isServerLocal: false),
                               .serverElsewhere)
            }
        }
    }

    func testRefusesADragCarryingNoFiles() {
        XCTAssertEqual(discDropRefusal(for: .none,
                                       isSlotEmpty: true,
                                       occupiedReason: occupied),
                       .notADiscImage)
    }

    func testEveryRefusalCarriesAReason() {
        for refusal: DiscDropRefusal in [.severalFiles, .notADiscImage,
                                         .slotOccupied(occupied),
                                         .serverElsewhere] {
            XCTAssertFalse(refusal.message.isEmpty)
        }
    }
}


final class HostFingerprintTests: XCTestCase {
    func testThisHostHasAFingerprint() {
        // macOS answers gethostuuid on an unsandboxed process, so a nil here
        // means the derivation broke, not that the platform declined.
        XCTAssertNotNil(HostFingerprint.current)
    }

    func testFingerprintIsASha256Digest() throws {
        let fingerprint = try XCTUnwrap(HostFingerprint.current)
        XCTAssertEqual(fingerprint.count, 64)
        XCTAssertTrue(fingerprint.allSatisfy { $0.isHexDigit && !$0.isUppercase })
    }

    func testFingerprintIsStable() {
        // Two processes on one host must agree, so it cannot vary per call.
        XCTAssertEqual(HostFingerprint.current, HostFingerprint.current)
    }

    func testThisHostMatchesItsOwnFingerprint() throws {
        let fingerprint = try XCTUnwrap(HostFingerprint.current)
        XCTAssertTrue(HostFingerprint.isThisHost(fingerprint))
    }

    func testAnotherHostDoesNotMatch() {
        XCTAssertFalse(HostFingerprint.isThisHost(String(repeating: "a", count: 64)))
    }

    func testUnknownIsNotAMatch() {
        // A server that will not say where it is must not be taken for a
        // local one: that would enable precisely the features that break.
        XCTAssertFalse(HostFingerprint.isThisHost(""))
    }
}
