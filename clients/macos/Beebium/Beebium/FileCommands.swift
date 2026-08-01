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
    @FocusedValue(\.openNewWindow) private var openNewWindow

    @ObservedObject private var presetManager = PresetManager.shared

    var body: some Commands {
        CommandGroup(replacing: .newItem) {
            Button("New...") {
                appActions.openNewMachine?()
            }
            .keyboardShortcut("n", modifiers: .command)
            .disabled(appActions.openNewMachine == nil)

            Button("New Window") {
                openNewWindow?()
            }
            .keyboardShortcut("n", modifiers: [.command, .shift])
            .disabled(openNewWindow == nil)
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
        panel.allowedContentTypes = [UTType(filenameExtension: "beebium") ?? .data]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.title = "Import Preset"
        panel.message = "Select a preset file to import."

        if panel.runModal() == .OK, let url = panel.url {
            // Parse the preset file to determine which model it's for
            guard let data = FileManager.default.contents(atPath: url.path),
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let model = json["model"] as? String else {
                NSLog("[FileCommands] Failed to parse preset file or missing model field")
                showImportErrorAlert(message: "Invalid preset file. The file must contain a valid model field.")
                return
            }

            // Find a matching system preset to get the executable path
            guard let matchingPreset = presetManager.systemPresets.first(where: {
                $0.coreExecutablePath.contains("beebium-\(model)")
            }) else {
                NSLog("[FileCommands] No matching executable found for model: \(model)")
                showImportErrorAlert(message: "No matching machine core found for model \"\(model)\". The preset cannot be imported.")
                return
            }

            Task { @MainActor in
                let result = await presetManager.importPreset(from: url.path, using: matchingPreset.coreExecutablePath)
                if result == nil {
                    showImportErrorAlert(message: "Failed to import the preset. Check the console for details.")
                }
            }
        }
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

struct HelpCommands: Commands {
    @ObservedObject private var appActions = AppActions.shared

    var body: some Commands {
        CommandGroup(before: .help) {
            Button("Welcome to Beebium") {
                appActions.openWelcome?()
            }
            .disabled(appActions.openWelcome == nil)
            Divider()
        }
    }
}
