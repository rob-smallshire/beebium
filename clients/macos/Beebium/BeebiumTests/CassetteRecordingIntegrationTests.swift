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

/// Real-server integration test for #208: recording a SAVE onto a freshly
/// created blank tape must write the user's own copy, never a shipped template,
/// and the created copy -- not a template -- is what Recent Tapes remembers.
///
/// Rob hit a SAVE landing in the repository's template, which Recent Tapes had
/// then offered under a label indistinguishable from a tape of his own. The
/// server fix is proven server-side (b8333d13); this proves the macOS client
/// end to end by driving the real clients (CassetteClient, KeyboardClient,
/// RecentTapesManager) against a real beebium-model-b. It never drives the app's
/// views -- only the clients beneath them -- so it is not GUI automation.
///
/// It spawns a server and runs the emulator at real speed (a SAVE and a CHAIN
/// take tens of seconds), so it is skipped unless BEEBIUM_CASSETTE_INTEGRATION=1.
/// The server is pointed at a temporary copy of the blank templates through
/// BEEBIUM_SERVERS_DIRPATH, so the repository's own templates are never touched.
/// Run (from clients/macos/Beebium):
///   BEEBIUM_CASSETTE_INTEGRATION=1 xcodebuild test -scheme Beebium \
///     -configuration Debug -destination 'platform=macOS' \
///     -only-testing:BeebiumTests/CassetteRecordingIntegrationTests
@MainActor
final class CassetteRecordingIntegrationTests: XCTestCase {

    private let port = 49287

