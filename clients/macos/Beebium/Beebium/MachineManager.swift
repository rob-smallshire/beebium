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

import Foundation

/// A server process launched and owned by this macOS app instance
struct ManagedMachine: Identifiable {
    let id: UUID
    let target: ConnectionTarget
    let process: Process
    let provenanceUUID: String
    /// The machine's name, as this app last knew it. Kept here because the
    /// manager is what knows which machines exist, and so is what can say
    /// which names are still spoken for.
    var name: String
    var lifetimeLinked: Bool
    var unlinkRequested: Bool = false
    /// Last known number of WatchServerStatus clients on the server.
    /// Updated by ContentView when SystemClient.clientCount changes.
    /// Used by the app quit handler to avoid creating temporary gRPC connections.
    var lastKnownClientCount: Int = 1
}

// MARK: - Window Close Action

/// What action to take when a window viewing a machine is closed
enum WindowCloseAction: Equatable {
    /// Not a managed machine, or already unlinked. Just disconnect.
    case disconnect
    /// Sole client of a lifetime-linked machine. Shut down the server.
    case shutdownServer
    /// Multiple clients connected to a lifetime-linked machine. Prompt the user.
    case promptUser(clientCount: Int)
}

// MARK: - Quit Action

/// What action to take when the app quits
enum QuitAction: Equatable {
    /// No lifetime-linked machines. Terminate immediately.
    case terminateNow
    /// Some machines need shutdown. Contains the IDs of machines to terminate.
    case shutdownThenTerminate(machineIds: [UUID])
}

/// Central registry tracking server processes launched by this app.
///
/// Externally-connected machines are not tracked here; their absence from
/// the registry is the signal for "silent disconnect" behavior on window close.
///
/// All shutdown decision logic lives here, testable in isolation from the UI.
@MainActor
class MachineManager: ObservableObject {
    static let shared = MachineManager()

    @Published private(set) var machines: [UUID: ManagedMachine] = [:]

    /// The source of machine names, held here so that a name is released
    /// exactly when its machine stops existing.
    private let nameAllocator = MachineNameAllocator()

    /// Non-private init for testing. Production code uses `.shared`.
    init() {}

    // MARK: - Registration

    /// Register a launched core process and return its tracking ID
    @discardableResult
    func register(process: Process, port: Int, provenanceUUID: String,
                  name: String = "") -> UUID {
        let id = UUID()
        let target = ConnectionTarget(host: "127.0.0.1", port: port)
        // A name reserved before launch is already out of the queue; one that
        // arrived some other way is claimed now so it cannot be issued twice.
        nameAllocator.claim(name)
        let machine = ManagedMachine(
            id: id,
            target: target,
            process: process,
            provenanceUUID: provenanceUUID,
            name: name,
            lifetimeLinked: true
        )
        machines[id] = machine
        NSLog("[MachineManager] Registered machine %@ at %@ (provenance: %@)", id.uuidString, target.address, provenanceUUID)
        return id
    }

    // MARK: - Lookup

    /// Look up a managed machine by its gRPC address (host:port)
    func machine(forAddress address: String) -> ManagedMachine? {
        machines.values.first { $0.target.address == address }
    }

    /// Look up a managed machine by its connection target
    func machine(forTarget target: ConnectionTarget) -> ManagedMachine? {
        machine(forAddress: target.address)
    }

    /// Check whether a given address corresponds to a lifetime-linked machine
    func isLifetimeLinked(address: String) -> Bool {
        machine(forAddress: address)?.lifetimeLinked == true
    }

    /// Check whether a given address has a pending unlink request
    func isUnlinkRequested(address: String) -> Bool {
        machine(forAddress: address)?.unlinkRequested == true
    }

    // MARK: - State Changes

    /// Unlink a machine's lifetime from this app.
    /// The server process continues running independently.
    func unlink(id: UUID) {
        guard var machine = machines[id] else { return }
        machine.lifetimeLinked = false
        machine.unlinkRequested = false
        machines[id] = machine
        NSLog("[MachineManager] Unlinked machine %@ at %@", id.uuidString, machine.target.address)
    }

