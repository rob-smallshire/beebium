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
import Combine
import SwiftUI

/// Opt-in logging for the popover/sheet caret problem (#153). Compiled in but
/// silent unless the environment variable BEEBIUM_DEBUG_FOCUS=1 is set, so the
/// user can launch, reproduce a missing caret, and send a log that shows which
/// condition differs between a good and a bad open.
///
/// Run with:  BEEBIUM_DEBUG_FOCUS=1 /path/to/Beebium.app/Contents/MacOS/Beebium
/// then open the editor a few times; filter the log for "[FOCUS]".
enum FocusDiagnostics {
    static let isEnabled =
        ProcessInfo.processInfo.environment["BEEBIUM_DEBUG_FOCUS"] == "1"

    /// Log one snapshot of who holds focus and which window is key.
    static func snapshot(_ label: String, window: NSWindow?) {
        guard isEnabled else { return }
        let firstResponder = window?.firstResponder
        let fieldEditor = firstResponder as? NSTextView
        let key = NSApp.keyWindow
        NSLog("""
            [FOCUS] %@ | win=%@ isKey=%d canBecomeKey=%d | appKey=%@ "%@" | \
            firstResponder=%@ isFieldEditor=%d fieldEditorDelegate=%@
            """,
            label,
            window.map { String(describing: type(of: $0)) } ?? "nil",
            (window?.isKeyWindow ?? false) ? 1 : 0,
            (window?.canBecomeKey ?? false) ? 1 : 0,
            key.map { String(describing: type(of: $0)) } ?? "nil",
            key?.title ?? "",
            firstResponder.map { String(describing: type(of: $0)) } ?? "nil",
            fieldEditor != nil ? 1 : 0,
            fieldEditor?.delegate.map { String(describing: type(of: $0)) } ?? "nil")
    }
}

/// Per-second rate counting for the invalidation-storm investigation (#153): a
/// caret that draws on the first popover open but never after fits SwiftUI
/// re-evaluating the open popover continuously (a high-rate @Published observed
/// by the window tree), so updateNSView runs many times a second and the field
/// editor's insertion indicator never gets to draw. `tick` counts labelled
/// events; once a second the totals are logged as one [RATE] line and reset.
/// No-op unless BEEBIUM_DEBUG_FOCUS=1.
@MainActor
enum FocusRates {
    private static var counts: [String: Int] = [:]
    private static var timer: Timer?

    static func tick(_ label: String) {
        guard FocusDiagnostics.isEnabled else { return }
        counts[label, default: 0] += 1
        ensureTimer()
    }

    private static func ensureTimer() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in
            MainActor.assumeIsolated {
                guard !counts.isEmpty else { return }
                let line = counts.sorted { $0.key < $1.key }
                    .map { "\($0.key)=\($0.value)" }
                    .joined(separator: " ")
                NSLog("[RATE] %@", line)
                counts.removeAll()
            }
        }
    }
}

extension FocusDiagnostics {
    /// Count how often `object` publishes a change, under `pub:<name>`, so the
    /// [RATE] line names which observable is storming while a popover is open.
    static func observePublisher<O: ObservableObject>(_ object: O,
                                                      named name: String) -> AnyCancellable? {
        guard isEnabled else { return nil }
        return object.objectWillChange
            .receive(on: DispatchQueue.main)
            .sink { _ in MainActor.assumeIsolated { FocusRates.tick("pub:\(name)") } }
    }
}

/// Attaches focus logging to a view's hosting window: a snapshot at appear and
/// at +100 ms, +300 ms and +1 s, plus one on every didBecomeKey / didResignKey
/// of that window. No-op unless BEEBIUM_DEBUG_FOCUS=1. See `FocusDiagnostics`.
private struct FocusDiagnosticsProbe: NSViewRepresentable {
    let label: String

    func makeNSView(context: Context) -> NSView {
        let view = NSView(frame: .zero)
        guard FocusDiagnostics.isEnabled else { return view }
        for delay in [0.0, 0.1, 0.3, 1.0] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak view] in
                FocusDiagnostics.snapshot("\(label)+\(Int(delay * 1000))ms",
                                          window: view?.window)
            }
        }
        context.coordinator.observe(label: label) { [weak view] in view?.window }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator() }

    final class Coordinator {
        private var tokens: [NSObjectProtocol] = []

        func observe(label: String, window: @escaping () -> NSWindow?) {
            DispatchQueue.main.async { [weak self] in
                guard let self, let window = window() else { return }
                let center = NotificationCenter.default
                tokens.append(center.addObserver(
                    forName: NSWindow.didBecomeKeyNotification,
                    object: window, queue: .main) { _ in
                        FocusDiagnostics.snapshot("\(label) didBecomeKey", window: window)
                    })
                tokens.append(center.addObserver(
                    forName: NSWindow.didResignKeyNotification,
                    object: window, queue: .main) { _ in
                        FocusDiagnostics.snapshot("\(label) didResignKey", window: window)
                    })
            }
        }

        deinit {
            tokens.forEach { NotificationCenter.default.removeObserver($0) }
        }
    }
}

extension View {
    /// Log focus/key-window state for this content's hosting window. No-op unless
    /// BEEBIUM_DEBUG_FOCUS=1. See `FocusDiagnostics`.
    func focusDiagnostics(_ label: String) -> some View {
        background(
            FocusDiagnosticsProbe(label: label)
                .frame(width: 0, height: 0)
                .accessibilityHidden(true)
        )
    }
}
