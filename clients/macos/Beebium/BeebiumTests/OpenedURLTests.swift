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

final class OpenedURLTests: XCTestCase {

    func testBeebiumConnectLinkClassifiesAsConnect() {
        let url = URL(string: "beebium://connect?host=127.0.0.1&port=48875")!
        guard case .connect(let request) = OpenedURL.classify(url) else {
            return XCTFail("expected .connect, got \(OpenedURL.classify(url))")
        }
        XCTAssertEqual(request.target.host, "127.0.0.1")
        XCTAssertEqual(request.target.port, 48875)
    }

    func testMachinePackageDirectoryClassifiesAsSavedMachine() {
        // A package directory as Finder and LaunchServices deliver it: a file
        // URL with a trailing slash.
        let url = URL(string: "file:///Users/me/Machines/abug.beebium-machine/")!
        guard case .savedMachine(let machineURL) = OpenedURL.classify(url) else {
            return XCTFail("expected .savedMachine, got \(OpenedURL.classify(url))")
        }
        XCTAssertEqual(machineURL, url)
    }

    func testInnerDocumentFileClassifiesAsSavedMachine() {
        // The openable document file inside the package, same name and
        // extension, no trailing slash.
        let url = URL(string: "file:///Users/me/Machines/abug.beebium-machine/abug.beebium-machine")!
        guard case .savedMachine = OpenedURL.classify(url) else {
            return XCTFail("expected .savedMachine, got \(OpenedURL.classify(url))")
        }
    }

    func testPathWithSpacesClassifiesAsSavedMachine() {
        let url = URL(fileURLWithPath: "/Users/me/Machines/Model B (Blank disc) #1.beebium-machine",
                      isDirectory: true)
        guard case .savedMachine = OpenedURL.classify(url) else {
            return XCTFail("expected .savedMachine, got \(OpenedURL.classify(url))")
        }
    }

    func testOtherFileClassifiesAsIgnored() {
        let url = URL(fileURLWithPath: "/Users/me/notes.txt")
        guard case .ignored = OpenedURL.classify(url) else {
            return XCTFail("expected .ignored, got \(OpenedURL.classify(url))")
        }
    }

    func testNonBeebiumSchemeWithMachineExtensionClassifiesAsIgnored() {
        // Not a file URL, so the machine extension in the path must not make it
        // a saved machine.
        let url = URL(string: "https://example.com/abug.beebium-machine")!
        guard case .ignored = OpenedURL.classify(url) else {
            return XCTFail("expected .ignored, got \(OpenedURL.classify(url))")
        }
    }
}