    /// Set or clear the deferred unlink request for a machine.
    /// The actual unlink is deferred until the window closes.
    func setUnlinkRequested(id: UUID, requested: Bool) {
        guard var machine = machines[id] else { return }
        machine.unlinkRequested = requested
        machines[id] = machine
        NSLog("[MachineManager] Unlink %@ for machine %@ at %@",
              requested ? "requested" : "cancelled", id.uuidString, machine.target.address)
    }

    /// Remove tracking for a machine entirely (after shutdown or process exit)
    func unregister(id: UUID) {
        if let machine = machines.removeValue(forKey: id) {
            // The machine is gone, so its name is free -- but goes to the back
            // of the queue, so the next machine is not immediately given the
            // name of the one just closed.
            releaseName(machine.name)
            NSLog("[MachineManager] Unregistered machine %@ at %@", machine.id.uuidString, machine.target.address)
        }
    }

    // MARK: - Names

    /// Take a name for a machine about to be launched.
    ///
    /// Reserved before the process starts, because the name is a command-line
    /// argument to it. If the launch then fails, hand it back with
    /// `releaseName` rather than letting it leak out of the pool.
    func reserveName() -> String {
        nameAllocator.issue()
    }

    /// Return a name to the pool, for reuse after every other free name.
    func releaseName(_ name: String) {
        nameAllocator.release(name)
    }

    /// Record that a machine has been renamed, so the pool keeps up.
    ///
    /// The old name becomes available again and the new one is spoken for.
    /// Without this a machine renamed by hand onto a pool name could later be
    /// duplicated by a generated one.
    func renamed(address: String, to newName: String) {
        guard let machine = machine(forAddress: address), machine.name != newName else {
            return
        }
        machines[machine.id]?.name = newName
        nameAllocator.claim(newName)
        releaseName(machine.name)
    }

    /// Update the cached client count for a machine
    func updateClientCount(address: String, count: Int) {
        guard let machine = machine(forAddress: address) else { return }
        machines[machine.id]?.lastKnownClientCount = count
    }

    /// All machines that are effectively lifetime-linked to this app.
    /// Excludes machines where the user has requested unlinking.
    var linkedMachines: [ManagedMachine] {
        machines.values.filter { $0.lifetimeLinked && !$0.unlinkRequested }
    }

    // MARK: - Window Close Decision

    /// Determine what action to take when a window viewing this address closes.
    ///
    /// This is pure decision logic with no side effects, testable in isolation.
    ///
    /// - Parameters:
    ///   - address: The gRPC address (host:port) of the machine this window is viewing
    ///   - clientCount: Current number of clients connected to that machine's server
    /// - Returns: The action the UI should take
    func windowCloseAction(forAddress address: String, clientCount: Int) -> WindowCloseAction {
        guard let machine = machine(forAddress: address) else {
            NSLog("[MachineManager] windowCloseAction: no managed machine at %@ -> disconnect", address)
            return .disconnect
        }

        guard machine.lifetimeLinked && !machine.unlinkRequested else {
            NSLog("[MachineManager] windowCloseAction: machine at %@ is unlinked -> disconnect", address)
            return .disconnect
        }

        if clientCount <= 1 {
            NSLog("[MachineManager] windowCloseAction: sole client of lifetime-linked machine at %@ -> shutdown", address)
            return .shutdownServer
        }

        NSLog("[MachineManager] windowCloseAction: %d clients at lifetime-linked machine %@ -> prompt", clientCount, address)
        return .promptUser(clientCount: clientCount)
    }

    // MARK: - Shutdown Execution

