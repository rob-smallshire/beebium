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
import AppKit

/// Opening a saved machine, from wherever the request comes -- File > Open...,
/// a double-clicked .beebium-machine package, or the Welcome window's Machines
/// page. It reads the machine with describe-machine, decides via OpenMachinePlan
/// whether a window is already showing it (bring that forward) or it must be
/// launched, and on launch opens a window on the new server and records the
/// machine in recents.
///
/// `openMainWindow` is the one piece the caller supplies: opening a window needs
/// SwiftUI's openWindow, which only a scene or view holds. Everything else is
/// the same wherever the request originates.
enum SavedMachineOpening {

    @MainActor
    static func open(openedURL: URL, openMainWindow: @escaping () -> Void) async {
        let machineDirpath = MachineDocument.machineDirectory(forOpenedPath: openedURL)

        guard let description = await PresetManager.shared.describeMachine(path: machineDirpath.path) else {
            presentError("Beebium could not read a saved machine at \"\(machineDirpath.lastPathComponent)\".")
            return
        }

        switch OpenMachinePlan.plan(for: description,
                                    appLaunchedPids: MachineManager.shared.launchedProcessIdentifiers) {
        case .bringForward(let pid):
            // A window this app owns is already showing the machine.
            if let target = MachineManager.shared.target(forProcessIdentifier: pid) {
                ConnectionRegistry.shared.activateWindow(for: target)
            }
        case .reportInUse(let pid):
            presentError("\"\(description.name)\" is already open in another process (pid \(pid)).")
        case .launch:
            await launch(description: description, machineDirpath: machineDirpath, openMainWindow: openMainWindow)
        }
    }

    @MainActor
    private static func launch(description: MachineDescription,
                               machineDirpath: URL,
                               openMainWindow: @escaping () -> Void) async {
        let result = await PresetManager.shared.launchMachine(
            machineDirpath: machineDirpath.path, model: description.model)
        switch result {
        case .success(let core):
            MachineManager.shared.register(process: core.process, port: core.port,
                                           provenanceUUID: core.provenanceUUID)
            let state = ConnectWindowState.shared
            state.pendingTarget = ConnectionTarget(host: "127.0.0.1", port: core.port)
            state.pendingNeedsRun = true
            state.pendingProvenanceUUID = core.provenanceUUID
            openMainWindow()
            MachineRecents().remember(machineDirpath: machineDirpath)
        case .failure(let error):
            presentError("Beebium could not open \"\(description.name)\": \(error.localizedDescription)")
        }
    }

    @MainActor
    private static func presentError(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "Couldn't Open Machine"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }
}
