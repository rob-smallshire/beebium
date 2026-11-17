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

/// Tests for CassetteClient's snapshot-replacing status stream and its refusal
/// of a key when there is no connection. Modelled on EconetClientTests: the
/// stream handlers are internal, so a test feeds them real DeckStatus values
/// and asserts on the published state, with no gRPC server.
@MainActor
final class CassetteClientTests: XCTestCase {

    private func status(transport: Beebium_TapeTransport = .playing,
                        position: Double = 12.5,
                        motorOn: Bool = false,
                        recordable: Bool = true,
                        inserted: Bool = true) -> Beebium_DeckStatus {
        var status = Beebium_DeckStatus()
        status.hasCassetteInterface_p = true
        status.tapeInserted = inserted
        if inserted {
            var tape = Beebium_TapeMetadata()
            tape.name = "Elite.uef"
            tape.format = "UEF"
            tape.durationSeconds = 300
            tape.recordable = recordable
            status.tape = tape
        }
        status.transport = transport
        status.positionSeconds = position
        status.motorOn = motorOn
        status.ulaPart = "ferranti"
        return status
    }

    /// A client that has received one snapshot on its current stream.
    private func loadedClient() -> CassetteClient {
        let client = CassetteClient()
        client.handleDeckStatus(status(), generation: client.streamGeneration)
        return client
    }

    func testSnapshotIsPublishedFresh() {
        let client = loadedClient()
        XCTAssertTrue(client.isLoaded)
        XCTAssertFalse(client.isStale)
        XCTAssertTrue(client.hasCassetteInterface)
        XCTAssertTrue(client.tapeInserted)
        XCTAssertEqual(client.tape?.name, "Elite.uef")
        XCTAssertEqual(client.transport, .playing)
        XCTAssertEqual(client.positionSeconds, 12.5)
        XCTAssertFalse(client.motorOn)
        XCTAssertNil(client.errorMessage)
    }

    func testEmptyDeckClearsTheTape() {
        let client = loadedClient()
        client.handleDeckStatus(status(inserted: false), generation: client.streamGeneration)
        XCTAssertFalse(client.tapeInserted)
        XCTAssertNil(client.tape, "an empty deck must not keep the ejected tape's metadata")
    }

    func testLostStreamMarksStaleAndStillsTheTransport() {
        let client = loadedClient()
        client.handleDeckStatus(status(transport: .playing, motorOn: true),
                                generation: client.streamGeneration)

        client.handleStreamEnd(.lost("unavailable (14)"), generation: client.streamGeneration)

        XCTAssertTrue(client.isStale)
        XCTAssertFalse(client.motorOn, "a lost stream must never read as a live, moving deck")
        XCTAssertEqual(client.transport, .stopped)
        XCTAssertNotNil(client.errorMessage)
        // The tape is kept as last-known for display.
        XCTAssertTrue(client.isLoaded)
        XCTAssertTrue(client.tapeInserted)
    }

    func testFinishedStreamLeavesStatusAlone() {
        let client = loadedClient()
        client.handleStreamEnd(.finished, generation: client.streamGeneration)
        XCTAssertFalse(client.isStale)
        XCTAssertEqual(client.transport, .playing)
        XCTAssertNil(client.errorMessage)
    }

    func testSnapshotOnAFreshStreamRecoversFromStale() {
        let client = loadedClient()
        client.handleStreamEnd(.lost("unavailable (14)"), generation: client.streamGeneration)

        client.handleDeckStatus(status(transport: .stopped, position: 0),
                                generation: client.streamGeneration)

        XCTAssertFalse(client.isStale)
        XCTAssertEqual(client.transport, .stopped)
        XCTAssertEqual(client.positionSeconds, 0)
        XCTAssertNil(client.errorMessage)
    }

    func testDisconnectClearsStaleAndCachedStatus() {
        let client = loadedClient()
        client.handleStreamEnd(.lost("unavailable (14)"), generation: client.streamGeneration)

        client.disconnect()

        XCTAssertFalse(client.isStale)
        XCTAssertFalse(client.isLoaded)
        XCTAssertFalse(client.hasCassetteInterface)
        XCTAssertFalse(client.tapeInserted)
        XCTAssertNil(client.tape)
        XCTAssertEqual(client.positionSeconds, 0)
        XCTAssertNil(client.errorMessage)
    }

    func testLossOfASupersededStreamIsIgnored() {
        let client = loadedClient()
        let oldGeneration = client.streamGeneration

        client.disconnect()   // reconnect cascade: abandons the old stream
        XCTAssertNotEqual(client.streamGeneration, oldGeneration)
        client.handleDeckStatus(status(), generation: client.streamGeneration)

        client.handleStreamEnd(.lost("unavailable (14)"), generation: oldGeneration)

        XCTAssertFalse(client.isStale)
        XCTAssertNil(client.errorMessage)
    }

    func testSnapshotFromASupersededStreamIsIgnored() {
        let client = loadedClient()
        let oldGeneration = client.streamGeneration
        client.disconnect()

        client.handleDeckStatus(status(), generation: oldGeneration)

        XCTAssertFalse(client.isLoaded)
        XCTAssertFalse(client.tapeInserted)
    }

    func testDeckKeyIsRefusedWithoutAConnection() async {
        let client = CassetteClient()
        guard case .failure(.notConnected) = await client.play() else {
            return XCTFail("expected notConnected from play()")
        }
        guard case .failure(.notConnected) = await client.seek(positionSeconds: 10) else {
            return XCTFail("expected notConnected from seek()")
        }
        guard case .failure(.notConnected) = await client.insertTape(
            url: URL(fileURLWithPath: "/tapes/x.uef")) else {
            return XCTFail("expected notConnected from insertTape()")
        }
    }
}
