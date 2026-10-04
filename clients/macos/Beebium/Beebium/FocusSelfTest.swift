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
import GRPC
import NIOCore
import NIOPosix
import SwiftUI

/// A self-driving visual check for the popover caret and the restyled field
/// (#153). The caret cannot be seen from a unit test (a caret blinks and the test
/// host cannot make a window key), so this drives the REAL popovers in a running,
/// activated app, synthesises a real click into each field, photographs each
/// popover's own window several times across the blink, and writes PNGs to look
/// at. Enabled only when BEEBIUM_DEBUG_FOCUS_SELFTEST names an output directory.
///
/// BEEBIUM_DEBUG_FOCUS_SELFTEST_PORT, when set, is a running server's port; the
/// rename popover is then shown against a real SystemClient so its placeholder
/// chips and live preview load (the picker needs ListNamePlaceholders).
@MainActor
final class FocusSelfTest {
    static func startIfRequested() {
        guard let dir = ProcessInfo.processInfo.environment["BEEBIUM_DEBUG_FOCUS_SELFTEST"],
              !dir.isEmpty else { return }
        NSLog("[SELFTEST] startIfRequested dir=%@", dir)
        let test = FocusSelfTest(outputDirpath: dir)
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { test.start() }
    }

    private let outputDirpath: String
    private var hostWindow: NSWindow?
    private let econetClient = EconetClient()
    private let localSystemClient = SystemClient()
    private var serverSystemClient: SystemClient?
    private var eventLoopGroup: EventLoopGroup?
    private let framesPerOpen = 8
    private let frameInterval: TimeInterval = 0.1

    init(outputDirpath: String) { self.outputDirpath = outputDirpath }

    private func start() {
        NSLog("[SELFTEST] start()")
        Task { @MainActor in
            await runAll()
            NSLog("[SELFTEST] done; PNGs in %@", outputDirpath)
            NSApp.terminate(nil)
        }
    }

    private func runAll() async {
        try? FileManager.default.createDirectory(
            atPath: outputDirpath, withIntermediateDirectories: true)
        setUpHostWindow()

        // Multi-field form (like add-peer / add-subnet): focus must settle on the
        // FIRST field and stay there, not jump to the last field (#153).
        await captureFormFocusOverTime(name: "form_settle", clickSecondAfter: nil)
        // A click on the second field at ~150 ms must not be overridden by the
        // delayed focus assertion.
        await captureFormFocusOverTime(name: "form_click2", clickSecondAfter: 0.15)
        // Tab should walk the fields in order.
        await captureFormTabOrder(name: "form_tab")
        // Single-field popover is unaffected (focus stays on its one field).
        await captureStation(name: "station_settle", appearance: .darkAqua)

        // With a server port, also capture the rename popover's chips and preview.
        if let port = ProcessInfo.processInfo.environment["BEEBIUM_DEBUG_FOCUS_SELFTEST_PORT"],
           let portNumber = Int(port) {
            await connectServer(port: portNumber)
            await sleep(1.8)
            await captureRename(name: "rename_dark", appearance: .darkAqua, clickChip: true)
        }
    }

    // MARK: - Multi-field form focus

    private func multiFieldEditor() -> Beebium_Control {
        func textInput(_ id: String, _ label: String, _ value: String) -> Beebium_Control {
            var input = Beebium_TextInput()
            input.label = label
            input.value = value
            var control = Beebium_Control()
            control.id = id
            control.textInput = input
            return control
        }
        var group = Beebium_Group()
        group.label = "Add peer"
        group.controls = [
            textInput("host", "Host", "192.168.0.10"),
            textInput("port", "Port", "32768"),
            textInput("station", "Station", "101"),
            textInput("remark", "Remark", "File server"),
        ]
        var root = Beebium_Control()
        root.id = "root"
        root.group = group
        return root
    }

    /// Present the multi-field form as a sheet on the host window -- the same way
    /// the add-peer / add-subnet editors are presented (EditableList uses a sheet),
    /// so its window-becomes-key timing matches and the focus behaviour is faithful.
    private func presentFormSheet() -> NSWindow {
        bringHostToFront()
        let content = ExtensionEditorForm(editor: multiFieldEditor(), commitTitle: "Add",
                                          showCancel: true, onCancel: {}, onCommit: { _ in })
            .padding()
            .frame(width: 340)
        let sheet = NSWindow(contentViewController: NSHostingController(rootView: content))
        sheet.styleMask = [.titled]
        hostWindow?.beginSheet(sheet) { _ in }
        return sheet
    }

