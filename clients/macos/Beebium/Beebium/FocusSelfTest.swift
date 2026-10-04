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

/// A self-driving visual check for the popover caret (#153). The caret cannot be
/// seen from a unit test (the test host cannot make a window key, and a caret
/// blinks), so this drives the REAL popovers in a running, activated app,
/// synthesises a real click into each field, photographs each popover's own
/// window several times across its blink, and writes PNGs for a human (or Claude,
/// via the Read tool) to look at.
///
/// Enabled only when BEEBIUM_DEBUG_FOCUS_SELFTEST is set to an output directory;
/// compiled in but inert otherwise. Started from applicationDidFinishLaunching.
/// It opens its own window (so it does not depend on launching a machine), shows
/// the real editor content in real NSPopovers anchored to it -- the same popover
/// window machinery and the same shared field editor the app reuses across opens
/// -- captures, and then quits the app.
@MainActor
final class FocusSelfTest {
    static func startIfRequested() {
        guard let dir = ProcessInfo.processInfo.environment["BEEBIUM_DEBUG_FOCUS_SELFTEST"],
              !dir.isEmpty else { return }
        NSLog("[SELFTEST] startIfRequested dir=%@", dir)
        let test = FocusSelfTest(outputDirpath: dir)
        // Let the normal launch settle (the Welcome window appears), then run.
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            test.start()
        }
    }

    private let outputDirpath: String
    private var hostWindow: NSWindow?
    private let econetClient = EconetClient()
    private let systemClient = SystemClient()
    /// Frames per open, 100 ms apart, to catch the caret across its blink.
    private let framesPerOpen = 10
    private let frameInterval: TimeInterval = 0.1

    init(outputDirpath: String) {
        self.outputDirpath = outputDirpath
    }

    private func start() {
        NSLog("[SELFTEST] start()")
        Task { @MainActor in
            NSLog("[SELFTEST] runAll begin")
            await runAll()
            NSLog("[SELFTEST] done; PNGs in %@", outputDirpath)
            NSApp.terminate(nil)
        }
    }

    // MARK: - Orchestration

    private func runAll() async {
        try? FileManager.default.createDirectory(
            atPath: outputDirpath, withIntermediateDirectories: true)

        setUpHostWindow()

        // Seed a machine identity so the rename field opens with a real template.
        var identity = Beebium_MachineIdentity()
        identity.name = "Station 11 (AUN, Model B) #1"
        identity.nameTemplate = "Station {econet-station} (AUN, Model B) {machine-ordinal}"
        systemClient.updateIdentity(identity)

        NSLog("[SELFTEST] host window up; beginning station captures")
        for open in 1...4 {
            NSLog("[SELFTEST] station open %d", open)
            await captureStationEditor(open: open)
            await sleep(1.0)
        }
        await captureRenameEditor()
        await sleep(1.0)
        await captureAddPeerSheet()
    }

    private func setUpHostWindow() {
        let window = NSWindow(
            contentRect: NSRect(x: 200, y: 200, width: 420, height: 320),
            styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = "Focus Self-Test"
        let content = NSView(frame: window.contentRect(forFrameRect: window.frame))
        window.contentView = content
        hostWindow = window
        bringHostToFront()
    }

    private func bringHostToFront() {
        NSApp.activate(ignoringOtherApps: true)
        // Order out the Welcome / other windows so the popover is anchored to and
        // keyed by proxy through our host window alone.
        for other in NSApp.windows where other !== hostWindow {
            other.orderOut(nil)
        }
        hostWindow?.makeKeyAndOrderFront(nil)
    }

    // MARK: - Captures

    private func captureStationEditor(open: Int) async {
        bringHostToFront()
        let content = StationIdPopover(currentStationId: 11,
                                       econetClient: econetClient,
                                       breakKeyLabel: nil,
                                       isPresented: .constant(true))
        let popover = present(content)
        await sleep(0.5)                       // appear, become key, take focus
        clickFirstField(in: popover)
        await sleep(0.1)
        await captureFrames(popover, prefix: "station_open\(open)")
        popover.performClose(nil)
        await sleep(0.4)
    }

    private func captureRenameEditor() async {
        bringHostToFront()
        let content = MachineRenameEditor(systemClient: systemClient, dismiss: {})
        let popover = present(content)
        await sleep(0.5)
        clickFirstField(in: popover)
        await sleep(0.1)
        await captureFrames(popover, prefix: "rename_click")
        // The placeholder chips need a connected server (ListNamePlaceholders), so
        // chip-insertion caret return is not exercised here; it has unit coverage.
        popover.performClose(nil)
        await sleep(0.4)
    }

    private func captureAddPeerSheet() async {
        bringHostToFront()
        var textInput = Beebium_TextInput()
        textInput.label = "Host"
        textInput.value = "192.168.0.10"
        var field = Beebium_Control()
        field.id = "host"
        field.textInput = textInput
        var group = Beebium_Group()
        group.label = "Add peer"
        group.controls = [field]
        var root = Beebium_Control()
        root.id = "root"
        root.group = group

        let content = ExtensionEditorForm(editor: root, commitTitle: "Add",
                                          showCancel: true, onCancel: {}, onCommit: { _ in })
            .padding()
            .frame(width: 320)
        let popover = present(content)
        await sleep(0.5)
        clickFirstField(in: popover)
        await sleep(0.1)
        await captureFrames(popover, prefix: "addpeer_click")
        popover.performClose(nil)
        await sleep(0.4)
    }

    // MARK: - Popover plumbing

    private func present<V: View>(_ content: V) -> NSPopover {
        let popover = NSPopover()
        popover.behavior = .applicationDefined   // we close it ourselves
        popover.contentViewController = NSHostingController(rootView: content)
        if let host = hostWindow, let anchor = host.contentView {
            popover.show(relativeTo: anchor.bounds, of: anchor, preferredEdge: .minY)
        }
        // A popover takes key by proxy through its parent; make sure the parent is
        // key so the field editor's caret runs, mirroring the real app.
        DispatchQueue.main.async { [weak self] in
            self?.hostWindow?.makeKey()
            popover.contentViewController?.view.window?.makeKey()
        }
        return popover
    }

    private func clickFirstField(in popover: NSPopover) {
        guard let root = popover.contentViewController?.view,
              let field = Self.firstTextView(in: root),
              let window = field.window else { return }
        let point = field.convert(NSPoint(x: 6, y: field.bounds.midY), to: nil)
        // POST the events to the queue rather than sendEvent: a text field's
        // mouseDown enters a tracking loop that waits for the mouseUp, so calling
        // sendEvent(mouseDown) directly blocks the main thread forever. Posting
        // both lets the normal run loop dequeue the down (enter tracking) then the
        // up (leave tracking) -- a real click that actually places the caret.
        for type in [NSEvent.EventType.leftMouseUp, .leftMouseDown] {
            if let event = NSEvent.mouseEvent(
                with: type, location: point, modifierFlags: [],
                timestamp: ProcessInfo.processInfo.systemUptime,
                windowNumber: window.windowNumber, context: nil,
                eventNumber: 0, clickCount: 1,
                pressure: type == .leftMouseDown ? 1 : 0) {
                // atStart: down first (prepended last), up after it.
                NSApp.postEvent(event, atStart: true)
            }
        }
    }

    // MARK: - Capture

    private func captureFrames(_ popover: NSPopover, prefix: String) async {
        for frame in 0..<framesPerOpen {
            capture(popover, name: "\(prefix)_frame\(frame)")
            await sleep(frameInterval)
        }
    }

    private func capture(_ popover: NSPopover, name: String) {
        guard let view = popover.contentViewController?.view,
              let window = view.window else { return }
        let path = "\(outputDirpath)/\(name).png"
        let windowID = CGWindowID(window.windowNumber)
        if let image = CGWindowListCreateImage(
            .null, .optionIncludingWindow, windowID, [.boundsIgnoreFraming]),
           writePNG(cgImage: image, to: path) {
            return
        }
        // Fallback: cache the view's display into a bitmap.
        if let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
            view.cacheDisplay(in: view.bounds, to: rep)
            if let data = rep.representation(using: .png, properties: [:]) {
                try? data.write(to: URL(fileURLWithPath: path))
                NSLog("[SELFTEST] wrote %@ (cacheDisplay)", name)
            }
        }
    }

    private func writePNG(cgImage: CGImage, to path: String) -> Bool {
        let rep = NSBitmapImageRep(cgImage: cgImage)
        guard let data = rep.representation(using: .png, properties: [:]) else { return false }
        do {
            try data.write(to: URL(fileURLWithPath: path))
            NSLog("[SELFTEST] wrote %@ (CGWindowListCreateImage)",
                  (path as NSString).lastPathComponent)
            return true
        } catch {
            return false
        }
    }

    // MARK: - View tree

    private static func firstTextView(in view: NSView) -> NSTextView? {
        if let textView = view as? NSTextView { return textView }
        for subview in view.subviews {
            if let found = firstTextView(in: subview) { return found }
        }
        return nil
    }

    private func sleep(_ seconds: TimeInterval) async {
        try? await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
    }
}
