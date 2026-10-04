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

/// A single-line AppKit text field for use inside popovers and sheets, where a
/// SwiftUI `TextField` has two problems (#153):
///
/// - Overprint: in a popover's vibrant material a field with no opaque
///   background lets the cell draw its own unscrolled text under the field
///   editor's scrolled text. This field draws an opaque bezeled background.
/// - Invisible caret: a field's insertion point blinks only in the key window,
///   and a popover/sheet does not reliably become key before a field takes
///   focus. This field takes focus from the window becoming key -- it watches
///   `didBecomeKeyNotification` and (re-)makes itself first responder then,
///   clearing focus first so the insertion-point timer restarts even if it had
///   focus while the window was not key -- rather than racing focus on appear.
///   Because it is AppKit it also survives a parent re-render (the Network
///   sidebar rebuilds on every status event): `updateNSView` leaves the field
///   editor alone while the user is typing.
struct PopoverTextField: NSViewRepresentable {
    @Binding var text: String
    var placeholder: String = ""
    /// Optional controller exposing caret insertion (the rename picker uses it).
    var controller: TemplateFieldController?
    /// When set and BEEBIUM_DEBUG_FOCUS=1, logs focus transitions for this field.
    var diagnosticsLabel: String?
    let onSubmit: () -> Void
    let onCancel: () -> Void

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

        init(_ parent: PopoverTextField) { self.parent = parent }

        /// Take focus in the key window now if the window is already key, and
        /// again whenever it becomes key. Making the field first responder in a
        /// non-key window does not start its insertion-point timer, so we wait
        /// for the window to be key -- and re-assert on didBecomeKey so a field
        /// focused earlier (in a not-yet-key window) still starts blinking.
        func beginTakingFocus() {
            DispatchQueue.main.async { [weak self] in
                guard let self, let field = self.field, let window = field.window else { return }
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

        private func focus(in window: NSWindow) {
            guard let field else { return }
            // Clear then set so the insertion-point timer restarts even if this
            // field already held first responder in the not-yet-key window.
            window.makeFirstResponder(nil)
            window.makeFirstResponder(field)
            if let label = diagnosticsLabel {
                FocusDiagnostics.snapshot("\(label) focus()", window: window)
            }
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
