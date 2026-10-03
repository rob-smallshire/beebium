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

/// The shared gRPC event loop group is sized one loop per core, capped at 4 and
/// never below 1 (#152): windows spread across cores without over-subscribing a
/// host that also runs the emulator servers.
final class VideoClientEventLoopTests: XCTestCase {

    func testOneLoopPerCoreUpToTheCap() {
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: 1), 1)
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: 2), 2)
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: 4), 4)
    }

    func testCapsAtFour() {
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: 8), 4)
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: 10), 4)
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: 64), 4)
    }

    func testNeverBelowOne() {
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: 0), 1)
        XCTAssertEqual(VideoClient.eventLoopThreadCount(coreCount: -1), 1)
    }
}