    func testRecordingANewBlankTapeKeepsTemplatesPristineAndReloads() async throws {
        guard ProcessInfo.processInfo.environment["BEEBIUM_CASSETTE_INTEGRATION"] == "1" else {
            throw XCTSkip("set BEEBIUM_CASSETTE_INTEGRATION=1 to run the #208 recording integration test")
        }
        guard let serverPath = Self.findServer() else {
            throw XCTSkip("beebium-model-b executable not found")
        }
        let fm = FileManager.default
        let serverDirpath = (serverPath as NSString).deletingLastPathComponent
        let templatesSourceDirpath = (serverDirpath as NSString).appendingPathComponent("blank-tapes")
        // .../build/src/server/beebium-model-b -> repository root is three up.
        let repoRoot = URL(fileURLWithPath: serverDirpath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let romDirpath = repoRoot.appendingPathComponent("roms").path
        guard fm.fileExists(atPath: templatesSourceDirpath), fm.fileExists(atPath: romDirpath) else {
            throw XCTSkip("blank-tapes templates or roms not found beside the server")
        }

        // An isolated servers directory holding a COPY of the shipped templates,
        // and an output directory for the created tape. The repository's own
        // templates are never touched.
        let tempRoot = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("beebium-cassette-it-\(UUID().uuidString)")
        let templatesDirpath = tempRoot.appendingPathComponent("blank-tapes")
        let outDirpath = tempRoot.appendingPathComponent("out")
        try fm.createDirectory(at: templatesDirpath, withIntermediateDirectories: true)
        try fm.createDirectory(at: outDirpath, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: tempRoot) }

        var templateBytesByName: [String: Data] = [:]
        for name in try fm.contentsOfDirectory(atPath: templatesSourceDirpath) where name.hasSuffix(".uef") {
            let source = URL(fileURLWithPath: (templatesSourceDirpath as NSString).appendingPathComponent(name))
            let destination = templatesDirpath.appendingPathComponent(name)
            try fm.copyItem(at: source, to: destination)
            templateBytesByName[name] = try Data(contentsOf: destination)
        }
        XCTAssertFalse(templateBytesByName.isEmpty, "expected the shipped blank templates to copy")

        // Launch the server pointed at the temporary servers directory.
        let process = Process()
        process.executableURL = URL(fileURLWithPath: serverPath)
        process.arguments = ["start", "--port", "\(port)", "--rom-dir", romDirpath]
        var environment = ProcessInfo.processInfo.environment
        environment["BEEBIUM_SERVERS_DIRPATH"] = tempRoot.path
        process.environment = environment
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
        guard await waitForScreen(video, contains: "BASIC", timeout: 40) else {
            throw XCTSkip("the machine did not reach BASIC (ROMs missing?)")
        }

        let cassette = CassetteClient()
        let keyboard = KeyboardClient()
        cassette.connect(channel: channel)
        keyboard.connect(channel: channel)
        defer { cassette.disconnect(); keyboard.disconnect() }

        let recents = RecentTapesManager(defaults: isolatedDefaults())

        await assertEventually(10, "the deck status and the blank-tape list should arrive") {
            cassette.isLoaded && !cassette.blankTapes.isEmpty
        }

        // --- Phase A: create a blank C15, insert it, record a SAVE onto it ---

        guard let c15 = cassette.blankTapes.first(where: { $0.label.contains("C15") }) else {
            return XCTFail("the server offered no C15 blank template")
        }
        let createdPath: String
        switch await cassette.createTape(label: c15.label,
                                         destinationPath: outDirpath.appendingPathComponent("recorded.uef").path,
                                         overwrite: true) {
        case .success(let path): createdPath = path
        case .failure(let error): return XCTFail("createTape failed: \(error.localizedDescription)")
        }
        XCTAssertTrue(fm.fileExists(atPath: createdPath), "the created tape should exist")
        XCTAssertNotEqual(
            URL(fileURLWithPath: createdPath).resolvingSymlinksInPath().path,
            templatesDirpath.appendingPathComponent(c15.filename).resolvingSymlinksInPath().path,
            "the created tape must be the user's copy, not the template itself")

        switch await cassette.insertTape(url: URL(fileURLWithPath: createdPath)) {
        case .success: break
        case .failure(let error): return XCTFail("insertTape failed: \(error.localizedDescription)")
        }
        // Mirror the deck view: a successful insert is recorded in Recent Tapes.
        recents.record(filepath: createdPath)
        await assertEventually(5, "the tape should read as inserted") { cassette.tapeInserted }
        XCTAssertEqual(cassette.tape?.recordable, true, "a fresh blank tape must be recordable")

        switch await cassette.record() {
        case .success: break
        case .failure(let error): return XCTFail("record failed: \(error.localizedDescription)")
        }
        await assertEventually(5, "the deck should enter the recording transport") {
            cassette.transport == .recording
        }

        // Enter a one-line program and SAVE it, then answer the MOS prompt.
        let saveRefusal = await keyboard.typeQuickly("*TAPE\r10 PRINT \"LOADED OK\"\rSAVE \"CASS\"\r")
        XCTAssertNil(saveRefusal, "typing the SAVE should be accepted")
        await keyboard.waitUntilTypingComplete()
        let reachedPrompt = await waitForScreen(video, contains: "RECORD then RETURN", timeout: 20)
        guard reachedPrompt else {
            return XCTFail("the SAVE did not reach the RECORD-then-RETURN prompt")
        }
        let returnRefusal = await keyboard.typeQuickly("\r")
        XCTAssertNil(returnRefusal, "pressing RETURN at the prompt should be accepted")
        await keyboard.waitUntilTypingComplete()

        // The relay closes while the block is written, then opens when it is done.
        await assertEventually(20, "the motor should close to write") { cassette.motorOn }
        await assertEventually(90, "the save should finish") { !cassette.motorOn }

        switch await cassette.stop() {
        case .success: break
        case .failure(let error): return XCTFail("stop failed: \(error.localizedDescription)")
        }
        await assertEventually(5, "the deck should return to stopped") { cassette.transport == .stopped }

        // The deck holds the created file, by its url and name.
        XCTAssertEqual(resolvedPath(of: cassette.tape?.url),
                       URL(fileURLWithPath: createdPath).resolvingSymlinksInPath().path,
                       "the deck's tape url must be the created file")
        XCTAssertEqual(cassette.tape?.name, URL(fileURLWithPath: createdPath).lastPathComponent)

        // Eject (which flushes the recording to disk), then the byte assertions.
        _ = await cassette.ejectTape()
        await assertEventually(5, "the deck should empty after eject") { !cassette.tapeInserted }

        let c15TemplateBytes = templateBytesByName[c15.filename]
        XCTAssertNotNil(c15TemplateBytes)
        await assertEventually(10, "the recorded tape must differ from the blank template") {
            (try? Data(contentsOf: URL(fileURLWithPath: createdPath))) != c15TemplateBytes
        }

        for (name, before) in templateBytesByName {
            let now = try Data(contentsOf: templatesDirpath.appendingPathComponent(name))
            XCTAssertEqual(now, before, "the shipped template \(name) must be byte-identical to before")
        }

        // Recent Tapes remembers the created path, never a template.
        XCTAssertEqual(recents.tapes.first?.filepath, createdPath,
                       "Recent Tapes must hold the created tape")
        XCTAssertFalse(recents.tapes.contains { $0.filepath.contains("blank-tapes") },
                       "Recent Tapes must not hold a template path")

        // --- Phase B: pick the same tape from Recent and CHAIN it back ---

        guard let recentPath = recents.tapes.first?.filepath else {
            return XCTFail("no recent tape to reload")
        }
        switch await cassette.insertTape(url: URL(fileURLWithPath: recentPath)) {
        case .success: break
        case .failure(let error): return XCTFail("reinserting from Recent failed: \(error.localizedDescription)")
        }
        recents.record(filepath: recentPath)
        await assertEventually(5, "the reinserted tape should read as inserted") { cassette.tapeInserted }

        _ = await cassette.seek(positionSeconds: 0)
        switch await cassette.play() {
        case .success: break
        case .failure(let error): return XCTFail("play failed: \(error.localizedDescription)")
        }
        let chainRefusal = await keyboard.typeQuickly("NEW\r*TAPE\rCLS\rCHAIN \"\"\r")
        XCTAssertNil(chainRefusal, "typing the CHAIN should be accepted")
        await keyboard.waitUntilTypingComplete()
        let loaded = await waitForScreen(video, contains: "LOADED OK", timeout: 90)
        XCTAssertTrue(loaded, "CHAIN\"\" from Recent should load and run the saved program")

        _ = await cassette.stop()
    }

