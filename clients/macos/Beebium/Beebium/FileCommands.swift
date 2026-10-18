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
    /// focus, and the menu item is disabled accordingly. It renames a not-yet-
    /// saved machine through a modest sheet; a saved machine is renamed from the
    /// title bar's own document popover instead.
    @FocusedValue(\.renameMachine) private var renameMachine
    @FocusedValue(\.machineSaveActions) private var machineSaveActions
    @ObservedObject private var appActions = AppActions.shared

    @ObservedObject private var presetManager = PresetManager.shared

    var body: some Commands {
        CommandGroup(replacing: .newItem) {
            // Cmd-N opens the Welcome window on its Presets page -- the presets
            // and the custom builder -- always, never the saved-machines list
            // (section 4.8). It focuses the existing Welcome window if one is open
            // rather than spawning a second, switching it to Presets. The custom
            // machine builder is reached from the Welcome window's own "New
            // Machine..." button.
            Button("New...") {
                appActions.showWelcome?(WelcomePage.page(for: .new))
            }
            .keyboardShortcut("n", modifiers: .command)
            .disabled(appActions.showWelcome == nil)
        }

        CommandGroup(after: .newItem) {
            Button("Open...") { openMachine() }
            .keyboardShortcut("o", modifiers: .command)
            .disabled(presetManager.systemPresets.isEmpty)

            // A single item, not a submenu: File > Open Recent opens the Welcome
            // window on its Machines page, which previews each machine -- a
            // better recents list than a menu of names (section 4.8). The
            // standard recents submenu is not used; the Machines page is the
            // recents view, and Clear is a button there.
            Button("Open Recent") { appActions.showWelcome?(WelcomePage.page(for: .openRecent)) }
            .disabled(appActions.showWelcome == nil || !hasRecentMachines)

            Divider()

            Button("Rename Machine...") {
                renameMachine?()
            }
            .keyboardShortcut("r", modifiers: .command)
            .disabled(!Self.renameEnabled(
                hasFocusedMachineWindow: renameMachine != nil,
                isSaved: machineSaveActions?.isSaved ?? false))

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
            // Save and Save As act on the machine in the focused window; they are
            // disabled with no machine focused. Save As needs an already-saved
            // machine, since it duplicates one into a new directory.
            Button("Save") { machineSaveActions?.save() }
            .keyboardShortcut("s", modifiers: .command)
            .disabled(!Self.saveEnabled(hasFocusedMachineWindow: machineSaveActions != nil))

            Button("Save As...") { machineSaveActions?.saveAs() }
            .keyboardShortcut("s", modifiers: [.command, .shift])
            .disabled(!Self.saveAsEnabled(
                hasFocusedMachineWindow: machineSaveActions != nil,
                isSaved: machineSaveActions?.isSaved ?? false))

            Menu("Revert To") {
                Text("No Saved Versions")
            }
            .disabled(true)
        }
        // Close (Cmd-W) provided automatically by SwiftUI
    }

    /// Whether File > Save should be enabled: a machine window is focused, so
    /// there is a machine -- transient or saved -- to save. Pure, so the rule is
    /// testable; it is wired to the action with the SaveMachine RPC.
    static func saveEnabled(hasFocusedMachineWindow: Bool) -> Bool {
        hasFocusedMachineWindow
    }

    /// Whether File > Save As... should be enabled: a machine window is focused
    /// and its machine is already saved, since Save As duplicates a saved
    /// machine into a new directory with a new UUID (section 4.8). Pure.
    static func saveAsEnabled(hasFocusedMachineWindow: Bool, isSaved: Bool) -> Bool {
        hasFocusedMachineWindow && isSaved
    }

    /// Whether File > Rename Machine... should be enabled: a not-yet-saved
    /// machine window is focused. A saved machine renames through the title bar's
    /// own document popover (the standard way), so the menu item is only for the
    /// transient machine that has no popover yet. Pure, so it is testable.
    static func renameEnabled(hasFocusedMachineWindow: Bool, isSaved: Bool) -> Bool {
        hasFocusedMachineWindow && !isSaved
    }

    /// Whether there are saved machines to revisit, gating File > Open Recent.
    private var hasRecentMachines: Bool {
        !MachineRecents().machineDirpaths.isEmpty
    }

    /// Open a saved machine chosen from an open panel. The panel offers
    /// `.beebium-machine` documents (the macOS package) and also plain folders,
    /// since a machine directory renamed or stripped of its suffix still opens.
    private func openMachine() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.beebiumMachine]
        panel.allowsMultipleSelection = false
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.treatsFilePackagesAsDirectories = false
        panel.title = "Open Machine"
        panel.message = "Choose a saved Beebium machine."

        guard panel.runModal() == .OK, let url = panel.url else { return }
        appActions.requestOpenSavedMachine(url)
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