    private func captureFormFocusOverTime(name: String, clickSecondAfter: TimeInterval?) async {
        let sheet = presentFormSheet()
        let clickFrame = clickSecondAfter.map { Int(($0 / frameInterval).rounded()) }
        for frame in 0..<16 {
            if let clickFrame, frame == clickFrame, let root = sheet.contentView {
                clickTextView(index: 1, in: root)
            }
            capture(window: sheet, name: "\(name)_frame\(frame)")
            await sleep(frameInterval)
        }
        hostWindow?.endSheet(sheet)
        await sleep(0.4)
    }

    private func captureFormTabOrder(name: String) async {
        let sheet = presentFormSheet()
        await sleep(0.6)
        capture(window: sheet, name: "\(name)_0_initial")
        for step in 1...3 {
            sendTab(to: sheet, shift: false)
            await sleep(0.25)
            capture(window: sheet, name: "\(name)_\(step)_tab")
        }
        hostWindow?.endSheet(sheet)
        await sleep(0.4)
    }

    // MARK: - Host window

    private func setUpHostWindow() {
        let window = NSWindow(
            contentRect: NSRect(x: 200, y: 200, width: 440, height: 340),
            styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = "Focus Self-Test"
        window.contentView = NSView(frame: .zero)
        hostWindow = window
        bringHostToFront()
    }

    private func bringHostToFront() {
        NSApp.activate(ignoringOtherApps: true)
        for other in NSApp.windows where other !== hostWindow {
            other.orderOut(nil)
        }
        hostWindow?.makeKeyAndOrderFront(nil)
    }

    // MARK: - Server connection (for the rename picker + preview)

    private func connectServer(port: Int) async {
        let group = MultiThreadedEventLoopGroup(numberOfThreads: 1)
        eventLoopGroup = group
        do {
            let channel = try GRPCChannelPool.with(
                target: .host("127.0.0.1", port: port),
                transportSecurity: .plaintext,
                eventLoopGroup: group)
            let client = SystemClient()
            client.connect(channel: channel)
            serverSystemClient = client
            NSLog("[SELFTEST] connected to server on port %d", port)
        } catch {
            NSLog("[SELFTEST] server connect failed: %@", error.localizedDescription)
        }
    }

    // MARK: - Captures

    private func captureStation(name: String, appearance: NSAppearance.Name) async {
        bringHostToFront()
        let content = StationIdPopover(currentStationId: 11,
                                       econetClient: econetClient,
                                       breakKeyLabel: nil,
                                       isPresented: .constant(true))
        let popover = present(content, appearance: appearance)
        await sleep(0.5)
        // Select-all-on-open is visible before the click.
        await captureFrames(popover, prefix: "\(name)_open", count: 2)
        clickFirstField(in: popover)
        await sleep(0.1)
        await captureFrames(popover, prefix: "\(name)_click", count: framesPerOpen)
        popover.performClose(nil)
        await sleep(0.4)
    }

    private func captureRename(name: String, appearance: NSAppearance.Name,
                               clickChip: Bool) async {
        guard let systemClient = serverSystemClient else { return }
        bringHostToFront()
        let content = MachineRenameEditor(systemClient: systemClient, dismiss: {})
        let popover = present(content, appearance: appearance)
        await sleep(1.0)                 // let ListNamePlaceholders + preview load
        clickFirstField(in: popover)
        await sleep(0.1)
        await captureFrames(popover, prefix: "\(name)", count: framesPerOpen)
        if clickChip, let chip = firstChipButton(in: popover) {
            postClick(on: chip)
            await sleep(0.3)
            await captureFrames(popover, prefix: "\(name)_afterchip", count: framesPerOpen)
        }
        popover.performClose(nil)
        await sleep(0.4)
    }

    // MARK: - Popover plumbing

    private func present<V: View>(_ content: V, appearance: NSAppearance.Name) -> NSPopover {
        let popover = NSPopover()
        popover.behavior = .applicationDefined
        popover.appearance = NSAppearance(named: appearance)
        popover.contentViewController = NSHostingController(rootView: content)
        if let host = hostWindow, let anchor = host.contentView {
            popover.show(relativeTo: anchor.bounds, of: anchor, preferredEdge: .minY)
        }
        DispatchQueue.main.async { [weak self] in
            self?.hostWindow?.makeKey()
            popover.contentViewController?.view.window?.makeKey()
        }
        return popover
    }

    private func clickFirstField(in popover: NSPopover) {
        guard let root = popover.contentViewController?.view,
              let field = Self.firstTextView(in: root) else { return }
        postClick(on: field, at: NSPoint(x: 6, y: field.bounds.midY))
    }

    private func postClick(on view: NSView, at viewPoint: NSPoint? = nil) {
        guard let window = view.window else { return }
        let point = view.convert(viewPoint ?? NSPoint(x: view.bounds.midX, y: view.bounds.midY),
                                 to: nil)
        for type in [NSEvent.EventType.leftMouseUp, .leftMouseDown] {
            if let event = NSEvent.mouseEvent(
                with: type, location: point, modifierFlags: [],
                timestamp: ProcessInfo.processInfo.systemUptime,
                windowNumber: window.windowNumber, context: nil,
                eventNumber: 0, clickCount: 1,
                pressure: type == .leftMouseDown ? 1 : 0) {
                NSApp.postEvent(event, atStart: true)   // down first, then up
            }
        }
    }

    // MARK: - Capture

    private func captureFrames(_ popover: NSPopover, prefix: String, count: Int) async {
        for frame in 0..<count {
            capture(popover, name: "\(prefix)_frame\(frame)")
            await sleep(frameInterval)
        }
    }

    private func capture(_ popover: NSPopover, name: String) {
        guard let window = popover.contentViewController?.view.window else { return }
        capture(window: window, name: name)
    }

    private func capture(window: NSWindow, name: String) {
        let path = "\(outputDirpath)/\(name).png"
        let windowID = CGWindowID(window.windowNumber)
        if let image = CGWindowListCreateImage(
            .null, .optionIncludingWindow, windowID, [.boundsIgnoreFraming]),
           writePNG(cgImage: image, to: path) {
            return
        }
        if let view = window.contentView,
           let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
            view.cacheDisplay(in: view.bounds, to: rep)
            if let data = rep.representation(using: .png, properties: [:]) {
                try? data.write(to: URL(fileURLWithPath: path))
            }
        }
    }