    /// Terminate the server at the given address via SIGTERM.
    ///
    /// Synchronous and reliable — uses the stored Process reference to send SIGTERM
    /// directly. The server handles SIGTERM gracefully via its signal handler.
    /// Unregisters the machine from tracking.
    ///
    /// This is the primary shutdown mechanism for processes launched by this app.
    /// No gRPC channel is needed.
    ///
    /// - Parameter address: The gRPC address (host:port) of the machine to shut down
    @discardableResult
    func shutdownServer(forAddress address: String) -> Bool {
        guard let machine = machine(forAddress: address) else {
            NSLog("[MachineManager] shutdownServer: no machine found at %@", address)
            return false
        }

        if machine.process.isRunning {
            machine.process.terminate()
            NSLog("[MachineManager] Sent SIGTERM to machine at %@", address)
        } else {
            NSLog("[MachineManager] Machine at %@ already stopped", address)
        }

        unregister(id: machine.id)
        return true
    }

    /// Graceful shutdown via gRPC, with SIGTERM fallback.
    ///
    /// Used for the multi-client case where we want the server to notify other
    /// connected clients before shutting down. Falls back to SIGTERM if the gRPC
    /// request fails or the server doesn't exit promptly.
    ///
    /// - Parameters:
    ///   - address: The gRPC address (host:port) of the machine to shut down
    ///   - systemClient: The SystemClient to use for sending the gRPC shutdown request
    @discardableResult
    func gracefulShutdownServer(forAddress address: String, using systemClient: SystemClient) async -> Bool {
        guard let machine = machine(forAddress: address) else {
            NSLog("[MachineManager] gracefulShutdownServer: no machine found at %@", address)
            return false
        }

        let accepted = await systemClient.requestShutdown()
        if accepted {
            NSLog("[MachineManager] gracefulShutdownServer: server at %@ accepted shutdown", address)
        } else {
            NSLog("[MachineManager] gracefulShutdownServer: gRPC failed for %@, sending SIGTERM", address)
            if machine.process.isRunning {
                machine.process.terminate()
            }
        }

        // Schedule SIGTERM as fallback in case the server accepted but hangs
        let process = machine.process
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            if process.isRunning {
                process.terminate()
                NSLog("[MachineManager] SIGTERM fallback for machine at %@", address)
            }
        }

        unregister(id: machine.id)
        return true
    }

    // MARK: - Quit Decision

    /// Determine what action to take when the app is quitting.
    ///
    /// Pure decision logic with no side effects.
    ///
    /// Quit rule: shut down a core only if BOTH (a) this app launched it AND
    /// (b) it's the sole client. Everything else is left running.
    func quitAction() -> QuitAction {
        let linked = linkedMachines
        let toShutDown = linked.filter { $0.lastKnownClientCount <= 1 }

        if linked.isEmpty {
            NSLog("[MachineManager] quitAction: no linked machines -> terminate now")
            return .terminateNow
        }

        // Unregister multi-client machines (leave them running)
        for machine in linked where machine.lastKnownClientCount > 1 {
            NSLog("[MachineManager] quitAction: leaving multi-client machine at %@ running", machine.target.address)
            unregister(id: machine.id)
        }

        if toShutDown.isEmpty {
            NSLog("[MachineManager] quitAction: all linked machines have other clients -> terminate now")
            return .terminateNow
        }

        let ids = toShutDown.map(\.id)
        NSLog("[MachineManager] quitAction: %d sole-client machines need shutdown", ids.count)
        return .shutdownThenTerminate(machineIds: ids)
    }

    /// Terminate server processes for the given machine IDs (SIGTERM fallback for quit).
    ///
    /// Called after a delay to give gRPC shutdown requests (sent by each window's
    /// onDisappear) time to be processed.
    func terminateServers(ids: [UUID]) {
        for id in ids {
            if let machine = machines[id] {
                if machine.process.isRunning {
                    machine.process.terminate()
                    NSLog("[MachineManager] SIGTERM for machine at %@ during quit", machine.target.address)
                } else {
                    NSLog("[MachineManager] Machine at %@ already stopped during quit", machine.target.address)
                }
                unregister(id: id)
            }
        }
    }
}
