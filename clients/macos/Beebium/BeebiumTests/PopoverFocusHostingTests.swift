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
/// loop, and observes state -- so the "opens empty" regression (defect a) and
/// the caret question (defect b) are checked by assertion rather than by eye
/// (#153). Defect (a) is deterministic; defect (b) depends on the test host
/// actually making the window key, which these tests check and report.
@MainActor
final class PopoverFocusHostingTests: XCTestCase {

    /// Host `view` in a titled window, try to make it key and active, spin the
    /// run loop, and return the window plus the first NSTextField found in it.
    private func host<V: View>(_ view: V,
                               appearance: NSAppearance.Name = .darkAqua,
                               seconds: TimeInterval = 1.0)
        -> (window: NSWindow, field: NSTextField?) {
        let hosting = NSHostingController(rootView: view)
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 360, height: 220),
            styleMask: [.titled], backing: .buffered, defer: false)
        window.appearance = NSAppearance(named: appearance)
        window.contentViewController = hosting
        // Try to activate and key the window. The test host is usually not
        // frontmost during `xcodebuild test`, so this does not always succeed --
        // the caret test checks what is observable regardless (see its comment).
        NSApp.activate(ignoringOtherApps: true)
        window.makeKeyAndOrderFront(nil)
        window.makeKey()

        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
        }
        return (window, Self.firstTextField(in: window.contentView))
    }

    private static func firstTextField(in view: NSView?) -> NSTextField? {
        guard let view else { return nil }
        if let field = view as? NSTextField { return field }
        for subview in view.subviews {
            if let found = firstTextField(in: subview) { return found }
        }
        return nil
    }

    // MARK: - Defect (a): opens prefilled

    func testStationEditorOpensPrefilledWithConfiguredNumber() {
        let (_, field) = host(
            StationIdPopover(currentStationId: 9,
                             econetClient: EconetClient(),
                             breakKeyLabel: nil,
                             isPresented: .constant(true)))
        XCTAssertEqual(field?.stringValue, "9",
                       "station editor must open showing the configured number")
    }

    func testRenameEditorOpensPrefilledWithTemplate() {
        let systemClient = SystemClient()
        var identity = Beebium_MachineIdentity()
        identity.name = "Station 80 (AUN, Model B) #1"
        identity.nameTemplate = "Station {econet-station} (AUN, Model B) {machine-ordinal}"
        systemClient.updateIdentity(identity)

        let (_, field) = host(
            MachineRenameEditor(systemClient: systemClient, dismiss: {}))
        XCTAssertEqual(field?.stringValue,
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

        let (_, field) = host(
            ExtensionEditorForm(editor: root,
                                commitTitle: "Save",
                                showCancel: true,
                                onCancel: {},
                                onCommit: { _ in }))
        XCTAssertEqual(field?.stringValue, "stations.aun-map.json",
                       "extension editor form must open with its field prefilled")
    }

    // MARK: - Per-presentation field editor (the "first open only" caret)

    /// Present content in `window` (reused across presentations), spin, and return
    /// the first NSTextField. Models the real bug condition: the same window's
    /// shared field editor would be reused unless each presentation vends its own.
    private func present<V: View>(_ view: V, in window: NSWindow,
                                  seconds: TimeInterval = 0.8) -> NSTextField? {
        window.contentViewController = NSHostingController(rootView: view)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
        }
        return Self.firstTextField(in: window.contentView)
    }

    private func dismiss(in window: NSWindow, seconds: TimeInterval = 0.4) {
        window.contentViewController = NSHostingController(rootView: EmptyView())
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
        }
    }

    // The fix gives each presentation its own field editor via a window-delegate
    // provider. Whether that editor becomes first responder depends on the window
    // being key, which this test host cannot arrange -- so the EDITOR IDENTITY is
    // tested at the provider level (deterministic), and the hosted test below only
    // guards the lifecycle (prefill on every open, clean dismissal).
    func testDedicatedProviderGivesEachFieldItsOwnEditor() {
        let fieldA = NSTextField()
        let fieldB = NSTextField()
        let providerA = DedicatedFieldEditorProvider()
        providerA.field = fieldA
        let providerB = DedicatedFieldEditorProvider()
        providerB.field = fieldB
        let window = NSWindow()

        // Each provider vends its own editor for its own field...
        let editorA = providerA.windowWillReturnFieldEditor(window, to: fieldA) as? NSTextView
        let editorB = providerB.windowWillReturnFieldEditor(window, to: fieldB) as? NSTextView
        XCTAssertNotNil(editorA)
        XCTAssertNotNil(editorB)
        XCTAssertFalse(editorA === editorB,
                       "a second presentation must get a different field editor")
        // ...and declines (forwards) for a field that is not its own.
        XCTAssertNil(providerA.windowWillReturnFieldEditor(window, to: fieldB),
                     "provider should not vend its editor for another field")
    }

    func testStationEditorPresentsTwicePrefilledAndDismissesCleanly() throws {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 360, height: 220),
            styleMask: [.titled], backing: .buffered, defer: false)

        let field1 = try XCTUnwrap(present(
            StationIdPopover(currentStationId: 9, econetClient: EconetClient(),
                             breakKeyLabel: nil, isPresented: .constant(true)),
            in: window))
        XCTAssertEqual(field1.stringValue, "9", "first open prefilled")

        dismiss(in: window)
        XCTAssertNil(Self.firstTextField(in: window.contentView),
                     "the field should be gone after dismissal")

        let field2 = try XCTUnwrap(present(
            StationIdPopover(currentStationId: 9, econetClient: EconetClient(),
                             breakKeyLabel: nil, isPresented: .constant(true)),
            in: window))
        XCTAssertEqual(field2.stringValue, "9", "second open also prefilled")
    }

    // MARK: - Defect (b): caret / key window

    /// The focus path installs the field editor (firstResponder is an NSTextView
    /// whose delegate is our field) even in this non-key test host, so the editor
    /// and its insertion-point colour are observable here -- which rules the
    /// colour in or out as a cause of the missing caret. Whether the window is
    /// key, and whether the insertion point actually blinks, is NOT observable in
    /// this environment (the test app is not activated); that part falls back to
    /// the user's BEEBIUM_DEBUG_FOCUS log.
    func testFieldEditorInstalledAndInsertionPointVisible() throws {
        for appearance in [NSAppearance.Name.darkAqua, .aqua] {
            let (window, field) = host(
                StationIdPopover(currentStationId: 9,
                                 econetClient: EconetClient(),
                                 breakKeyLabel: nil,
                                 isPresented: .constant(true)),
                appearance: appearance)
            let textField = try XCTUnwrap(field, "no text field hosted (\(appearance))")

            let editor = try XCTUnwrap(
                window.firstResponder as? NSTextView,
                "field editor should be installed as first responder (\(appearance))")
            XCTAssertTrue(editor.delegate === textField,
                          "field editor's delegate should be our field (\(appearance))")

            let insertion = resolve(editor.insertionPointColor, in: window)
            let background = resolve(textField.backgroundColor ?? .textBackgroundColor,
                                     in: window)
            NSLog("[CARET] appearance=%@ insertionPoint=%@ background=%@",
                  appearance.rawValue, "\(insertion)", "\(background)")
            XCTAssertNotEqual(editor.insertionPointColor, NSColor.clear,
                              "insertion point must not be clear (\(appearance))")
            XCTAssertFalse(approximatelyEqual(insertion, background),
                           "insertion point must contrast with the field background "
                           + "(\(appearance))")
        }
    }

    // MARK: - Invalidation storm (the "first open only" caret)

    func testIdlePopoverDoesNotReHostTheFieldContinuously() {
        PopoverTextField.updateCountForTesting = 0
        _ = host(StationIdPopover(currentStationId: 9,
                                  econetClient: EconetClient(),
                                  breakKeyLabel: nil,
                                  isPresented: .constant(true)),
                 seconds: 1.0)
        // A static popover settles: a handful of updateNSView calls, not dozens.
        // A high number means something is invalidating the window tree under the
        // field (the storm that stops the caret drawing).
        XCTAssertLessThan(PopoverTextField.updateCountForTesting, 10,
                          "idle popover re-hosted the field "
                          + "\(PopoverTextField.updateCountForTesting) times in 1s")
    }

    func testRapidObservedPublishesReHostTheField() {
        // Mechanism: an object the popover observes (here EconetClient) publishing
        // rapidly re-hosts the field's updateNSView -- exactly what a background
        // invalidation storm does, and the suspected cause of the vanishing caret.
        let econet = EconetClient()
        let hosting = NSHostingController(
            rootView: StationIdPopover(currentStationId: 9,
                                       econetClient: econet,
                                       breakKeyLabel: nil,
                                       isPresented: .constant(true)))
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 360, height: 220),
            styleMask: [.titled], backing: .buffered, defer: false)
        window.contentViewController = hosting
        window.makeKeyAndOrderFront(nil)
        RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.3))

        PopoverTextField.updateCountForTesting = 0
        var status = Beebium_GetEconetStatusResponse()
        status.hasEconetSocket_p = true
        status.stationID = 9
        for tick in 0..<30 {
            status.stationInForce = UInt32(9 + tick % 2)   // vary so nothing dedups
            econet.handleStatusUpdate(status, generation: econet.streamGeneration)
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
        }
        XCTAssertGreaterThan(PopoverTextField.updateCountForTesting, 10,
                             "rapid observed publishes should re-host the field "
                             + "(got \(PopoverTextField.updateCountForTesting))")
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
