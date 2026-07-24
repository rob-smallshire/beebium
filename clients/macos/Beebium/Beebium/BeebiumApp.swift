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

// MARK: - App Delegate

/// Handles app quit by delegating to MachineManager for shutdown decisions.
///
/// Quit rule: shut down a core only if BOTH (a) this app launched it AND (b) it's the
/// sole client. Everything else is silently disconnected. No quit dialog ever.
class BeebiumAppDelegate: NSObject, NSApplicationDelegate {
    /// Titles of Window scene entries to hide in the Window menu.
    private static let hiddenWindowMenuTitles: Set<String> = ["New Machine", "Connect to Machine"]

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Tooltips drive much of Beebium's secondary information (per-row
        // socket details in the Memory sidebar, parsed ROM kinds, slot
        // numbers, source filepaths). macOS's default tooltip delay is
        // around 1.5 s, which is far too long for the discoverability
        // role tooltips play here - users hover, expect to see the info
        // quickly, and move on. Drop it to 400 ms app-wide. Note this is
        // an undocumented-but-stable AppKit user default key.
        UserDefaults.standard.set(400, forKey: "NSInitialToolTipDelay")

        // Beebium doesn't use tabbed windows. Disable macOS automatic window
        // tabbing to remove the Show Previous/Next Tab and Merge All Windows
        // items from the Window menu.
        NSWindow.allowsAutomaticWindowTabbing = false

        // SwiftUI automatically adds a Window menu entry for every Window scene,
        // with no API to opt out (macOS 13/14). Our dialog windows (New Machine,
        // Connect to Machine) are already accessible via the File menu, so these
        // entries are redundant and confusing.
        //
        // Workaround: observe NSMenu.didAddItemNotification to catch items as
        // SwiftUI adds them. We can't use a menu delegate because SwiftUI owns
        // the windowsMenu delegate and may replace it. We hide items rather than
        // remove them — removing triggers didAddItemNotification recursively as
        // AppKit rebuilds the item array, causing a stack overflow.
        NotificationCenter.default.addObserver(
            forName: NSMenu.didAddItemNotification,
            object: nil,
            queue: .main
        ) { notification in
            guard let menu = notification.object as? NSMenu,
                  menu == NSApp.windowsMenu else { return }
            for item in menu.items where Self.hiddenWindowMenuTitles.contains(item.title) {
                item.isHidden = true
            }
        }

