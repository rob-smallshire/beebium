// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

final class VideoClientTests: XCTestCase {

    @MainActor
    func testInitialState() async {
        let client = VideoClient()

        XCTAssertEqual(client.connectionState, .disconnected)
        XCTAssertNil(client.currentFrame)
        XCTAssertEqual(client.frameWidth, 736)
        XCTAssertEqual(client.frameHeight, 576)
        XCTAssertEqual(client.frameCount, 0)
    }

    @MainActor
    func testConnectionStateEquatable() async {
        XCTAssertEqual(ConnectionState.disconnected, ConnectionState.disconnected)
        XCTAssertEqual(ConnectionState.connecting, ConnectionState.connecting)
        XCTAssertEqual(ConnectionState.connected, ConnectionState.connected)
        XCTAssertEqual(ConnectionState.error("test"), ConnectionState.error("test"))

        XCTAssertNotEqual(ConnectionState.disconnected, ConnectionState.connecting)
        XCTAssertNotEqual(ConnectionState.error("a"), ConnectionState.error("b"))
    }

    @MainActor
    func testCustomHostAndPort() async {
        let client = VideoClient(host: "192.168.1.100", port: 8080)

        // Just verify it can be created with custom parameters
        XCTAssertEqual(client.connectionState, .disconnected)
    }
}
