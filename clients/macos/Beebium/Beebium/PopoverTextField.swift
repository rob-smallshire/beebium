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

/// Holds a weak reference to the live NSTextField behind a `PopoverTextField` so
/// a caller (the rename popover's placeholder picker) can insert text at the
/// caret. SwiftUI's `TextField` exposes no caret, which is one reason the
/// popovers use this AppKit-backed field.
@MainActor
final class TemplateFieldController: ObservableObject {
    weak var field: NSTextField?

    /// Insert `insertion` at the field's caret, or over its current selection,
    /// then update `text` and return the editing session to the field with the
    /// caret just after the inserted text and no selection. Falls back to
    /// appending if the field is not available yet.
    func insertAtCaret(_ insertion: String, text: Binding<String>) {
        guard let field else {
            text.wrappedValue += insertion
            return
        }
        let current = field.stringValue
        let selected = field.currentEditor()?.selectedRange
            ?? NSRange(location: (current as NSString).length, length: 0)
        let range = selected.location ..< (selected.location + selected.length)
        let (newText, caret) = NameTemplateEditing.insert(
            insertion, into: current, replacingUTF16: range)
        field.stringValue = newText
        text.wrappedValue = newText
        // The chip is non-activating, but a button click can still settle focus
        // after this runs, so re-assert first responder on the next runloop and
        // place the caret where the user will type next.
        DispatchQueue.main.async { [weak field] in
            guard let field, let window = field.window else { return }
            window.makeFirstResponder(field)
            field.currentEditor()?.selectedRange = NSRange(location: caret, length: 0)
        }
    }
}

/// Gives one text field its OWN field editor for the lifetime of a single
/// popover or sheet presentation (#153).
///
/// A window has one shared field editor it lends to every control that edits
/// text. On macOS 14 the caret's rendering state on that shared editor goes
/// stale after its first use, so the SECOND and later presentations of a popover
/// whose field reuses it show no caret even though the field is first responder
/// (proven from the user's focus log: the field editor is the same object across
/// every open while the field is new each time). Vending a dedicated editor per
/// presentation makes every open behave like the first.
///
/// Installed as the hosting window's delegate; every call other than the field
/// editor request is forwarded to the delegate it replaced, and the original is
/// restored on teardown.
final class DedicatedFieldEditorProvider: NSObject, NSWindowDelegate {
    weak var field: NSTextField?
    weak var previousDelegate: NSWindowDelegate?

    /// The field's own editor. A plain field editor the window configures when it
    /// vends it; created once per presentation and discarded with this provider.
    private(set) lazy var editor: NSTextView = {
        let editor = NSTextView()
        editor.isFieldEditor = true
        return editor
    }()

    func windowWillReturnFieldEditor(_ sender: NSWindow, to client: Any?) -> Any? {
        if let field, client as AnyObject? === field {
            return editor
        }
        return previousDelegate?.windowWillReturnFieldEditor?(sender, to: client)
    }

    // Forward every other NSWindowDelegate call to the delegate we replaced, so
    // the popover/sheet's own window behaviour is preserved.
    override func responds(to aSelector: Selector!) -> Bool {
        if super.responds(to: aSelector) { return true }
        return previousDelegate?.responds(to: aSelector) ?? false
    }

    override func forwardingTarget(for aSelector: Selector!) -> Any? {
        if let previousDelegate, previousDelegate.responds(to: aSelector) {
            return previousDelegate
        }
        return super.forwardingTarget(for: aSelector)
    }
}

/// A single-line AppKit text field for use inside popovers and sheets, where a
/// SwiftUI `TextField` has three problems (#153):
///
/// - Overprint: in a popover's vibrant material a field with no opaque
///   background lets the cell draw its own unscrolled text under the field
///   editor's scrolled text. This field draws an opaque bezeled background.
/// - Invisible caret, later opens: the window's shared field editor is reused
///   across presentations and its caret state goes stale, so only the first
///   presentation shows a caret. This field vends a dedicated editor per
///   presentation (see `DedicatedFieldEditorProvider`) and ends editing cleanly
///   on dismissal, so every open is a first open.
/// - Because it is AppKit it also survives a parent re-render (the Network
///   sidebar rebuilds on every status event): `updateNSView` leaves the field
///   editor alone once the user is typing.
struct PopoverTextField: NSViewRepresentable {
    @Binding var text: String
    var placeholder: String = ""
    /// Optional controller exposing caret insertion (the rename picker uses it).
    var controller: TemplateFieldController?
    /// When set and BEEBIUM_DEBUG_FOCUS=1, logs focus transitions for this field.
    var diagnosticsLabel: String?
    let onSubmit: () -> Void
    let onCancel: () -> Void

    /// How many times updateNSView has run, for the invalidation-storm tests to
    /// read. A re-host per frame means the window tree is being invalidated under
    /// the field (#153). Always compiled so the Release test target can read it.
    @MainActor static var updateCountForTesting = 0

    func makeNSView(context: Context) -> NSTextField {
        let field = NSTextField()
        field.placeholderString = placeholder
        field.stringValue = text
        field.delegate = context.coordinator
        field.usesSingleLineMode = true
        field.cell?.wraps = false
        field.cell?.isScrollable = true
        field.isBezeled = true
        field.bezelStyle = .roundedBezel
        field.drawsBackground = true
        field.backgroundColor = .textBackgroundColor
        controller?.field = field
        context.coordinator.field = field
        context.coordinator.diagnosticsLabel = diagnosticsLabel
        context.coordinator.beginTakingFocus()
        return field
    }

