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

final class DeepLinkTests: XCTestCase {
    private func parse(_ string: String) -> DeepLinkConnectRequest? {
        guard let url = URL(string: string) else {
            XCTFail("could not form URL from \(string)")
            return nil
        }
        return DeepLink.parseConnect(url)
    }

    func testHostAndPort() {
        let request = parse("beebium://connect?host=127.0.0.1&port=48875")
        XCTAssertEqual(request?.target, ConnectionTarget(host: "127.0.0.1", port: 48875))
        XCTAssertEqual(request?.needsRun, false)
        XCTAssertNil(request?.provenanceUUID)
    }

    func testPortDefaultsWhenOmitted() {
        let request = parse("beebium://connect?host=localhost")
        XCTAssertEqual(request?.target, ConnectionTarget(host: "localhost", port: DeepLink.defaultPort))
    }

    func testRunFlagVariants() {
        XCTAssertEqual(parse("beebium://connect?host=h&run=true")?.needsRun, true)
        XCTAssertEqual(parse("beebium://connect?host=h&run=1")?.needsRun, true)
        XCTAssertEqual(parse("beebium://connect?host=h&run=yes")?.needsRun, true)
        XCTAssertEqual(parse("beebium://connect?host=h&run=false")?.needsRun, false)
        XCTAssertEqual(parse("beebium://connect?host=h")?.needsRun, false)
    }

    func testProvenanceCarried() {
        let request = parse("beebium://connect?host=h&provenance=ABC-123")
        XCTAssertEqual(request?.provenanceUUID, "ABC-123")
    }

    func testSidebarParam() {
        XCTAssertEqual(parse("beebium://connect?host=h&sidebar=closed")?.showSidebar, false)
        XCTAssertEqual(parse("beebium://connect?host=h&sidebar=open")?.showSidebar, true)
        XCTAssertNil(parse("beebium://connect?host=h")?.showSidebar)
        XCTAssertNil(parse("beebium://connect?host=h&sidebar=weird")?.showSidebar)
    }

    func testWrongSchemeRejected() {
        XCTAssertNil(parse("http://connect?host=h&port=1"))
    }

    func testWrongActionRejected() {
        XCTAssertNil(parse("beebium://disconnect?host=h"))
    }

    func testMissingHostRejected() {
        XCTAssertNil(parse("beebium://connect?port=48875"))
    }

    func testEmptyHostRejected() {
        XCTAssertNil(parse("beebium://connect?host=&port=48875"))
    }

    func testOutOfRangePortRejected() {
        XCTAssertNil(parse("beebium://connect?host=h&port=0"))
        XCTAssertNil(parse("beebium://connect?host=h&port=70000"))
        XCTAssertNil(parse("beebium://connect?host=h&port=notanumber"))
    }
}
