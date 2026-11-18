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

import AppKit
import SwiftUI

/// Builds the cassette deck's browse menu as a native `NSMenu` (cassette round
/// 12 follow-up). SwiftUI's `Menu` snapshots its content when it opens and
/// cannot populate a submenu dynamically, so the Input Device picker showed
/// either an empty list (deferred refresh) or asked for the microphone at the
/// deck's render (enumeration in the body), and a click-to-open popup lost the
/// submenu's chevron and kept the parent open. A native menu with a submenu
/// delegate resolves all of it: the submenu carries its chevron, the parent
/// stays open, and its items are enumerated only when it opens.
enum CassetteBrowseMenu {
    /// What the menu items do when chosen. The deck view supplies these; the
    /// menu knows nothing of the client.
    struct Actions {
        let browse: () -> Void
        let insertRecent: (URL) -> Void
        let clearRecent: () -> Void
        let newBlank: (Beebium_BlankTape) -> Void
        let chooseDevice: (LiveAudioDevice) -> Void
    }

    /// The top-level items, in order: Browse..., Recent, New Blank Tape when the
    /// server is local (they need a shared filesystem), then Input Device when
    /// the usage string is present (capture is host-side, so it is offered even
    /// for a remote server). The Input Device item carries a submenu whose
    /// `delegate` populates it on open -- enumerating no devices here, so
    /// building the menu (and so rendering the deck) touches no audio API.
    @MainActor
    static func items(canBrowse: Bool,
                      liveInputAvailable: Bool,
                      recentTapes: [RecentTape],
                      blankTapes: [Beebium_BlankTape],
                      deviceSubmenuDelegate: NSMenuDelegate,
                      actions: Actions) -> [NSMenuItem] {
        var items: [NSMenuItem] = []
        if canBrowse {
            items.append(BlockMenuItem(title: "Browse...") { actions.browse() })
            items.append(recentItem(recentTapes, actions))
            items.append(newBlankItem(blankTapes, actions))
        }
        if liveInputAvailable {
            items.append(inputDeviceItem(delegate: deviceSubmenuDelegate))
        }
        return items
    }

    @MainActor
    private static func recentItem(_ tapes: [RecentTape], _ actions: Actions) -> NSMenuItem {
        let item = NSMenuItem(title: "Recent", action: nil, keyEquivalent: "")
        let submenu = NSMenu(title: "Recent")
        submenu.autoenablesItems = false
        let labels = RecentTapesManager.menuLabels(for: tapes)
        for (tape, label) in zip(tapes, labels) {
            let entry = BlockMenuItem(title: label) {
                actions.insertRecent(URL(fileURLWithPath: tape.filepath))
            }
            entry.isEnabled = FileManager.default.fileExists(atPath: tape.filepath)
            submenu.addItem(entry)
        }
        submenu.addItem(.separator())
        submenu.addItem(BlockMenuItem(title: "Clear Recent Tapes") { actions.clearRecent() })
        item.submenu = submenu
        item.isEnabled = !tapes.isEmpty
        return item
    }

    @MainActor
    private static func newBlankItem(_ blanks: [Beebium_BlankTape], _ actions: Actions) -> NSMenuItem {
        let item = NSMenuItem(title: "New Blank Tape", action: nil, keyEquivalent: "")
        let submenu = NSMenu(title: "New Blank Tape")
        submenu.autoenablesItems = false
        for tape in blanks {
            submenu.addItem(BlockMenuItem(title: tape.label) { actions.newBlank(tape) })
        }
        item.submenu = submenu
        item.isEnabled = !blanks.isEmpty
        return item
    }

    @MainActor
    private static func inputDeviceItem(delegate: NSMenuDelegate) -> NSMenuItem {
        let item = NSMenuItem(title: "Input Device", action: nil, keyEquivalent: "")
        let submenu = NSMenu(title: "Input Device")
        submenu.autoenablesItems = false
        // The delegate fills this on open (menuWillOpen). A static placeholder
        // keeps the submenu non-empty -- so its arrow stays live before the
        // first open -- without enumerating any device here.
        submenu.addItem(placeholderItem())
        submenu.delegate = delegate
        item.submenu = submenu
        return item
    }

    /// The disabled "no devices" row, used before the submenu first opens and
    /// when an open finds no input devices.
    static func placeholderItem() -> NSMenuItem {
        let empty = NSMenuItem(title: "No audio input devices", action: nil, keyEquivalent: "")
        empty.isEnabled = false
        return empty
    }
}

/// Holds the browse control's current state and actions, builds its menu on
/// demand, and is the Input Device submenu's delegate so that submenu is
/// enumerated and rebuilt each time it opens.
@MainActor
final class CassetteBrowseCoordinator: NSObject, NSMenuDelegate {
    var canBrowse = false
    var liveInputAvailable = false
    var recentTapes: [RecentTape] = []
    var blankTapes: [Beebium_BlankTape] = []
    let inputDevices: LiveInputDeviceList
    var actions: CassetteBrowseMenu.Actions

    init(inputDevices: LiveInputDeviceList, actions: CassetteBrowseMenu.Actions) {
        self.inputDevices = inputDevices
        self.actions = actions
    }

    /// A fresh top-level menu for the current state. The Input Device submenu is
    /// empty here; this coordinator fills it on open.
    func makeMenu() -> NSMenu {
        let menu = NSMenu()
        menu.autoenablesItems = false
        for item in CassetteBrowseMenu.items(
            canBrowse: canBrowse, liveInputAvailable: liveInputAvailable,
            recentTapes: recentTapes, blankTapes: blankTapes,
            deviceSubmenuDelegate: self, actions: actions) {
            menu.addItem(item)
        }
        return menu
    }

