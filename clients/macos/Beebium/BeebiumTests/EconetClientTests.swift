// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

@MainActor
final class EconetClientTests: XCTestCase {

    private func snapshot(station: UInt32 = 80, connected: Bool = true) -> Beebium_GetEconetStatusResponse {
        var response = Beebium_GetEconetStatusResponse()
        response.hasEconetSocket_p = true
        response.enabled = true
        response.stationID = station
        response.aunMode = true
        response.connected = connected
        response.gatedBySpeed = true
        response.aunStationCollisionCount = 2
        response.aunLastStationCollision = "station 80 claimed twice"
        return response
    }

    /// A client that has received one snapshot on its current stream.
    private func loadedClient() -> EconetClient {
        let client = EconetClient()
        client.handleStatusUpdate(snapshot(), generation: client.streamGeneration)
        return client
    }

    func testSnapshotIsPublishedFresh() {
        let client = loadedClient()
        XCTAssertTrue(client.isLoaded)
        XCTAssertFalse(client.isStale)
        XCTAssertTrue(client.connected)
        XCTAssertEqual(client.stationId, 80)
        XCTAssertNil(client.errorMessage)
    }

    func testLostStreamMarksStatusStaleAndClearsLiveGauges() {
        let client = loadedClient()

        client.handleStreamEnd(.lost("unavailable (14)"), generation: client.streamGeneration)

        XCTAssertTrue(client.isStale)
        XCTAssertFalse(client.connected, "a lost stream must never read as a live link")
        XCTAssertFalse(client.gatedBySpeed)
        XCTAssertEqual(client.stationCollisionCount, 0)
        XCTAssertEqual(client.lastStationCollision, "")
        XCTAssertNotNil(client.errorMessage)
        // Configuration is kept as last-known for display.
        XCTAssertTrue(client.isLoaded)
        XCTAssertTrue(client.enabled)
        XCTAssertEqual(client.stationId, 80)
    }

    func testFinishedStreamLeavesStatusAlone() {
        let client = loadedClient()

        client.handleStreamEnd(.finished, generation: client.streamGeneration)

        XCTAssertFalse(client.isStale)
        XCTAssertTrue(client.connected)
        XCTAssertNil(client.errorMessage)
    }

    func testSnapshotOnAFreshStreamRecoversFromStale() {
        let client = loadedClient()
        client.handleStreamEnd(.lost("unavailable (14)"), generation: client.streamGeneration)

        client.handleStatusUpdate(snapshot(station: 81), generation: client.streamGeneration)

        XCTAssertFalse(client.isStale)
        XCTAssertTrue(client.connected)
        XCTAssertEqual(client.stationId, 81)
        XCTAssertNil(client.errorMessage)
    }

    func testDisconnectClearsStaleAndCachedStatus() {
        let client = loadedClient()
        client.handleStreamEnd(.lost("unavailable (14)"), generation: client.streamGeneration)

        client.disconnect()

        XCTAssertFalse(client.isStale)
        XCTAssertFalse(client.isLoaded)
        XCTAssertFalse(client.connected)
        XCTAssertEqual(client.stationId, 0)
        XCTAssertNil(client.errorMessage)
    }

    /// The wake-from-sleep reconnect tears the client down and starts a new
    /// stream; the old stream's loss can be delivered afterwards and must not
    /// poison the new connection.
    func testLossOfASupersededStreamIsIgnored() {
        let client = loadedClient()
        let oldGeneration = client.streamGeneration

        client.disconnect()   // reconnect cascade: abandons the old stream
        XCTAssertNotEqual(client.streamGeneration, oldGeneration)
        client.handleStatusUpdate(snapshot(), generation: client.streamGeneration)

        client.handleStreamEnd(.lost("unavailable (14)"), generation: oldGeneration)

        XCTAssertFalse(client.isStale)
        XCTAssertTrue(client.connected)
        XCTAssertNil(client.errorMessage)
    }

    func testSnapshotFromASupersededStreamIsIgnored() {
        let client = loadedClient()
        let oldGeneration = client.streamGeneration
        client.disconnect()

        client.handleStatusUpdate(snapshot(), generation: oldGeneration)

        XCTAssertFalse(client.isLoaded)
        XCTAssertFalse(client.connected)
    }

    func testSetStationIdIsRefusedWithoutAConnection() async {
        let client = EconetClient()
        let result = await client.setStationId(81)
        guard case .failure(.notConnected) = result else {
            return XCTFail("expected notConnected, got \(result)")
        }
    }
}

@MainActor
final class EconetTransportsClientTests: XCTestCase {

    private struct ListFailure: Error {}

    private let aun = EconetTransportInfo(id: "aun-0", name: "AUN", description: "",
                                          active: true, hasUI: true)

    func testFirstFailureIsNotAnEmptyList() {
        let client = EconetTransportsClient()

        client.applyListResult(.failure(ListFailure()))

        XCTAssertTrue(client.isLoaded)
        XCTAssertFalse(client.hasListed, "a failed first attempt has listed nothing")
        XCTAssertTrue(client.transports.isEmpty)
        XCTAssertNotNil(client.errorMessage)
    }

    func testSuccessRecordsTheList() {
        let client = EconetTransportsClient()

        client.applyListResult(.success([aun]))

        XCTAssertTrue(client.isLoaded)
        XCTAssertTrue(client.hasListed)
        XCTAssertEqual(client.transports, [aun])
        XCTAssertNil(client.errorMessage)
    }

    func testSuccessWithNoTransportsStillCountsAsListed() {
        let client = EconetTransportsClient()

        client.applyListResult(.success([]))

        XCTAssertTrue(client.hasListed)
        XCTAssertTrue(client.transports.isEmpty)
    }

    func testFailureAfterSuccessKeepsTheLastList() {
        let client = EconetTransportsClient()
        client.applyListResult(.success([aun]))

        client.applyListResult(.failure(ListFailure()))

        XCTAssertTrue(client.hasListed)
        XCTAssertEqual(client.transports, [aun])
        XCTAssertNotNil(client.errorMessage)
    }

    func testDisconnectForgetsTheList() {
        let client = EconetTransportsClient()
        client.applyListResult(.success([aun]))

        client.disconnect()

        XCTAssertFalse(client.hasListed)
        XCTAssertFalse(client.isLoaded)
        XCTAssertTrue(client.transports.isEmpty)
    }
}

@MainActor
final class ExtensionUiClientStreamEndTests: XCTestCase {

    private func view(for extensionID: String) -> Beebium_View {
        var view = Beebium_View()
        view.extensionID = extensionID
        view.viewRevision = 7
        return view
    }

    func testLostStreamKeepsTheLastView() {
        let client = ExtensionUiClient()
        client.handleView(view(for: "aun-0"))

        client.handleSubscriptionEnd(.lost("unavailable (14)"), extensionID: "aun-0")

        XCTAssertEqual(client.views["aun-0"]?.viewRevision, 7)
    }

    func testFinishedStreamDropsTheView() {
        let client = ExtensionUiClient()
        client.handleView(view(for: "aun-0"))

        client.handleSubscriptionEnd(.finished, extensionID: "aun-0")

        XCTAssertNil(client.views["aun-0"])
    }

    func testDisconnectDropsAViewKeptFromALostStream() {
        let client = ExtensionUiClient()
        client.handleView(view(for: "aun-0"))
        client.handleSubscriptionEnd(.lost("unavailable (14)"), extensionID: "aun-0")

        client.disconnect()

        XCTAssertTrue(client.views.isEmpty)
    }
}