    private func writePNG(cgImage: CGImage, to path: String) -> Bool {
        let rep = NSBitmapImageRep(cgImage: cgImage)
        guard let data = rep.representation(using: .png, properties: [:]) else { return false }
        return (try? data.write(to: URL(fileURLWithPath: path))) != nil
    }

    // MARK: - View tree

    private static func firstTextView(in view: NSView) -> NSTextView? {
        allTextViews(in: view).first
    }

    private static func allTextViews(in view: NSView) -> [NSTextView] {
        var found: [NSTextView] = []
        if let textView = view as? NSTextView { found.append(textView) }
        for subview in view.subviews {
            found.append(contentsOf: allTextViews(in: subview))
        }
        return found
    }

    private func clickTextView(index: Int, in root: NSView) {
        let textViews = Self.allTextViews(in: root)
        guard index < textViews.count else { return }
        let field = textViews[index]
        postClick(on: field, at: NSPoint(x: 6, y: field.bounds.midY))
    }

    private func sendTab(to window: NSWindow, shift: Bool) {
        let tabKeyCode: UInt16 = 48
        for type in [NSEvent.EventType.keyDown, .keyUp] {
            if let event = NSEvent.keyEvent(
                with: type, location: .zero,
                modifierFlags: shift ? [.shift] : [],
                timestamp: ProcessInfo.processInfo.systemUptime,
                windowNumber: window.windowNumber, context: nil,
                characters: "\t", charactersIgnoringModifiers: "\t",
                isARepeat: false, keyCode: tabKeyCode) {
                NSApp.postEvent(event, atStart: false)
            }
        }
    }

    /// The first placeholder chip -- a button whose title is an insertion string
    /// like "{econet-station}" -- skipping Cancel/Save.
    private func firstChipButton(in popover: NSPopover) -> NSButton? {
        guard let root = popover.contentViewController?.view else { return nil }
        // SwiftUI buttons expose no usable title, so a chip can't be matched by
        // text. The chips come before Cancel/Save in the tree, so the first button
        // that is not Cancel or Save is the first chip.
        return Self.firstButton(in: root) { button in
            let title = button.title
            return title != "Cancel" && title != "Save"
        }
    }

    private static func firstButton(in view: NSView,
                                    where match: (NSButton) -> Bool) -> NSButton? {
        if let button = view as? NSButton, match(button) { return button }
        for subview in view.subviews {
            if let found = firstButton(in: subview, where: match) { return found }
        }
        return nil
    }

    private func sleep(_ seconds: TimeInterval) async {
        try? await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
    }
}