    func updateNSView(_ nsView: NSTextField, context: Context) {
        context.coordinator.parent = self
        Self.updateCountForTesting += 1
        FocusRates.tick("updateNSView")
        // Apply the model value until the user starts typing -- the initial value
        // must land even though the field has already taken focus (its field
        // editor exists), which a plain "don't touch while an editor exists" guard
        // wrongly blocked, opening the field empty. Once the user has typed, their
        // text wins, so a parent re-render (the Network sidebar on a status event)
        // cannot clobber it or move the caret.
        if !context.coordinator.userBeganEditing && nsView.stringValue != text {
            nsView.stringValue = text
        }
    }

    static func dismantleNSView(_ nsView: NSTextField, coordinator: Coordinator) {
        coordinator.teardown(field: nsView)
    }

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject, NSTextFieldDelegate {
        var parent: PopoverTextField
        weak var field: NSTextField?
        var diagnosticsLabel: String?
        /// True once the user has actually edited the field, after which the model
        /// no longer overwrites what they typed. Before that, updateNSView is free
        /// to apply the initial/changed value even though the editor exists.
        var userBeganEditing = false
        private var keyObserver: NSObjectProtocol?
        private var editorProvider: DedicatedFieldEditorProvider?
        private weak var editorWindow: NSWindow?

        init(_ parent: PopoverTextField) { self.parent = parent }

        /// Install the dedicated field editor, then take focus -- in the key
        /// window now if the window is already key, and again whenever it becomes
        /// key (the popover/sheet may not be key when the field first appears).
        func beginTakingFocus() {
            DispatchQueue.main.async { [weak self] in
                guard let self, let field = self.field, let window = field.window else { return }
                self.installDedicatedEditor(in: window, for: field)
                if window.isKeyWindow {
                    self.focus(in: window)
                } else {
                    window.makeKey()
                }
                self.keyObserver = NotificationCenter.default.addObserver(
                    forName: NSWindow.didBecomeKeyNotification,
                    object: window, queue: .main) { [weak self] _ in
                        self?.focus(in: window)
                    }
            }
        }

        private func installDedicatedEditor(in window: NSWindow, for field: NSTextField) {
            let provider = DedicatedFieldEditorProvider()
            provider.field = field
            provider.previousDelegate = window.delegate
            window.delegate = provider
            editorProvider = provider
            editorWindow = window
        }

        private func focus(in window: NSWindow) {
            guard let field else { return }
            // Make the field first responder so the window vends our dedicated
            // editor. If the shared editor was somehow installed first, clear
            // first responder so the window re-vends through our provider.
            if let provider = editorProvider, field.currentEditor() !== provider.editor {
                window.makeFirstResponder(nil)
            }
            window.makeFirstResponder(field)
            if let label = diagnosticsLabel {
                FocusDiagnostics.snapshot("\(label) focus()", window: window)
                FocusDiagnostics.dumpEditor("\(label) focus()", field: field)
                FocusDiagnostics.locateCaret("\(label) focus()", field: field)
                for delay in [0.5, 1.5] {
                    DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak field] in
                        guard let field else { return }
                        FocusDiagnostics.dumpEditor("\(label)+\(Int(delay * 1000))ms", field: field)
                        FocusDiagnostics.locateCaret("\(label)+\(Int(delay * 1000))ms", field: field)
                    }
                }
            }
        }

        /// End the editing session and restore the window delegate when the field
        /// goes away, so nothing is left mid-session on the shared machinery and
        /// the next presentation starts clean.
        func teardown(field: NSTextField) {
            if let keyObserver {
                NotificationCenter.default.removeObserver(keyObserver)
                self.keyObserver = nil
            }
            // End the editing session on the window we captured (the field's own
            // window pointer may already be nil by dismantle time), so the shared
            // field editor is not left bound to this dead presentation -- the
            // "nothing left mid-session" the next open needs.
            let window = editorWindow ?? field.window
            if let window {
                if let editor = field.currentEditor() {
                    window.endEditing(for: editor)
                }
                field.abortEditing()
                // Resign first responder if it is still our field or its editor.
                if window.firstResponder === field || window.firstResponder === field.currentEditor() {
                    window.makeFirstResponder(window)
                }
                if let provider = editorProvider, window.delegate === provider {
                    window.delegate = provider.previousDelegate
                }
            }
            editorProvider = nil
        }

        deinit {
            if let keyObserver {
                NotificationCenter.default.removeObserver(keyObserver)
            }
        }

        func controlTextDidChange(_ obj: Notification) {
            guard let field = obj.object as? NSTextField else { return }
            userBeganEditing = true
            parent.text = field.stringValue
        }

        func control(_ control: NSControl,
                     textView: NSTextView,
                     doCommandBy commandSelector: Selector) -> Bool {
            switch commandSelector {
            case #selector(NSResponder.insertNewline(_:)):
                parent.onSubmit()
                return true
            case #selector(NSResponder.cancelOperation(_:)):
                parent.onCancel()
                return true
            default:
                return false
            }
        }
    }
}
