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

/// Real-server integration test for the MOS ROM choice (#189): a MachineDraft
/// whose `mos_image_uri` is set launches a machine running that MOS. On the
/// Master the MOS and banks 9-15 are one chip, so choosing MOS 3.50 must bring
/// the 3.50 banks too -- which the server derives from the MOS in force, so a
/// draft that names only the MOS boots a coherent 3.50 machine.
///
/// It drives the app's own launch path -- writeTemporaryPreset +
/// presetLaunchArguments -- so it proves the draft's MOS is honoured end to end.
/// It spawns a server and types into it, so it is skipped unless
/// BEEBIUM_MOS_INTEGRATION=1 (pass it as TEST_RUNNER_BEEBIUM_MOS_INTEGRATION=1 to
/// xcodebuild, which forwards it into the test host). Run (from
/// clients/macos/Beebium):
///   TEST_RUNNER_BEEBIUM_MOS_INTEGRATION=1 xcodebuild test -scheme Beebium \
///     -configuration Debug -destination 'platform=macOS' \
///     -only-testing:BeebiumTests/MosLaunchIntegrationTests
@MainActor
final class MosLaunchIntegrationTests: XCTestCase {

    private let port = 49352

    func testMasterDraftWithMos350BootsMos350() async throws {
        guard ProcessInfo.processInfo.environment["BEEBIUM_MOS_INTEGRATION"] == "1" else {
            throw XCTSkip("set BEEBIUM_MOS_INTEGRATION=1 to run the MOS launch integration test")
        }
        guard let serverPath = Self.findServer() else {
            throw XCTSkip("beebium-master-128 executable not found")
        }
        let serverDirpath = (serverPath as NSString).deletingLastPathComponent
        let repoRoot = URL(fileURLWithPath: serverDirpath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let romDirpath = repoRoot.appendingPathComponent("roms").path
        guard FileManager.default.fileExists(atPath: romDirpath) else {
            throw XCTSkip("roms not found beside the server")
        }

        // A draft the configurator could produce: the Master with its MOS set to
        // 3.50. The server fills banks 9-15 from the MOS in force, so naming only
        // the MOS is enough for a coherent machine.
        let draft = MachineDraft(model: "master-128", name: "MOS Test",
                                 mosImageUri: "acorn-mos_3_50.rom")
        let presetURL = try PresetManager.writeTemporaryPreset(draft)
        defer { try? FileManager.default.removeItem(at: presetURL) }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: serverPath)
        // The app's own launch arguments, minus advertise (not wanted in a test).
        process.arguments = ["start", "--preset", presetURL.path,
                             "--port", "\(port)", "--rom-dir", romDirpath]
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

        let video = Beebium_VideoServiceNIOClient(channel: channel)
        try await waitUntilReady(video)

        let keyboard = KeyboardClient()
        keyboard.connect(channel: channel)
        defer { keyboard.disconnect() }

        // Let the machine finish booting so the keyboard is live, then switch to
        // MODE 7 (the glyph reader is most reliable on teletext) and *HELP, whose
        // OS line names the MOS version. Type-ahead buffers the keys.
        try? await Task.sleep(nanoseconds: 3_000_000_000)
        _ = await keyboard.typeQuickly("MODE 7\r*HELP\r")
        await keyboard.waitUntilTypingComplete()

        let shows350 = await waitForScreen(video, contains: "OS 3.50", timeout: 25)
        let finalScreen = await screenText(video)
        if !shows350 {
            XCTFail("*HELP did not show OS 3.50; last screen:\n\(finalScreen)")
        }
        // And not the default 3.20, proving the draft's MOS took effect.
        XCTAssertFalse(finalScreen.contains("OS 3.20"))
    }

    // MARK: - Helpers

    private func waitUntilReady(_ video: Beebium_VideoServiceNIOClient) async throws {
        let deadline = Date().addingTimeInterval(20)
        while Date() < deadline {
            if (try? await video.getConfig(Beebium_GetConfigRequest()).response.get()) != nil { return }
            try? await Task.sleep(nanoseconds: 250_000_000)
        }
        throw XCTSkip("the server never became ready on port \(port)")
    }

    private func screenText(_ video: Beebium_VideoServiceNIOClient) async -> String {
        (try? await video.getScreenText(Beebium_GetScreenTextRequest()).response.get())?.text ?? ""
    }

    private func waitForScreen(_ video: Beebium_VideoServiceNIOClient, contains needle: String,
                               timeout: TimeInterval) async -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if await screenText(video).contains(needle) { return true }
            try? await Task.sleep(nanoseconds: 300_000_000)
        }
        return await screenText(video).contains(needle)
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