    // MARK: - Helpers

    /// The file-system path a deck tape url resolves to, symlinks collapsed so a
    /// macOS /var -> /private/var temporary path compares equal.
    private func resolvedPath(of url: String?) -> String? {
        guard let url, !url.isEmpty else { return nil }
        if let parsed = URL(string: url), parsed.isFileURL {
            return parsed.resolvingSymlinksInPath().path
        }
        return URL(fileURLWithPath: url).resolvingSymlinksInPath().path
    }

    /// A throwaway UserDefaults suite, so the test's Recent Tapes never touch the
    /// host's real preferences.
    private func isolatedDefaults() -> UserDefaults {
        let suite = "beebium-cassette-it-\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defaults.removePersistentDomain(forName: suite)
        return defaults
    }

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
            try? await Task.sleep(nanoseconds: 200_000_000)
        }
        return await screenText(video).contains(needle)
    }

    private func pollUntil(timeout: TimeInterval, _ condition: () -> Bool) async -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if condition() { return true }
            try? await Task.sleep(nanoseconds: 150_000_000)
        }
        return condition()
    }

    /// Assert `condition` becomes true within `timeout`, polling the published
    /// deck state. A separate method so the `await` is not inside an XCTAssert
    /// autoclosure, which cannot suspend.
    private func assertEventually(_ timeout: TimeInterval, _ message: String,
                                  file: StaticString = #filePath, line: UInt = #line,
                                  _ condition: () -> Bool) async {
        let satisfied = await pollUntil(timeout: timeout, condition)
        XCTAssertTrue(satisfied, message, file: file, line: line)
    }

    private static func findServer() -> String? {
        if let frameworks = Bundle.main.privateFrameworksPath {
            let path = "\(frameworks)/beebium-model-b"
            if FileManager.default.isExecutableFile(atPath: path) { return path }
        }
        let home = FileManager.default.homeDirectoryForCurrentUser
        let build = home.appendingPathComponent("Code/beebium/build/src/server/beebium-model-b").path
        if FileManager.default.isExecutableFile(atPath: build) { return build }
        return nil
    }
}
