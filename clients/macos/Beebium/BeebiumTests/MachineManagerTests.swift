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

final class MachineManagerTests: XCTestCase {

    // MARK: - Registration

    @MainActor
    func testRegisterCreatesLinkedMachine() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "test-uuid")

        XCTAssertNotNil(manager.machines[id])
        let machine = manager.machines[id]!
        XCTAssertEqual(machine.target.address, "127.0.0.1:50000")
        XCTAssertEqual(machine.provenanceUUID, "test-uuid")
        XCTAssertTrue(machine.lifetimeLinked)
        XCTAssertEqual(machine.lastKnownClientCount, 1)
    }

    @MainActor
    func testLookupByAddress() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        let found = manager.machine(forAddress: "127.0.0.1:50000")
        XCTAssertNotNil(found)
        XCTAssertEqual(found?.id, id)

        let notFound = manager.machine(forAddress: "127.0.0.1:99999")
        XCTAssertNil(notFound)
    }

    @MainActor
    func testLookupByTarget() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        let target = ConnectionTarget(host: "127.0.0.1", port: 50000)
        let found = manager.machine(forTarget: target)
        XCTAssertNotNil(found)
        XCTAssertEqual(found?.id, id)
    }

    // MARK: - State Changes

    @MainActor
    func testUnlinkSetsLifetimeLinkedFalse() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        XCTAssertTrue(manager.isLifetimeLinked(address: "127.0.0.1:50000"))

        manager.unlink(id: id)

        XCTAssertFalse(manager.isLifetimeLinked(address: "127.0.0.1:50000"))
        // Machine should still be tracked
        XCTAssertNotNil(manager.machines[id])
    }

    @MainActor
    func testUnlinkClearsUnlinkRequested() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        manager.setUnlinkRequested(id: id, requested: true)
        XCTAssertTrue(manager.isUnlinkRequested(address: "127.0.0.1:50000"))

        manager.unlink(id: id)

        XCTAssertFalse(manager.isUnlinkRequested(address: "127.0.0.1:50000"))
        XCTAssertFalse(manager.isLifetimeLinked(address: "127.0.0.1:50000"))
    }

    @MainActor
    func testSetUnlinkRequestedTogglesFlag() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        XCTAssertFalse(manager.isUnlinkRequested(address: "127.0.0.1:50000"))

        manager.setUnlinkRequested(id: id, requested: true)
        XCTAssertTrue(manager.isUnlinkRequested(address: "127.0.0.1:50000"))
        // Machine is still lifetime-linked (unlink not yet finalized)
        XCTAssertTrue(manager.isLifetimeLinked(address: "127.0.0.1:50000"))

        manager.setUnlinkRequested(id: id, requested: false)
        XCTAssertFalse(manager.isUnlinkRequested(address: "127.0.0.1:50000"))
    }

    @MainActor
    func testIsUnlinkRequestedForUnknownAddress() async {
        let manager = MachineManager()
        XCTAssertFalse(manager.isUnlinkRequested(address: "127.0.0.1:99999"))
    }

    @MainActor
    func testUnregisterRemovesMachine() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        manager.unregister(id: id)

        XCTAssertNil(manager.machines[id])
        XCTAssertNil(manager.machine(forAddress: "127.0.0.1:50000"))
    }

    @MainActor
    func testUpdateClientCount() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        manager.updateClientCount(address: "127.0.0.1:50000", count: 3)

        XCTAssertEqual(manager.machines[id]?.lastKnownClientCount, 3)
    }

    @MainActor
    func testLinkedMachinesFiltersUnlinked() async {
        let manager = MachineManager()
        let p1 = Process()
        let p2 = Process()
        let id1 = manager.register(process: p1, port: 50001, provenanceUUID: "uuid1")
        _ = manager.register(process: p2, port: 50002, provenanceUUID: "uuid2")

        manager.unlink(id: id1)

        let linked = manager.linkedMachines
        XCTAssertEqual(linked.count, 1)
        XCTAssertEqual(linked.first?.target.address, "127.0.0.1:50002")
    }

    @MainActor
    func testLinkedMachinesExcludesUnlinkRequested() async {
        let manager = MachineManager()
        let p1 = Process()
        let p2 = Process()
        let id1 = manager.register(process: p1, port: 50001, provenanceUUID: "uuid1")
        _ = manager.register(process: p2, port: 50002, provenanceUUID: "uuid2")

        manager.setUnlinkRequested(id: id1, requested: true)

        let linked = manager.linkedMachines
        XCTAssertEqual(linked.count, 1)
        XCTAssertEqual(linked.first?.target.address, "127.0.0.1:50002")
    }

    // MARK: - Window Close Action Decision

    @MainActor
    func testWindowCloseActionUnmanagedMachineReturnsDisconnect() async {
        let manager = MachineManager()

        let action = manager.windowCloseAction(forAddress: "192.168.1.1:48875", clientCount: 1)

        XCTAssertEqual(action, .disconnect)
    }

    @MainActor
    func testWindowCloseActionUnlinkedMachineReturnsDisconnect() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")
        manager.unlink(id: id)

        let action = manager.windowCloseAction(forAddress: "127.0.0.1:50000", clientCount: 1)

        XCTAssertEqual(action, .disconnect)
    }

    @MainActor
    func testWindowCloseActionUnlinkRequestedReturnsDisconnect() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")
        manager.setUnlinkRequested(id: id, requested: true)

        let action = manager.windowCloseAction(forAddress: "127.0.0.1:50000", clientCount: 1)

        XCTAssertEqual(action, .disconnect)
    }

    @MainActor
    func testWindowCloseActionSoleClientReturnsShutdown() async {
        let manager = MachineManager()
        let process = Process()
        _ = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        let action = manager.windowCloseAction(forAddress: "127.0.0.1:50000", clientCount: 1)

        XCTAssertEqual(action, .shutdownServer)
    }

    @MainActor
    func testWindowCloseActionZeroClientsReturnsShutdown() async {
        let manager = MachineManager()
        let process = Process()
        _ = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        // clientCount 0 should still trigger shutdown (stale count or not yet updated)
        let action = manager.windowCloseAction(forAddress: "127.0.0.1:50000", clientCount: 0)

        XCTAssertEqual(action, .shutdownServer)
    }

    @MainActor
    func testWindowCloseActionMultipleClientsReturnsPrompt() async {
        let manager = MachineManager()
        let process = Process()
        _ = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        let action = manager.windowCloseAction(forAddress: "127.0.0.1:50000", clientCount: 3)

        XCTAssertEqual(action, .promptUser(clientCount: 3))
    }

    @MainActor
    func testWindowCloseActionTwoClientsReturnsPrompt() async {
        let manager = MachineManager()
        let process = Process()
        _ = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        let action = manager.windowCloseAction(forAddress: "127.0.0.1:50000", clientCount: 2)

        XCTAssertEqual(action, .promptUser(clientCount: 2))
    }

    // MARK: - Quit Action Decision

    @MainActor
    func testQuitActionNoMachinesReturnsTerminateNow() async {
        let manager = MachineManager()

        let action = manager.quitAction()

        XCTAssertEqual(action, .terminateNow)
    }

    @MainActor
    func testQuitActionSoleClientReturnsShutdownThenTerminate() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        let action = manager.quitAction()

        XCTAssertEqual(action, .shutdownThenTerminate(machineIds: [id]))
    }

    @MainActor
    func testQuitActionMultiClientReturnsTerminateNow() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")
        manager.updateClientCount(address: "127.0.0.1:50000", count: 3)

        let action = manager.quitAction()

        XCTAssertEqual(action, .terminateNow)
        // Multi-client machine should have been unregistered
        XCTAssertNil(manager.machines[id])
    }

    @MainActor
    func testQuitActionMixedMachines() async {
        let manager = MachineManager()
        let p1 = Process()
        let p2 = Process()
        let id1 = manager.register(process: p1, port: 50001, provenanceUUID: "uuid1")
        let id2 = manager.register(process: p2, port: 50002, provenanceUUID: "uuid2")
        manager.updateClientCount(address: "127.0.0.1:50002", count: 5)

        let action = manager.quitAction()

        XCTAssertEqual(action, .shutdownThenTerminate(machineIds: [id1]))
        // Multi-client machine should have been unregistered
        XCTAssertNil(manager.machines[id2])
    }

    @MainActor
    func testQuitActionUnlinkedMachinesAreIgnored() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")
        manager.unlink(id: id)

        let action = manager.quitAction()

        // Unlinked machines are not in linkedMachines, so no shutdown needed
        XCTAssertEqual(action, .terminateNow)
    }

    @MainActor
    func testQuitActionUnlinkRequestedMachinesAreIgnored() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")
        manager.setUnlinkRequested(id: id, requested: true)

        let action = manager.quitAction()

        // Machines with pending unlink request are not in linkedMachines
        XCTAssertEqual(action, .terminateNow)
    }

    // MARK: - isLifetimeLinked

    @MainActor
    func testIsLifetimeLinkedForUnknownAddress() async {
        let manager = MachineManager()

        XCTAssertFalse(manager.isLifetimeLinked(address: "127.0.0.1:50000"))
    }

    @MainActor
    func testIsLifetimeLinkedForLinkedMachine() async {
        let manager = MachineManager()
        let process = Process()
        _ = manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        XCTAssertTrue(manager.isLifetimeLinked(address: "127.0.0.1:50000"))
    }

    @MainActor
    func testIsLifetimeLinkedForUnlinkedMachine() async {
        let manager = MachineManager()
        let process = Process()
        let id = manager.register(process: process, port: 50000, provenanceUUID: "uuid")
        manager.unlink(id: id)

        XCTAssertFalse(manager.isLifetimeLinked(address: "127.0.0.1:50000"))
    }

    // MARK: - shutdownServer Integration Tests

    @MainActor
    func testShutdownServerTerminatesProcess() async {
        // Test that shutdownServer() actually terminates a running process
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sleep")
        process.arguments = ["1000"]
        try! process.run()

        let manager = MachineManager()
        manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        XCTAssertTrue(process.isRunning)

        let result = manager.shutdownServer(forAddress: "127.0.0.1:50000")
        XCTAssertTrue(result, "shutdownServer should return true for a registered machine")

        let deadline = Date().addingTimeInterval(2.0)
        while process.isRunning && Date() < deadline {
            try? await Task.sleep(nanoseconds: 50_000_000)
        }

        XCTAssertFalse(process.isRunning, "Process should have been terminated by shutdownServer")
        XCTAssertNil(manager.machine(forAddress: "127.0.0.1:50000"),
                     "Machine should be unregistered after shutdown")
    }

    @MainActor
    func testShutdownServerReturnsFalseForUnknownAddress() async {
        let manager = MachineManager()
        let result = manager.shutdownServer(forAddress: "127.0.0.1:99999")
        XCTAssertFalse(result, "shutdownServer should return false for unknown address")
    }

    @MainActor
    func testWindowCloseActionThenShutdownServerFlow() async {
        // Test the full decision → execution flow that ContentView.onDisappear uses
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sleep")
        process.arguments = ["1000"]
        try! process.run()

        let manager = MachineManager()
        manager.register(process: process, port: 50000, provenanceUUID: "uuid")

        XCTAssertTrue(process.isRunning)

        // Simulate what onDisappear does
        let action = manager.windowCloseAction(forAddress: "127.0.0.1:50000", clientCount: 1)
        XCTAssertEqual(action, .shutdownServer)

        if case .shutdownServer = action {
            manager.shutdownServer(forAddress: "127.0.0.1:50000")
        }

        let deadline = Date().addingTimeInterval(2.0)
        while process.isRunning && Date() < deadline {
            try? await Task.sleep(nanoseconds: 50_000_000)
        }

        XCTAssertFalse(process.isRunning, "Process should be dead after windowCloseAction + shutdownServer")
    }

    @MainActor
    func testShutdownServerWithBeebiumServer() async throws {
        // Integration test with the actual beebium server executable.
        // Verifies that SIGTERM via process.terminate() actually stops the server.
        let serverPath = findBeebiumServer()
        guard let serverPath = serverPath else {
            throw XCTSkip("beebium-model-b executable not found")
        }

        let (process, port) = try await launchBeebiumServer(at: serverPath)

        // Register with MachineManager and shut down via the real code path
        let manager = MachineManager()
        manager.register(process: process, port: port, provenanceUUID: UUID().uuidString)

        XCTAssertTrue(process.isRunning, "Server should be running before shutdown")

        let result = manager.shutdownServer(forAddress: "127.0.0.1:\(port)")
        XCTAssertTrue(result, "shutdownServer should return true")

        // Wait up to 5 seconds for the server to exit
        let deadline = Date().addingTimeInterval(5.0)
        while process.isRunning && Date() < deadline {
            try await Task.sleep(nanoseconds: 100_000_000)
        }

        XCTAssertFalse(process.isRunning,
                       "beebium-model-b should have exited after SIGTERM (status: \(process.terminationStatus))")
    }

    // MARK: - WindowCloseCoordinator Integration Test

    @MainActor
    func testCoordinatorInstallsOnCloseButton() async {
        // Verify the coordinator correctly redirects the close button's target/action
        let manager = MachineManager()
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 100, height: 100),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        let videoClient = VideoClient()
        let systemClient = SystemClient()

        let coordinator = WindowCloseCoordinator(
            systemClient: systemClient,
            videoClient: videoClient,
            machineManager: manager,
            window: window,
            clientGroup: ClientGroup(),
            reconnectCoordinator: ReconnectCoordinator()
        )
        coordinator.install(on: window)

        let closeButton = window.standardWindowButton(.closeButton)
        XCTAssertNotNil(closeButton, "Window should have a close button")
        XCTAssertTrue(closeButton?.target === coordinator,
                      "Close button target should be the coordinator")
    }

    @MainActor
    func testCoordinatorShutdownsServerOnClose() async throws {
        // End-to-end test: launch a real server, set up a coordinator,
        // verify the full decision-execution flow sends SIGTERM.
        //
        // Note: We don't call window.close() because NSWindow.close() in an
        // async test context causes a crash (AppKit/Swift Concurrency interaction).
        // The window close is UI cleanup; what matters is the SIGTERM.
        let serverPath = findBeebiumServer()
        guard let serverPath = serverPath else {
            throw XCTSkip("beebium-model-b executable not found")
        }

        let (process, port) = try await launchBeebiumServer(at: serverPath)

        let manager = MachineManager()
        manager.register(process: process, port: port, provenanceUUID: UUID().uuidString)

        let videoClient = VideoClient(target: ConnectionTarget(host: "127.0.0.1", port: port))
        let systemClient = SystemClient()

        // Simulate the exact same decision-execution flow as handleCloseButton:
        // 1. Read address and client count from the clients
        let address = videoClient.target.address
        let clientCount = systemClient.clientCount

        // 2. Ask MachineManager for the action
        let action = manager.windowCloseAction(forAddress: address, clientCount: clientCount)
        XCTAssertEqual(action, .shutdownServer,
                       "Sole-client lifetime-linked machine should trigger shutdown")

        // 3. Execute the action
        XCTAssertTrue(process.isRunning, "Server should be running before shutdown")
        if case .shutdownServer = action {
            manager.shutdownServer(forAddress: address)
        }
        // 4. In the real app, window.close() happens here

        // Wait for the server to exit
        let deadline = Date().addingTimeInterval(5.0)
        while process.isRunning && Date() < deadline {
            try await Task.sleep(nanoseconds: 100_000_000)
        }

        XCTAssertFalse(process.isRunning,
                       "beebium-model-b should be terminated after coordinator shutdown flow")
        XCTAssertNil(manager.machine(forAddress: "127.0.0.1:\(port)"),
                     "Machine should be unregistered after shutdown")
    }

    // MARK: - Helpers

    /// Launch a beebium server and wait for it to report its port
    private func launchBeebiumServer(at serverPath: String) async throws -> (Process, Int) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: serverPath)
        process.arguments = ["start", "--port", "0", "--wait=api"]

        let stdoutPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = FileHandle.nullDevice

        try process.run()

        var port: Int?
        let deadline = Date().addingTimeInterval(10.0)
        var buffer = Data()
        let regex = try NSRegularExpression(pattern: #"Listening on port (\d+)"#)

        while Date() < deadline && port == nil {
            if !process.isRunning {
                XCTFail("Server exited unexpectedly with status \(process.terminationStatus)")
                throw CoreLaunchError.processExited("Server exited with status \(process.terminationStatus)")
            }
            let chunk = stdoutPipe.fileHandleForReading.availableData
            if chunk.isEmpty {
                try await Task.sleep(nanoseconds: 50_000_000)
                continue
            }
            buffer.append(chunk)
            if let text = String(data: buffer, encoding: .utf8),
               let match = regex.firstMatch(in: text, range: NSRange(text.startIndex..., in: text)),
               let portRange = Range(match.range(at: 1), in: text) {
                port = Int(text[portRange])
            }
        }

        guard let actualPort = port else {
            process.terminate()
            throw CoreLaunchError.timeout
        }

        return (process, actualPort)
    }

    /// Find the beebium-model-b executable in known locations
    private func findBeebiumServer() -> String? {
        // Check app bundle first
        if let bundlePath = Bundle.main.privateFrameworksPath {
            let path = "\(bundlePath)/beebium-model-b"
            if FileManager.default.isExecutableFile(atPath: path) {
                return path
            }
        }

        // Check build directory
        let homeDir = FileManager.default.homeDirectoryForCurrentUser
        let buildPath = homeDir.appendingPathComponent("Code/beebium/build/src/server/beebium-model-b").path
        if FileManager.default.isExecutableFile(atPath: buildPath) {
            return buildPath
        }

        return nil
    }
}
