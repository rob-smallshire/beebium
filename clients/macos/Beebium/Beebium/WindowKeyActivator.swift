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

/// Makes the window hosting this view become key once it appears.
///
/// A text field shows and blinks its insertion point only in the key window. A
/// popover or sheet SwiftUI presents does not reliably make its window key
/// before a field inside it takes focus, so the caret can be invisible even
/// though the field has focus -- the "often, but not always" the user saw in the
/// rename popover, the Econet station editor and the AUN map entry editors. The
/// race is between the window becoming key and the field becoming first
/// responder; when focus wins, the insertion point never starts blinking.
///
/// Placing this on the root of popover or sheet content asks the hosting window
/// to become key as soon as it exists, so a field inside it draws its caret.
/// Applied through the `activatesHostingWindow()` modifier.
private struct WindowKeyActivator: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        let view = NSView(frame: .zero)
        // The window is not attached during makeNSView; ask on the next runloop,
        // by when the view is in the hierarchy and its window exists.
        DispatchQueue.main.async { [weak view] in
            guard let window = view?.window, !window.isKeyWindow else { return }
            window.makeKey()
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {}
}

extension View {
    /// Make the popover or sheet window hosting this content become key, so a
    /// text field inside it shows its insertion point. See `WindowKeyActivator`.
    func activatesHostingWindow() -> some View {
        background(
            WindowKeyActivator()
                .frame(width: 0, height: 0)
                .accessibilityHidden(true)
        )
    }
}
