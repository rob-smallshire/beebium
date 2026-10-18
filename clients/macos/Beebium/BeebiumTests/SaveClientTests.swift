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
import GRPC
@testable import Beebium

/// Mapping SaveMachine's gRPC statuses to a MachineSaveError, and the first-save
/// panel defaults. Both are pure.
final class SaveClientTests: XCTestCase {

    func testMapsInvalidArgumentToNoDestination() {
        XCTAssertEqual(SaveClient.saveError(from: GRPCStatus(code: .invalidArgument, message: "x")),
                       .noDestination)
    }

    func testMapsAlreadyExistsToDestinationExists() {
        XCTAssertEqual(SaveClient.saveError(from: GRPCStatus(code: .alreadyExists, message: nil)),
                       .destinationExists)
    }

    func testMapsFailedPreconditionToNotReplaceableKeepingTheReason() {
        let error = SaveClient.saveError(from: GRPCStatus(code: .failedPrecondition,
                                                          message: "a machine is running there"))
        XCTAssertEqual(error, .notReplaceable("a machine is running there"))
        XCTAssertEqual(error.errorDescription,
                       "That item cannot be replaced: a machine is running there.")
    }

    func testMapsInternalToFailed() {
        let error = SaveClient.saveError(from: GRPCStatus(code: .internalError, message: "disk full"))
        XCTAssertEqual(error, .failed("disk full"))
        XCTAssertEqual(error.errorDescription, "The machine could not be saved: disk full.")
    }

    func testMapsANonGRPCErrorToFailed() {
        struct SomeError: Error, LocalizedError { var errorDescription: String? { "boom" } }
        XCTAssertEqual(SaveClient.saveError(from: SomeError()), .failed("boom"))
    }

    // MARK: - Sentence composition

    func testSentenceReturnsTheLeadAloneWhenThereIsNoServerDetail() {
        XCTAssertEqual(SaveClient.sentence("That name can't be used.", detail: "   "),
                       "That name can't be used.")
    }

    func testSentenceJoinsLeadAndDetailAfterAColonWithOneFullStop() {
        // A lower-case server fragment, with or without its own full stop, must
        // not add a second one or lead the sentence.
        XCTAssertEqual(
            SaveClient.sentence("That location can't be used.", detail: "destination is inside the machine"),
            "That location can't be used: destination is inside the machine.")
        XCTAssertEqual(
            SaveClient.sentence("That location can't be used.", detail: "no such folder."),
            "That location can't be used: no such folder.")
    }

    func testFirstSaveDefaultsUseDocumentsAndTheMachineName() {
        let documents = URL(fileURLWithPath: "/Users/me/Documents")
        let defaults = SaveClient.firstSaveDefaults(machineName: "Elite Server", documentsDirpath: documents)
        XCTAssertEqual(defaults.directoryURL, documents)
        XCTAssertEqual(defaults.nameFieldStringValue, "Elite Server")
    }

    func testFirstSaveDefaultsFallBackWhenTheNameIsBlank() {
        let defaults = SaveClient.firstSaveDefaults(
            machineName: "   ", documentsDirpath: URL(fileURLWithPath: "/d"))
        XCTAssertEqual(defaults.nameFieldStringValue, "Beebium Machine")
    }
}
