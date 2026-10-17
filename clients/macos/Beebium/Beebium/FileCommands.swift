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

import SwiftUI
import AppKit
import UniformTypeIdentifiers

struct FileCommands: Commands {
    /// Renaming needs a machine, so this is nil unless an emulator window has
    /// focus, and the menu item is disabled accordingly. It puts the cursor in
    /// the title bar's name field rather than opening a dialog of its own.
    @FocusedValue(\.renameMachine) private var renameMachine
    @ObservedObject private var appActions = AppActions.shared

    @ObservedObject private var presetManager = PresetManager.shared

    var body: some Commands {
        CommandGroup(replacing: .newItem) {
            // Cmd-N opens the Welcome window (the preset picker). It focuses the
            // existing Welcome window if one is open rather than spawning a
            // second. The custom machine builder is reached from the Welcome
            // window's own "New Machine..." button.
            Button("New...") {
                appActions.showWelcome?()
            }
            .keyboardShortcut("n", modifiers: .command)
            .disabled(appActions.showWelcome == nil)
        }

        CommandGroup(after: .newItem) {
            Button("Open...") { }
            .keyboardShortcut("o", modifiers: .command)
            .disabled(true)

            Menu("Open Recent") {
                Text("No Recent Items")
            }
            .disabled(true)

            Divider()

            Button("Rename Machine...") {
                renameMachine?()
            }
            .keyboardShortcut("r", modifiers: .command)
            .disabled(renameMachine == nil)

            Divider()

            Button("Import Preset...") {
                importPreset()
            }
            .keyboardShortcut("i", modifiers: [.command, .shift])
            .disabled(presetManager.systemPresets.isEmpty)

            Divider()

            Button("Connect...") {
                appActions.openConnect?()
            }
            .disabled(appActions.openConnect == nil)
        }

        CommandGroup(replacing: .saveItem) {
            Button("Save") { }
            .keyboardShortcut("s", modifiers: .command)
            .disabled(true)

            Button("Save As...") { }
            .keyboardShortcut("s", modifiers: [.command, .shift])
            .disabled(true)

            Menu("Revert To") {
                Text("No Saved Versions")
            }
            .disabled(true)
        }
        // Close (Cmd-W) provided automatically by SwiftUI
    }

    private func importPreset() {
        let panel = NSOpenPanel()
        // A .beebium-preset archive, a directory laid out like one, or a lone
        // JSON preset -- several at once. import-preset validates each file and
        // names its model itself, so the client no longer parses the file to
        // pick an executable; any one serves.
        panel.allowedContentTypes = [.beebiumPreset, .json]
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.title = "Import Presets"
        panel.message = "Select presets to import."

        guard panel.runModal() == .OK, !panel.urls.isEmpty else { return }
        guard let executablePath = presetManager.anyCoreExecutablePath else {
            showImportErrorAlert(message: "No machine core is available to import presets.")
            return
        }

        let filepaths = panel.urls.map(\.path)
        Task { @MainActor in
            let failures = await presetManager.importPresets(from: filepaths, using: executablePath)
            if let message = Self.importFailureMessage(failures: failures, total: filepaths.count) {
                showImportErrorAlert(message: message)
            }
        }
    }

    /// The alert message for an import of `total` files of which `failures`
    /// failed, or nil when none did. A pure seam so the wording is testable.
    static func importFailureMessage(failures: [String], total: Int) -> String? {
        guard !failures.isEmpty else { return nil }
        let list = failures.joined(separator: ", ")
        if failures.count == 1 {
            return "Could not import \(list). Check the console for details."
        }
        return "Could not import \(failures.count) of \(total) presets: \(list). Check the console for details."
    }

    private func showImportErrorAlert(message: String) {
        let alert = NSAlert()
        alert.messageText = "Import Failed"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }
}

