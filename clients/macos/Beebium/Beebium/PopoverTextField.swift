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

/// Holds a weak reference to the live text view behind a `PopoverTextField` so a
/// caller (the rename popover's placeholder picker) can insert text at the caret.
@MainActor
final class TemplateFieldController: ObservableObject {
    weak var textView: NSTextView?

    /// Insert `insertion` at the caret, or over the current selection, then update
    /// `text` and leave the caret just after the inserted text with focus held.
    func insertAtCaret(_ insertion: String, text: Binding<String>) {
        guard let textView else {
            text.wrappedValue += insertion
            return
        }
        let current = textView.string
        let selected = textView.selectedRange()
        let range = selected.location ..< (selected.location + selected.length)
        let (newText, caret) = NameTemplateEditing.insert(
            insertion, into: current, replacingUTF16: range)
        textView.string = newText
        text.wrappedValue = newText
        textView.window?.makeFirstResponder(textView)
        textView.setSelectedRange(NSRange(location: caret, length: 0))
    }
}

/// A single-line editable text control for popovers and sheets, backed by an
/// NSTextView rather than an NSTextField (#153).
///
/// The reason is the caret. An NSTextField edits through the window's ONE shared
/// field editor, and on macOS 14 that shared editor, reused across popover
/// presentations anchored to the same window, stops drawing its caret after the
/// first use -- so the second and later opens of a popover show no caret even
/// after a click (confirmed by photographing the running app: open 1 has a caret,
/// open 4 does not, and the field is not even first responder). An NSTextView is
/// its own editor with its own insertion point and never touches the shared field
/// editor, so every presentation behaves like the first.
///
/// It is styled as a bezeled single-line field, draws its own placeholder, and
/// commits on Return / cancels on Escape.
struct PopoverTextField: NSViewRepresentable {
    @Binding var text: String
    var placeholder: String = ""
    /// Optional controller exposing caret insertion (the rename picker uses it).
    var controller: TemplateFieldController?
    /// When set and BEEBIUM_DEBUG_FOCUS=1, logs focus transitions.
    var diagnosticsLabel: String?
    let onSubmit: () -> Void
    let onCancel: () -> Void

    /// How many times updateNSView has run, for the invalidation-storm tests.
    @MainActor static var updateCountForTesting = 0

    func makeNSView(context: Context) -> NSScrollView {
        let textView = SingleLineTextView()
        textView.delegate = context.coordinator
        textView.isRichText = false
        textView.importsGraphics = false
        textView.isFieldEditor = false
        textView.allowsUndo = true
        textView.font = .systemFont(ofSize: NSFont.systemFontSize)
        textView.textColor = .controlTextColor
        textView.insertionPointColor = .controlTextColor
        // The rounded bezel is drawn by the enclosing scroll view's layer; the
        // text view itself is transparent so its text sits on that background and
        // the corners stay rounded.
        textView.drawsBackground = false
        // Inset so the single line's baseline sits where an NSTextField's does.
        textView.textContainerInset = NSSize(width: 3, height: 3)
        textView.isVerticallyResizable = false
        textView.isHorizontallyResizable = true
        textView.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude,
                                  height: CGFloat.greatestFiniteMagnitude)
        textView.textContainer?.widthTracksTextView = false
        textView.textContainer?.containerSize = NSSize(width: CGFloat.greatestFiniteMagnitude,
                                                       height: CGFloat.greatestFiniteMagnitude)
        textView.textContainer?.lineFragmentPadding = 2
        textView.string = text
        textView.placeholder = placeholder
        textView.onSubmit = onSubmit
        textView.onCancel = onCancel

        let scroll = BezelTextFieldScrollView()
        scroll.documentView = textView
        scroll.borderType = .noBorder          // the layer draws the bezel instead
        scroll.hasVerticalScroller = false
        scroll.hasHorizontalScroller = false
        scroll.autohidesScrollers = true
        scroll.verticalScrollElasticity = .none
        scroll.drawsBackground = false
        scroll.contentView.drawsBackground = false

