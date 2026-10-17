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

/// Saving the machine in the focused window: File > Save and File > Save As.
/// Save on an already-saved machine writes in place; on a not-yet-saved machine
/// it runs the first-save panel. Save As always runs the panel. The panel's own
/// Replace is passed to the server as replace_existing -- nothing is deleted
/// here. The window's proxy icon and edited dot follow from SystemClient's
/// published fields, which a successful save updates, so there is nothing to
/// apply to the window here.
enum MachineSaving {

    @MainActor
    static func save(systemClient: SystemClient) async {
        if systemClient.machineSaved {
            await perform(systemClient: systemClient, destinationDirpath: "", replaceExisting: false)
        } else {
            await saveToChosenDirectory(systemClient: systemClient)
        }
    }

    @MainActor
    static func saveAs(systemClient: SystemClient) async {
        await saveToChosenDirectory(systemClient: systemClient)
    }

    @MainActor
    private static func saveToChosenDirectory(systemClient: SystemClient) async {
        guard let url = runSavePanel(machineName: systemClient.machineName) else { return }
        // The panel's own Replace confirmation is the only way an existing path
        // comes back, so an existing destination means the user chose to
        // replace it; pass that to the server, which does the replacing. The
        // app never deletes anything itself.
        let replaceExisting = FileManager.default.fileExists(atPath: url.path)
        await perform(systemClient: systemClient,
                      destinationDirpath: url.path,
                      replaceExisting: replaceExisting)
    }

    @MainActor
    private static func perform(systemClient: SystemClient,
                                destinationDirpath: String,
                                replaceExisting: Bool) async {
        let result = await systemClient.saveMachine(destinationDirpath: destinationDirpath,
                                                    replaceExisting: replaceExisting)
        switch result {
        case .success(let dirpath):
            MachineRecents().remember(machineDirpath: URL(fileURLWithPath: dirpath))
        case .failure(let error):
            presentError(error.localizedDescription)
        }
    }

    @MainActor
    private static func runSavePanel(machineName: String) -> URL? {
        let documentsDirpath = (try? FileManager.default.url(
            for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: false))
            ?? FileManager.default.homeDirectoryForCurrentUser
        let defaults = SaveClient.firstSaveDefaults(machineName: machineName,
                                                    documentsDirpath: documentsDirpath)

        let panel = NSSavePanel()
        panel.allowedContentTypes = [.beebiumMachine]
        panel.directoryURL = defaults.directoryURL
        panel.nameFieldStringValue = defaults.nameFieldStringValue
        panel.canCreateDirectories = true
        panel.title = "Save Machine"
        panel.prompt = "Save"

        guard panel.runModal() == .OK, let url = panel.url else { return nil }
        return url
    }

    @MainActor
    private static func presentError(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "Couldn't Save Machine"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }
}