        // SwiftUI can only place a command group before or after a whole
        // standard group, and .pasteboard is all of Cut/Copy/Paste/Delete/
        // Select All -- so "Paste at Full Speed" lands after Select All,
        // stranded from the Paste it belongs with. Replacing .pasteboard
        // outright would put it in the right place but cost Cut, Copy, Delete
        // and Select All their AppKit validation, which they need in the app's
        // text fields. Moving the one item afterwards is the smaller price.
        NotificationCenter.default.addObserver(
            forName: NSMenu.didAddItemNotification,
            object: nil,
            queue: .main
        ) { notification in
            guard let menu = notification.object as? NSMenu else { return }
            Self.placeFullSpeedPasteBelowPaste(in: menu)
            Self.placeCopyVariantsBelowCopy(in: menu)
        }
    }

    /// The two copy variants that must sit together directly beneath the
    /// standard Copy. SwiftUI drops them after the whole pasteboard group
    /// (below Select All), stranded from the Copy they belong with; this pulls
    /// them back up. Matched by title because they are ours and never localised.
    ///
    /// "Mode 7 Copies As" is not among them: it is not a copy command but a
    /// setting the copies obey, so it lives in its own section lower down rather
    /// than crowding the Copy it governs.
    private static let copySectionTitles = [
        "Copy as Columns", "Copy Text from Graphics",
    ]

    /// Guards against the reorderings below re-entering through the very
    /// notification that triggered them.
    private static var isReorderingPasteItems = false
    private static var isReorderingCopyItems = false

    private static func placeFullSpeedPasteBelowPaste(in menu: NSMenu) {
        guard !isReorderingPasteItems else { return }

        // Our own item is matched by title because we set it and never
        // localise it; Paste is matched by selector so it survives any
        // localisation.
        guard let fullSpeedIndex = menu.items.firstIndex(
                  where: { $0.title == "Paste at Full Speed" }),
              let pasteIndex = menu.items.firstIndex(
                  where: { $0.action == #selector(NSText.paste(_:)) }),
              fullSpeedIndex != pasteIndex + 1
        else { return }

        isReorderingPasteItems = true
        defer { isReorderingPasteItems = false }

        let item = menu.items[fullSpeedIndex]
        menu.removeItem(at: fullSpeedIndex)
        menu.insertItem(item, at: pasteIndex + 1)
    }

    private static func placeCopyVariantsBelowCopy(in menu: NSMenu) {
        guard !isReorderingCopyItems else { return }

        // Copy is matched by selector so it survives localisation; the variants
        // by their (unlocalised) titles.
        let variants = copySectionTitles.compactMap { title in
            menu.items.first(where: { $0.title == title })
        }
        guard variants.count == copySectionTitles.count,
              let copyIndex = menu.items.firstIndex(
                  where: { $0.action == #selector(NSText.copy(_:)) })
        else { return }

        // Already grouped in order immediately below Copy? Then nothing to do,
        // which is also what stops this re-entering endlessly.
        let wanted = Array(menu.items[
            (copyIndex + 1)..<min(copyIndex + 1 + variants.count, menu.items.count)])
        if wanted == variants { return }

        isReorderingCopyItems = true
        defer { isReorderingCopyItems = false }

        for item in variants {
            if let index = menu.items.firstIndex(of: item) {
                menu.removeItem(at: index)
            }
        }
        // Copy's index can have shifted as the variants were pulled out.
        guard let anchor = menu.items.firstIndex(
                  where: { $0.action == #selector(NSText.copy(_:)) })
        else { return }
        for (offset, item) in variants.enumerated() {
            menu.insertItem(item, at: anchor + 1 + offset)
        }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        let machineManager = MachineManager.shared
        let action = machineManager.quitAction()

        switch action {
        case .terminateNow:
            return .terminateNow
        case .shutdownThenTerminate(let machineIds):
            // Terminate all sole-client servers via SIGTERM, then allow quit
            machineManager.terminateServers(ids: machineIds)
            return .terminateNow
        }
    }
}

@main
struct BeebiumApp: App {
    @NSApplicationDelegateAdaptor(BeebiumAppDelegate.self) var appDelegate
    @FocusedBinding(\.showStatusBar) private var showStatusBar
    @FocusedBinding(\.showSidebar) private var showSidebar
    @FocusedBinding(\.sidebarMode) private var sidebarMode
    @FocusedBinding(\.isImmersive) private var isImmersive
    @FocusedValue(\.pasteCoordinator) private var pasteCoordinator
    @FocusedValue(\.selectionCoordinator) private var selectionCoordinator
    @StateObject private var keyboardMappingManager = KeyboardMappingManager()
    @StateObject private var connectWindowState = ConnectWindowState.shared
    @Environment(\.openWindow) private var openWindow

    var body: some Scene {
        WindowGroup("Beebium", id: "main") {
            MainWindowRouter(
                keyboardMappingManager: keyboardMappingManager
            )
            .onOpenURL { url in
                handleDeepLink(url)
            }
        }
        .windowToolbarStyle(.unified)
        .commands {
            FileCommands()
            HelpCommands()
            CommandGroup(after: .toolbar) {
                Button(showStatusBar == true ? "Hide Status Bar" : "Show Status Bar") {
                    showStatusBar?.toggle()
                }
                .keyboardShortcut("/", modifiers: .command)
                .disabled(showStatusBar == nil)

                Button(isImmersive == true ? "Exit Immersive Mode" : "Enter Immersive Mode") {
                    isImmersive?.toggle()
                }
                .keyboardShortcut("f", modifiers: [.command, .shift])
                .disabled(isImmersive == nil)
            }
            CommandGroup(before: .sidebar) {
                Button(showSidebar == true ? "Hide Sidebar" : "Show Sidebar") {
                    withAnimation { showSidebar?.toggle() }
                }
                .keyboardShortcut("s", modifiers: [.control, .command])
                .disabled(showSidebar == nil)
            }
            CommandGroup(after: .sidebar) {
                ForEach(SidebarMode.allCases) { mode in
                    Button(mode.label) {
                        if showSidebar == true && sidebarMode == mode {
                            // Same mode is already showing; second press hides.
                            withAnimation { showSidebar = false }
                        } else {
                            // Either hidden, or showing a different mode.
                            sidebarMode = mode
                            if showSidebar == false {
                                withAnimation { showSidebar = true }
                            }
                        }
                    }
                    .keyboardShortcut(mode.shortcutKey, modifiers: .command)
                    .disabled(sidebarMode == nil)
                }
            }
            // The Edit menu keeps its standard groups. They look inapplicable
            // in a machine window, but the app has real text fields -- the
            // Connect dialog, the settings and configuration panes -- whose
            // field editor is an NSTextView, and there Undo, Redo, Cut, Copy,
            // Select All, Spelling and Substitutions all genuinely work.
            // AppKit already greys them out per first responder, which is
            // exactly the wanted behaviour, so there is nothing to remove.

            // Sits next to the stock Paste, following the same pattern as
            // "Paste and Match Style": a paste variant with its own item and
            // its own modifier. Kept permanently visible rather than revealed
            // by Option, because nobody goes looking for a feature they do not
            // know exists -- they paste a long listing, watch it crawl, and
            // conclude that pasting is slow.
            // The aligned-rows Copy is the standard Edit > Copy (Cmd-C),
            // handled by the machine view's responder. These two are the other
            // interpretations, discoverable in the menu with their shortcuts:
            // holding the modifier previews the highlight, pressing C commits
            // it. placeCopyVariantsBelowCopy pulls them up to sit under Copy,
            // which SwiftUI cannot do (it can only place a group after the whole
            // pasteboard group).
            CommandGroup(after: .pasteboard) {
                Button("Copy as Columns") {
                    guard let coordinator = selectionCoordinator else { return }
                    Task { @MainActor in await coordinator.copy(.rectangle) }
                }
                .keyboardShortcut("c", modifiers: [.option, .command])
                .disabled(selectionCoordinator == nil)

                Button("Copy Text from Graphics") {
                    guard let coordinator = selectionCoordinator else { return }
                    Task { @MainActor in await coordinator.copy(.anywhere) }
                }
                .keyboardShortcut("c", modifiers: [.shift, .command])
                .disabled(selectionCoordinator == nil)
            }

            CommandGroup(after: .pasteboard) {
                Button("Paste at Full Speed") {
                    guard let coordinator = pasteCoordinator,
                          let text = NSPasteboard.general.string(forType: .string),
                          !text.isEmpty else { return }
                    Task { @MainActor in
                        await coordinator.paste(text, pace: .fullSpeed)
                    }
                }
                .keyboardShortcut("v", modifiers: [.option, .command])
                .disabled(pasteCoordinator?.canPasteAtFullSpeed != true)
            }

            // Which meaning a MODE 7 byte carries when it is copied, in its own
            // section near the bottom of the menu -- as Safari groups its
            // settings-like items -- rather than beside the copy commands. It is
            // a setting the copies obey, not a fourth thing to copy, so it reads
            // better set apart from them than crowded among them.
            //
            // A submenu, and a Picker within it rather than a pair of Buttons.
            // The choice is one of two states, which is what a Picker models and
            // what AppKit draws the radio checkmark for; a pair of Buttons would
            // carry the checkmark in the title, and a menu item whose title
            // changes is a different item, so every toggle would replace the
            // whole submenu instead of updating it. It is declared before the
            // keyboard-mapping group so it sits above it.
            CommandGroup(after: .textEditing) {
                Divider()
                Mode7CopiesAsMenu()
            }

            CommandGroup(after: .textEditing) {
                Divider()
                if let target = keyboardMappingManager.toggleTargetMapping {
                    Button("Switch to \(target.name) Keyboard Mapping") {
                        keyboardMappingManager.toggleToDefaultLogical()
                    }
                    .keyboardShortcut("k", modifiers: .command)
                } else {
                    Button("Switch to Previous Keyboard Mapping") {
                    }
                    .keyboardShortcut("k", modifiers: .command)
                    .disabled(true)
                }
            }
        }

        // New Machine dialog window (singleton, opened from Welcome Window's "New Machine...")
        Window("New Machine", id: "new-machine") {
            NewMachineDialog()
        }
        // contentMinSize (not contentSize) so the window can be made taller to
        // show more of the sideways ROM/RAM list, with the content as a floor.
        .windowResizability(.contentMinSize)
        .defaultPosition(.center)

        // Connect window (singleton, non-modal)
        Window("Connect to Machine", id: "connect") {
            ConnectWindowContent()
        }
        .windowResizability(.contentSize)
        .defaultPosition(.center)

        // Settings window (singleton, accessed via Cmd+, or Beebium menu)
        Settings {
            SettingsView()
        }
    }

    /// Handle a `beebium://connect?host=...&port=...` deep link by attaching a
    /// window to the given server, bypassing Bonjour discovery and the Connect
    /// dialog. Used by automated soak drivers to point the frontend at a server
    /// on an ephemeral, un-advertised port; also usable interactively via
    /// `open "beebium://connect?host=localhost&port=48875"`.
    @MainActor
    private func handleDeepLink(_ url: URL) {
        guard let request = DeepLink.parseConnect(url) else { return }

        // If a window is already connected to this target, just bring it
        // forward rather than opening a duplicate. This also makes a
        // double-delivered URL idempotent.
        if ConnectionRegistry.shared.activateWindow(for: request.target) {
            return
        }

        // Set the pending target the way the Connect dialog does, then open a
        // fresh main window whose MainWindowRouter.onAppear consumes it.
        let state = ConnectWindowState.shared
        state.pendingTarget = request.target
        state.pendingNeedsRun = request.needsRun
        state.pendingProvenanceUUID = request.provenanceUUID
        openWindow(id: "main")
    }
}

/// The Edit menu's `Mode 7 Copies As` submenu: which meaning a MODE 7 byte
/// carries when it is copied.
///
/// Named for the screen mode, not for teletext. Teletext is the broadcast data
/// service, and the Acorn Teletext Adapter -- a 1 MHz bus peripheral with a TV
/// tuner -- is a thing this emulator may one day have. Spending the word on a
/// screen-mode concern here would leave that peripheral nowhere to stand.
///
/// A preference rather than a command: it is not about a particular window, so
/// it reads and writes the defaults directly and no focused value comes into
/// it. The coordinator reads the same defaults when it copies, so nothing has
/// to be pushed into a window from here.
///
/// A Picker and not a pair of Buttons. The choice is one of two states, which
/// is what a Picker models and what AppKit draws the radio checkmark for;
/// building it from Buttons means putting the checkmark in the title, and a
/// menu item whose title changes is a different item.
private struct Mode7CopiesAsMenu: View {
    @AppStorage(ScreenTextCharactersMode.defaultsKey)
    private var characters: String = ScreenTextCharactersMode.codes.rawValue

    var body: some View {
        Picker("Mode 7 Copies As", selection: $characters) {
            Text("Character Codes")
                .tag(ScreenTextCharactersMode.codes.rawValue)

            Text("Displayed Characters")
                .tag(ScreenTextCharactersMode.displayed.rawValue)
        }
    }
}