        controller?.textView = textView
        context.coordinator.textView = textView
        context.coordinator.diagnosticsLabel = diagnosticsLabel
        context.coordinator.beginTakingFocus()
        return scroll
    }

    func updateNSView(_ nsView: NSScrollView, context: Context) {
        context.coordinator.parent = self
        Self.updateCountForTesting += 1
        guard let textView = nsView.documentView as? NSTextView else { return }
        // Apply the model value until the user starts typing, so the initial value
        // lands even though the view already has focus; after that their text wins
        // so a parent re-render cannot clobber it or move the caret.
        if !context.coordinator.userBeganEditing && textView.string != text {
            textView.string = text
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject, NSTextViewDelegate {
        var parent: PopoverTextField
        weak var textView: NSTextView?
        var diagnosticsLabel: String?
        var userBeganEditing = false
        private var keyObserver: NSObjectProtocol?

        init(_ parent: PopoverTextField) { self.parent = parent }

        /// Focus the text view in the key window (now if already key, else when it
        /// becomes key) so its insertion point starts drawing.
        func beginTakingFocus() {
            DispatchQueue.main.async { [weak self] in
                guard let self, let textView = self.textView, let window = textView.window
                else { return }
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
            guard let textView else { return }
            window.makeFirstResponder(textView)
            // Select all on open, as a freshly-focused NSTextField does, so typing
            // replaces the value; a click then places the caret.
            if !userBeganEditing {
                textView.selectedRange = NSRange(location: 0, length: (textView.string as NSString).length)
            }
            if let label = diagnosticsLabel {
                FocusDiagnostics.snapshot("\(label) focus()", window: window)
            }
        }

        deinit {
            if let keyObserver {
                NotificationCenter.default.removeObserver(keyObserver)
            }
        }

        func textDidChange(_ notification: Notification) {
            guard let textView = notification.object as? NSTextView else { return }
            userBeganEditing = true
            parent.text = textView.string
            textView.needsDisplay = true    // repaint the placeholder region
        }

        func textView(_ textView: NSTextView,
                      doCommandBy commandSelector: Selector) -> Bool {
            switch commandSelector {
            case #selector(NSResponder.insertNewline(_:)):
                parent.onSubmit()
                return true
            case #selector(NSResponder.cancelOperation(_:)):
                parent.onCancel()
                return true
            case #selector(NSResponder.insertTab(_:)),
                 #selector(NSResponder.insertBacktab(_:)):
                // Let the window move focus rather than inserting a tab.
                return false
            default:
                return false
            }
        }
    }
}

/// A single-line NSTextView: it refuses newlines (handled as commit by the
/// delegate), keeps everything on one line, draws a placeholder when empty, and
/// tells its enclosing bezel when it starts and stops editing so the bezel can
/// show the focus ring.
final class SingleLineTextView: NSTextView {
    var placeholder: String = ""
    var onSubmit: (() -> Void)?
    var onCancel: (() -> Void)?

    private var bezel: BezelTextFieldScrollView? {
        enclosingScrollView as? BezelTextFieldScrollView
    }

    override func becomeFirstResponder() -> Bool {
        let became = super.becomeFirstResponder()
        if became { bezel?.isEditing = true }
        return became
    }

    override func resignFirstResponder() -> Bool {
        let resigned = super.resignFirstResponder()
        if resigned { bezel?.isEditing = false }
        return resigned
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        guard string.isEmpty, !placeholder.isEmpty else { return }
        let attributes: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.placeholderTextColor,
            .font: font ?? NSFont.systemFont(ofSize: NSFont.systemFontSize),
        ]
        let origin = NSPoint(x: textContainerInset.width + (textContainer?.lineFragmentPadding ?? 0),
                             y: textContainerInset.height)
        placeholder.draw(at: origin, withAttributes: attributes)
    }
}

/// Draws the rounded bezel, background and focus ring of a native single-line
/// text field around the text view it scrolls, and sizes itself to a field's
/// height so SwiftUI lays it out without an explicit frame. Layer-backed so the
/// text view's content is clipped to the rounded corners.
final class BezelTextFieldScrollView: NSScrollView {
    var isEditing = false {
        didSet {
            guard isEditing != oldValue else { return }
            needsDisplay = true
        }
    }

    override var intrinsicContentSize: NSSize {
        NSSize(width: NSView.noIntrinsicMetric, height: 22)
    }

    override var wantsUpdateLayer: Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        wantsLayer = true
        needsDisplay = true
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        needsDisplay = true
    }

    override func updateLayer() {
        guard let layer else { return }
        // Resolve colours in this view's effective appearance so light and dark
        // both look native.
        effectiveAppearance.performAsCurrentDrawingAppearance {
            layer.cornerRadius = 5
            layer.masksToBounds = true
            layer.backgroundColor = NSColor.textBackgroundColor.cgColor
            if isEditing {
                layer.borderWidth = 2
                layer.borderColor = NSColor.controlAccentColor.cgColor
            } else {
                layer.borderWidth = 1
                layer.borderColor = NSColor.separatorColor.cgColor
            }
        }
    }
}
