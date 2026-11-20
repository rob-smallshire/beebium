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

import Foundation
import GRPC
import NIOCore
import NIOPosix
import XCTest
@testable import Beebium

/// Real-server integration test for the memory sidebar's OS section (#189):
/// `SidewaysClient` reads the fitted OS ROM from `SidewaysService.GetOsRom` and
/// the view shows its title over its ROM name. This drives the real client
/// against a real beebium-master-128 -- the client beneath the view, so it is
/// not GUI automation.
///
/// It spawns a server, so it is skipped unless BEEBIUM_OSROM_INTEGRATION=1 (and
/// if the master-128 server or its ROMs are not found). GetOsRom is answered as
/// soon as the server is up -- the machine need not run -- so this is quick.
/// Run (from clients/macos/Beebium):
///   BEEBIUM_OSROM_INTEGRATION=1 xcodebuild test -scheme Beebium \
///     -configuration Debug -destination 'platform=macOS' \
///     -only-testing:BeebiumTests/OsRomIntegrationTests
@MainActor
final class OsRomIntegrationTests: XCTestCase {

    private let port = 49341

    func testMasterReportsItsFittedMos() async throws {
        guard ProcessInfo.processInfo.environment["BEEBIUM_OSROM_INTEGRATION"] == "1" else {
            throw XCTSkip("set BEEBIUM_OSROM_INTEGRATION=1 to run the GetOsRom integration test")
        }
        guard let serverPath = Self.findServer() else {
            throw XCTSkip("beebium-master-128 executable not found")
        }
        let serverDirpath = (serverPath as NSString).deletingLastPathComponent
        // .../build/src/server/beebium-master-128 -> repository root is three up.
        let repoRoot = URL(fileURLWithPath: serverDirpath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let romDirpath = repoRoot.appendingPathComponent("roms").path
        guard FileManager.default.fileExists(atPath: romDirpath) else {
            throw XCTSkip("roms not found beside the server")
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: serverPath)
        process.arguments = ["start", "--port", "\(port)", "--rom-dir", romDirpath]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try process.run()
        defer { if process.isRunning { process.terminate() }; process.waitUntilExit() }

        let group = MultiThreadedEventLoopGroup(numberOfThreads: 1)
        defer { try? group.syncShutdownGracefully() }
        let channel = try GRPCChannelPool.with(
            target: .host("127.0.0.1", port: port),
            transportSecurity: .plaintext, eventLoopGroup: group)
        defer { try? channel.close().wait() }

        try await waitUntilReady(channel)

        let sideways = SidewaysClient()
        sideways.connect(channel: channel)
        defer { sideways.disconnect() }

        // The OS ROM is fetched once on connect; wait for it to arrive.
        let arrived = await pollUntil(timeout: 10) { sideways.osRom != nil }
        let osRom = try XCTUnwrap(sideways.osRom, "GetOsRom never reported an OS ROM")
        XCTAssertTrue(arrived)

        // The default Master 128 MOS: a search-path ROM, titled by its manifest.
        XCTAssertEqual(osRom.title, "Acorn MOS 3.20")
        XCTAssertEqual(osRom.romName, "acorn-mos_3_20")
        XCTAssertEqual(osRom.subtitle, "acorn-mos_3_20",
                       "a search-path ROM shows its bare name, not the server path")
        XCTAssertEqual(osRom.sizeBytes, 16384)
        XCTAssertTrue(osRom.imageUri.hasSuffix("acorn-mos_3_20.rom"),
                      "the resolved image is the server's absolute path")
    }

    // MARK: - Helpers

    private func waitUntilReady(_ channel: GRPCChannel) async throws {
        let sideways = Beebium_SidewaysServiceNIOClient(channel: channel)
        let deadline = Date().addingTimeInterval(20)
        while Date() < deadline {
            if (try? await sideways.getSlotStatus(.init()).response.get()) != nil { return }
            try? await Task.sleep(nanoseconds: 250_000_000)
        }
        throw XCTSkip("the server never became ready on port \(port)")
    }

    private func pollUntil(timeout: TimeInterval, _ condition: () -> Bool) async -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if condition() { return true }
            try? await Task.sleep(nanoseconds: 150_000_000)
        }
        return condition()
    }

    private static func findServer() -> String? {
        if let frameworks = Bundle.main.privateFrameworksPath {
            let path = "\(frameworks)/beebium-master-128"
            if FileManager.default.isExecutableFile(atPath: path) { return path }
        }
        let home = FileManager.default.homeDirectoryForCurrentUser
        let build = home.appendingPathComponent("Code/beebium/build/src/server/beebium-master-128").path
        if FileManager.default.isExecutableFile(atPath: build) { return build }
        return nil
    }
}
