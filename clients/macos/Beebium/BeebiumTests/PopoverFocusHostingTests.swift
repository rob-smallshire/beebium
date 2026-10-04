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

import XCTest
import SwiftUI
import AppKit
@testable import Beebium

/// Hosts the REAL rename and station editor content in a window, spins the run
/// loop, and observes state (#153). The popover fields are NSTextView-backed
/// (`PopoverTextField`), so each presentation has its own editor and caret; these
/// tests cover the prefill, the lifecycle and the re-host rate. The caret itself
/// is verified visually by the FocusSelfTest harness (photographs the running
/// app), since a caret cannot be seen from a unit test.
@MainActor
final class PopoverFocusHostingTests: XCTestCase {

    private func host<V: View>(_ view: V,
                               appearance: NSAppearance.Name = .darkAqua,
                               seconds: TimeInterval = 1.0)
        -> (window: NSWindow, textView: NSTextView?) {
        let hosting = NSHostingController(rootView: view)
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 360, height: 220),
            styleMask: [.titled], backing: .buffered, defer: false)
        window.appearance = NSAppearance(named: appearance)
        window.contentViewController = hosting
        NSApp.activate(ignoringOtherApps: true)
        window.makeKeyAndOrderFront(nil)
        window.makeKey()

        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
        }
        return (window, Self.firstTextView(in: window.contentView))
    }

    private static func firstTextView(in view: NSView?) -> NSTextView? {
        guard let view else { return nil }
        if let textView = view as? NSTextView { return textView }
        for subview in view.subviews {
            if let found = firstTextView(in: subview) { return found }
        }
        return nil
    }

    // MARK: - Opens prefilled

    func testStationEditorOpensPrefilledWithConfiguredNumber() {
        let (_, textView) = host(
            StationIdPopover(currentStationId: 9,
                             econetClient: EconetClient(),
                             breakKeyLabel: nil,
                             isPresented: .constant(true)))
        XCTAssertEqual(textView?.string, "9",
                       "station editor must open showing the configured number")
    }

    func testRenameEditorOpensPrefilledWithTemplate() {
        let systemClient = SystemClient()
        var identity = Beebium_MachineIdentity()
        identity.name = "Station 80 (AUN, Model B) #1"
        identity.nameTemplate = "Station {econet-station} (AUN, Model B) {machine-ordinal}"
        systemClient.updateIdentity(identity)

        let (_, textView) = host(
            MachineRenameEditor(systemClient: systemClient, dismiss: {}))
        XCTAssertEqual(textView?.string,
                       "Station {econet-station} (AUN, Model B) {machine-ordinal}",
                       "rename editor must open showing the template")
    }

    func testExtensionEditorFormOpensPrefilled() {
        var textInput = Beebium_TextInput()
        textInput.label = "Map file"
        textInput.value = "stations.aun-map.json"
        var inputControl = Beebium_Control()
        inputControl.id = "map"
        inputControl.textInput = textInput
        var group = Beebium_Group()
        group.controls = [inputControl]
        var root = Beebium_Control()
        root.id = "root"
        root.group = group

        let (_, textView) = host(
            ExtensionEditorForm(editor: root,
                                commitTitle: "Save",
                                showCancel: true,
                                onCancel: {},
                                onCommit: { _ in }))
        XCTAssertEqual(textView?.string, "stations.aun-map.json",
                       "extension editor form must open with its field prefilled")
    }

    // MARK: - Lifecycle across presentations

    private func present<V: View>(_ view: V, in window: NSWindow,
                                  seconds: TimeInterval = 0.8) -> NSTextView? {
        window.contentViewController = NSHostingController(rootView: view)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
        }
        return Self.firstTextView(in: window.contentView)
    }

    private func dismiss(in window: NSWindow, seconds: TimeInterval = 0.4) {
        window.contentViewController = NSHostingController(rootView: EmptyView())
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
        }
    }

    /// Each presentation is its own NSTextView (its own editor and caret), so the
    /// second open is a different object from the first -- the heart of the fix.
    func testEachPresentationIsItsOwnTextView() throws {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 360, height: 220),
            styleMask: [.titled], backing: .buffered, defer: false)

        let first = try XCTUnwrap(present(
            StationIdPopover(currentStationId: 9, econetClient: EconetClient(),
                             breakKeyLabel: nil, isPresented: .constant(true)),
            in: window))
        XCTAssertEqual(first.string, "9", "first open prefilled")
        let firstId = ObjectIdentifier(first)

        dismiss(in: window)
        XCTAssertNil(Self.firstTextView(in: window.contentView),
                     "the field should be gone after dismissal")

        let second = try XCTUnwrap(present(
            StationIdPopover(currentStationId: 9, econetClient: EconetClient(),
                             breakKeyLabel: nil, isPresented: .constant(true)),
            in: window))
        XCTAssertEqual(second.string, "9", "second open also prefilled")
        XCTAssertNotEqual(firstId, ObjectIdentifier(second),
                          "each presentation must be its own text view, not a reused "
                          + "shared field editor")
    }

    // MARK: - Insertion point colour (rule the colour out as a cause)

    func testInsertionPointColourContrasts() throws {
        for appearance in [NSAppearance.Name.darkAqua, .aqua] {
            let (window, textView) = host(
                StationIdPopover(currentStationId: 9,
                                 econetClient: EconetClient(),
                                 breakKeyLabel: nil,
                                 isPresented: .constant(true)),
                appearance: appearance)
            let editor = try XCTUnwrap(textView, "no text view hosted (\(appearance))")
            let insertion = resolve(editor.insertionPointColor, in: window)
            let background = resolve(editor.backgroundColor, in: window)
            XCTAssertNotEqual(editor.insertionPointColor, NSColor.clear,
                              "insertion point must not be clear (\(appearance))")
            XCTAssertFalse(approximatelyEqual(insertion, background),
                           "insertion point must contrast with the background "
                           + "(\(appearance))")
        }
    }

    // MARK: - Invalidation storm guard

    func testIdlePopoverDoesNotReHostTheFieldContinuously() {
        PopoverTextField.updateCountForTesting = 0
        _ = host(StationIdPopover(currentStationId: 9,
                                  econetClient: EconetClient(),
                                  breakKeyLabel: nil,
                                  isPresented: .constant(true)),
                 seconds: 1.0)
        XCTAssertLessThan(PopoverTextField.updateCountForTesting, 10,
                          "idle popover re-hosted the field "
                          + "\(PopoverTextField.updateCountForTesting) times in 1s")
    }

    private func resolve(_ color: NSColor, in window: NSWindow) -> NSColor {
        var resolved = color
        window.appearance?.performAsCurrentDrawingAppearance {
            resolved = color.usingColorSpace(.sRGB) ?? color
        }
        return resolved.usingColorSpace(.sRGB) ?? resolved
    }

    private func approximatelyEqual(_ a: NSColor, _ b: NSColor) -> Bool {
        guard let x = a.usingColorSpace(.sRGB), let y = b.usingColorSpace(.sRGB)
        else { return false }
        let d = abs(x.redComponent - y.redComponent)
              + abs(x.greenComponent - y.greenComponent)
              + abs(x.blueComponent - y.blueComponent)
        return d < 0.05
    }
}
