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

    /// Whether closing a window should first offer to save: the machine has
    /// written to template media that a shutdown would lose, and this close
    /// shuts the machine down rather than merely disconnecting from one that
    /// keeps running. Pure, so the rule is testable. (The server only reports
    /// template_media_written while a machine is not yet saved, so this does
    /// not fire for a saved machine whose media are its own.)
    static func shouldPromptToSaveOnClose(closeAction: WindowCloseAction,
                                          templateMediaWritten: Bool) -> Bool {
        templateMediaWritten && closeAction == .shutdownServer
    }

    /// The content of the save-on-close prompt, which depends on whether the
    /// machine can be saved at all. A machine on a local server is offered Save;
    /// one on a remote server cannot be saved from here (#263), so the prompt
    /// only lets the user discard the unsaved work or cancel, and says why. Pure,
    /// so both variants are testable without a window.
    struct SaveOnClosePrompt: Equatable {
        let messageText: String
        let informativeText: String
        /// Whether the prompt offers Save (Save.../Cancel/Don't Save). When false
        /// it offers Close/Cancel only, so no save can be issued from it.
        let offersSave: Bool
    }

    static func saveOnClosePrompt(machineName: String, isServerLocal: Bool) -> SaveOnClosePrompt {
        let name = machineName.isEmpty ? "this machine" : "\"\(machineName)\""
        if isServerLocal {
            return SaveOnClosePrompt(
                messageText: "Do you want to save \(name)?",
                informativeText: "This machine has written to its discs. If you don't save it, that work is lost when it closes.",
                offersSave: true)
        }
        return SaveOnClosePrompt(
            messageText: "Close \(name) without saving?",
            informativeText: "This machine has written to its discs, but the server is on another host, so it can't be saved from here. That work is lost when it closes.",
            offersSave: false)
    }

    /// Returns whether the machine was saved: false if the user cancelled the
    /// save panel or the save failed, so a caller closing the window can leave
    /// it open instead.
    @MainActor @discardableResult
    static func save(systemClient: SystemClient) async -> Bool {
        if systemClient.machineSaved {
            return await perform(systemClient: systemClient, destinationDirpath: "", replaceExisting: false)
        }
        return await saveToChosenDirectory(systemClient: systemClient)
    }

    @MainActor @discardableResult
    static func saveAs(systemClient: SystemClient) async -> Bool {
        await saveToChosenDirectory(systemClient: systemClient)
    }

    @MainActor
    private static func saveToChosenDirectory(systemClient: SystemClient) async -> Bool {
        guard let url = runSavePanel(machineName: systemClient.machineName) else { return false }
        // The panel's own Replace confirmation is the only way an existing path
        // comes back, so an existing destination means the user chose to
        // replace it; pass that to the server, which does the replacing. The
        // app never deletes anything itself.
        let replaceExisting = FileManager.default.fileExists(atPath: url.path)
        return await perform(systemClient: systemClient,
                             destinationDirpath: url.path,
                             replaceExisting: replaceExisting)
    }

    @MainActor
    private static func perform(systemClient: SystemClient,
                                destinationDirpath: String,
                                replaceExisting: Bool) async -> Bool {
        let result = await systemClient.saveMachine(destinationDirpath: destinationDirpath,
                                                    replaceExisting: replaceExisting)
        switch result {
        case .success(let dirpath):
            MachineRecents().remember(machineDirpath: URL(fileURLWithPath: dirpath))
            return true
        case .failure(let error):
            presentError(error.localizedDescription)
            return false
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