    // NSMenuDelegate: the Input Device submenu is about to be SHOWN. This fires
    // only when the user actually opens the submenu -- unlike menuNeedsUpdate,
    // which AppKit also calls during menu validation and the key-equivalent
    // search once the control is in a window, and so would enumerate (and ask
    // for the microphone) merely because a machine window opened. Enumerate the
    // host's inputs here and rebuild the items.
    func menuWillOpen(_ menu: NSMenu) {
        menu.removeAllItems()
        for item in InputDeviceMenu.items(from: inputDevices, choose: actions.chooseDevice) {
            menu.addItem(item)
        }
    }

    // NSMenuDelegate: answer the key-equivalent search ourselves (there are no
    // key equivalents in this submenu) so AppKit does not update -- and so does
    // not enumerate -- the submenu while searching for a shortcut's target.
    func menuHasKeyEquivalent(_ menu: NSMenu, for event: NSEvent,
                              target: AutoreleasingUnsafeMutablePointer<AnyObject?>,
                              action: UnsafeMutablePointer<Selector?>) -> Bool {
        false
    }
}

/// A borderless folder control with a disclosure chevron: clicking the folder
/// runs the primary action (Browse where the server is local, else open the
/// menu), clicking the chevron opens the menu. The native equivalent of the
/// SwiftUI button-style `Menu` with a `primaryAction` it replaces.
final class SplitMenuButton: NSView {
    var isControlEnabled = true { didSet { needsDisplay = true } }
    var onPrimary: () -> Void = {}
    var menuProvider: () -> NSMenu? = { nil }

    private let iconWidth: CGFloat = 18
    private let chevronWidth: CGFloat = 12

    override var intrinsicContentSize: NSSize { NSSize(width: iconWidth + chevronWidth, height: 20) }
    override var isFlipped: Bool { false }

    override func draw(_ dirtyRect: NSRect) {
        let colour: NSColor = isControlEnabled
            ? NSColor.labelColor.withAlphaComponent(0.6)
            : NSColor.secondaryLabelColor.withAlphaComponent(0.3)
        draw(symbol: "folder", pointSize: 16,
             in: NSRect(x: 0, y: 0, width: iconWidth, height: bounds.height), colour: colour)
        draw(symbol: "chevron.down", pointSize: 8,
             in: NSRect(x: iconWidth, y: 0, width: chevronWidth, height: bounds.height), colour: colour)
    }

    private func draw(symbol: String, pointSize: CGFloat, in rect: NSRect, colour: NSColor) {
        guard let base = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)?
            .withSymbolConfiguration(.init(pointSize: pointSize, weight: .regular)) else { return }
        let tinted = NSImage(size: base.size, flipped: false) { frame in
            base.draw(in: frame)
            colour.set()
            frame.fill(using: .sourceAtop)
            return true
        }
        let size = tinted.size
        let origin = NSPoint(x: rect.midX - size.width / 2, y: rect.midY - size.height / 2)
        tinted.draw(at: origin, from: .zero, operation: .sourceOver, fraction: 1)
    }

    override func mouseDown(with event: NSEvent) {
        guard isControlEnabled else { return }
        let point = convert(event.locationInWindow, from: nil)
        if point.x >= bounds.width - chevronWidth {
            showMenu()
        } else {
            onPrimary()
        }
    }

    func showMenu() {
        guard let menu = menuProvider() else { return }
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: 0), in: self)
    }

    override func accessibilityRole() -> NSAccessibility.Role? { .menuButton }
    override func accessibilityLabel() -> String? { "Insert tape or choose input" }
}

/// The cassette deck's browse control, a native split menu button so the Input
/// Device submenu is a real AppKit submenu (chevron, parent stays open,
/// enumerated on open). One-click Browse and the Browse, Recent, New Blank Tape
/// and Input Device items keep their order and behaviour from the SwiftUI menu
/// this replaces.
struct CassetteBrowseButton: NSViewRepresentable {
    let isEnabled: Bool
    let canBrowse: Bool
    let liveInputAvailable: Bool
    let recentTapes: [RecentTape]
    let blankTapes: [Beebium_BlankTape]
    let inputDevices: LiveInputDeviceList
    let actions: CassetteBrowseMenu.Actions

    func makeCoordinator() -> CassetteBrowseCoordinator {
        CassetteBrowseCoordinator(inputDevices: inputDevices, actions: actions)
    }

    func makeNSView(context: Context) -> SplitMenuButton {
        let view = SplitMenuButton()
        view.setContentHuggingPriority(.required, for: .horizontal)
        view.setContentHuggingPriority(.required, for: .vertical)
        configure(view, coordinator: context.coordinator)
        return view
    }

    func updateNSView(_ view: SplitMenuButton, context: Context) {
        configure(view, coordinator: context.coordinator)
    }

    private func configure(_ view: SplitMenuButton, coordinator: CassetteBrowseCoordinator) {
        coordinator.canBrowse = canBrowse
        coordinator.liveInputAvailable = liveInputAvailable
        coordinator.recentTapes = recentTapes
        coordinator.blankTapes = blankTapes
        coordinator.actions = actions
        view.isControlEnabled = isEnabled
        view.onPrimary = { [weak view] in
            if canBrowse { actions.browse() } else { view?.showMenu() }
        }
        view.menuProvider = { coordinator.makeMenu() }
    }
}
